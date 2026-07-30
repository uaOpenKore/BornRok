#include "formats/UnityAnimClip.hpp"

#include <cmath>
#include <cstring>
#include <map>

#include "core/Log.hpp"

namespace uaro {

namespace {

struct LeReader {
    const u8* p;
    usize n;
    usize at = 0;

    bool ok(usize need) const { return at + need <= n; }
    u32 u32v() {
        u32 v;
        std::memcpy(&v, p + at, 4);
        at += 4;
        return v;
    }
    float f32v() {
        float f;
        std::memcpy(&f, p + at, 4);
        at += 4;
        return f;
    }
    u8 u8v() { return p[at++]; }
    void align4() { at = (at + 3) & ~usize{3}; }
    bool skip(usize k) {
        if (!ok(k)) return false;
        at += k;
        return true;
    }
    bool skipVec(usize elem) {  // u32 count + count*elem bytes
        if (!ok(4)) return false;
        const u32 c = u32v();
        return skip(static_cast<usize>(c) * elem);
    }
};

}  // namespace

std::optional<UnityAnimClipData> parseUnityAnimClip(const std::vector<u8>& b) {
    LeReader r{b.data(), b.size()};
    UnityAnimClipData out;
    if (!r.ok(4)) return std::nullopt;
    const u32 nameLen = r.u32v();
    if (nameLen > 4096 || !r.ok(nameLen)) return std::nullopt;
    out.name.assign(reinterpret_cast<const char*>(b.data() + r.at), nameLen);
    r.at = (r.at + nameLen + 3) & ~usize{3};

    // 3 bools + align, 7 empty curve arrays, sampleRate, wrapMode, bounds — verified layout.
    if (!r.skip(4)) return std::nullopt;
    for (int i = 0; i < 7; ++i)
        if (!r.ok(4) || r.u32v() != 0) {
            log::warn("UnityAnimClip '{}': non-empty legacy curves, unsupported", out.name);
            return std::nullopt;
        }
    if (!r.ok(4 * 9)) return std::nullopt;
    out.sampleRate = r.f32v();
    r.skip(4);       // wrapMode
    r.skip(24);      // bounds
    r.skip(4);       // muscleClipSize
    // ClipMuscleConstant (offsets verified on the real clip): HumanPose head + goals + hands
    // + muscle arrays + 4 xforms + avgSpeed.
    if (!r.skip(68)) return std::nullopt;  // rootX/lookAtPos/lookAtWeight
    if (!r.ok(4)) return std::nullopt;
    {
        const u32 goals = r.u32v();  // m_GoalArray: count + count x HumanGoal(64)
        if (!r.skip(static_cast<usize>(goals) * 64)) return std::nullopt;
    }
    for (int hand = 0; hand < 2; ++hand) {
        if (!r.skip(40)) return std::nullopt;  // grabX
        if (!r.skipVec(4)) return std::nullopt;  // DoF floats (20)
        if (!r.skip(16)) return std::nullopt;  // override/closeOpen/inOut/grab
    }
    if (!r.skipVec(4)) return std::nullopt;    // m_DoFArray (55)
    if (!r.skipVec(12)) return std::nullopt;   // m_TDoFArray (21 float3)
    if (!r.skip(40 * 4 + 12)) return std::nullopt;  // 4 xforms + avgSpeed

    // Clip: StreamedClip + DenseClip + ConstantClip.
    if (!r.ok(4)) return std::nullopt;
    const u32 streamWords = r.u32v();
    if (!r.ok(static_cast<usize>(streamWords) * 4)) return std::nullopt;
    const u8* stream = b.data() + r.at;
    r.skip(static_cast<usize>(streamWords) * 4);
    if (!r.ok(4)) return std::nullopt;
    const u32 streamCurves = r.u32v();
    if (!r.ok(16)) return std::nullopt;
    const u32 denseFrames = r.u32v();
    const u32 denseCurves = r.u32v();
    r.f32v();  // dense sampleRate (== clip rate)
    const float denseBegin = r.f32v();
    if (!r.ok(4)) return std::nullopt;
    const u32 denseSamples = r.u32v();
    if (denseSamples != denseFrames * denseCurves || !r.ok(static_cast<usize>(denseSamples) * 4))
        return std::nullopt;
    const u8* dense = b.data() + r.at;
    r.skip(static_cast<usize>(denseSamples) * 4);
    if (!r.ok(4)) return std::nullopt;
    const u32 constCount = r.u32v();
    if (!r.ok(static_cast<usize>(constCount) * 4)) return std::nullopt;
    const u8* constData = b.data() + r.at;
    r.skip(static_cast<usize>(constCount) * 4);

    // Post-clip scalars + arrays, then the binding table.
    if (!r.ok(4 * 6)) return std::nullopt;
    const float startTime = r.f32v();
    const float stopTime = r.f32v();
    r.skip(4 * 4);  // orientationOffsetY/level/cycleOffset/averageAngularSpeed
    if (!r.skipVec(4)) return std::nullopt;   // m_IndexArray (i32)
    if (!r.skipVec(8)) return std::nullopt;   // m_ValueArrayDelta {start,stop}
    if (!r.skipVec(4)) return std::nullopt;   // m_ValueArrayReferencePose
    if (!r.skip(11)) return std::nullopt;     // 11 loop/mirror bools (5.5+ set)
    r.align4();

    // m_ClipBindingConstant.genericBindings.
    if (!r.ok(4)) return std::nullopt;
    const u32 nBind = r.u32v();
    if (nBind == 0 || nBind > 4096) {
        log::warn("UnityAnimClip '{}': implausible binding count {}", out.name, nBind);
        return std::nullopt;
    }
    u32 slot = 0;
    for (u32 i = 0; i < nBind; ++i) {
        if (!r.ok(8)) return std::nullopt;
        UnityAnimBinding bd;
        bd.pathHash = r.u32v();
        bd.attribute = r.u32v();
        if (!r.skip(12)) return std::nullopt;  // script PPtr (fileID + align + pathID)
        if (!r.ok(4 + 2)) return std::nullopt;
        const u32 typeId = r.u32v();
        r.skip(1);  // customType
        const u8 isPPtr = r.u8v();
        r.align4();
        // Slot width by binding KIND: only Transform bindings (typeID 4) are vectors
        // (t/s/euler = 3, quaternion = 4). Everything else (blend-shape weights, material
        // floats, isActive...) is ONE scalar curve — assuming 3 shifted every later
        // binding's firstSlot and bled values across tracks (creamy: the root's flight
        // translation landed on an antenna bone -> a spike through the mesh, S.).
        // PPtr curves live in a separate block and occupy no float slot at all.
        bd.dim = isPPtr ? 0 : (typeId == 4 ? (bd.attribute == 2 ? 4u : 3u) : 1u);
        bd.firstSlot = slot;
        slot += bd.dim;
        if (typeId != 4) bd.attribute = 0;  // never mistake a scalar for a TRS channel
        out.bindings.push_back(bd);
    }
    out.slotCount = slot;

    // Bake: sample every slot at the clip rate across [startTime, stopTime].
    out.duration = stopTime - startTime;
    out.frameCount = static_cast<u32>(std::lround(out.duration * out.sampleRate)) + 1;
    out.baked.assign(static_cast<usize>(out.frameCount) * out.slotCount, 0.0f);

    // Decode StreamedClip into per-curve key lists: frames {time f32, count u32,
    // keys {index u32, coeff f32[4]}}; value(t) = ((c0*x + c1)*x + c2)*x + c3, x = t - keyTime.
    struct Key {
        float time;
        float c[4];
    };
    std::map<u32, std::vector<Key>> keys;
    {
        LeReader s{stream, static_cast<usize>(streamWords) * 4};
        while (s.ok(8)) {
            const float t = s.f32v();
            const u32 cnt = s.u32v();
            if (cnt > streamCurves || !s.ok(static_cast<usize>(cnt) * 20)) break;
            for (u32 k = 0; k < cnt; ++k) {
                const u32 idx = s.u32v();
                Key key;
                key.time = t;
                for (float& c : key.c) c = s.f32v();
                if (idx < streamCurves) keys[idx].push_back(key);
            }
        }
    }
    auto sampleStream = [&](u32 curve, float t) -> float {
        auto it = keys.find(curve);
        if (it == keys.end() || it->second.empty()) return 0.0f;
        const auto& ks = it->second;
        // Before the curve's first key the cubic would extrapolate with a NEGATIVE x and
        // produce garbage/zero rotations (creamy: |q|=0 rows -> a bone spike, S.: "модель
        // сломана"). Unity holds the first key's value; c[3] is the value AT the key.
        if (t < ks[0].time) return ks[0].c[3];
        usize k = 0;
        while (k + 1 < ks.size() && ks[k + 1].time <= t) ++k;
        const float x = std::isfinite(ks[k].time) ? t - ks[k].time : 0.0f;
        const float* c = ks[k].c;
        return ((c[0] * x + c[1]) * x + c[2]) * x + c[3];
    };
    for (u32 f = 0; f < out.frameCount; ++f) {
        const float t = startTime + static_cast<float>(f) / out.sampleRate;
        float* row = &out.baked[static_cast<usize>(f) * out.slotCount];
        for (u32 c = 0; c < streamCurves && c < out.slotCount; ++c) row[c] = sampleStream(c, t);
        // Dense block: nearest sample (the clips are baked at the same rate anyway).
        if (denseCurves) {
            i32 df = static_cast<i32>(std::lround((t - denseBegin) * out.sampleRate));
            df = df < 0 ? 0 : (df >= static_cast<i32>(denseFrames) ? denseFrames - 1 : df);
            for (u32 c = 0; c < denseCurves; ++c) {
                const u32 slotIdx = streamCurves + c;
                if (slotIdx >= out.slotCount) break;
                float v;
                std::memcpy(&v, dense + (static_cast<usize>(df) * denseCurves + c) * 4, 4);
                row[slotIdx] = v;
            }
        }
        for (u32 c = 0; c < constCount; ++c) {
            const u32 slotIdx = streamCurves + denseCurves + c;
            if (slotIdx >= out.slotCount) break;
            float v;
            std::memcpy(&v, constData + static_cast<usize>(c) * 4, 4);
            row[slotIdx] = v;
        }
    }
    return out;
}

}  // namespace uaro
