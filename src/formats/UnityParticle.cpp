#include "formats/UnityParticle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "core/Log.hpp"

static const bool kPsTrace = std::getenv("PSTRACE") != nullptr;
#define PSLOG(r, ...)                       \
    do {                                    \
        if (kPsTrace) {                     \
            std::fprintf(stderr, "@%04zx ", (r).at); \
            std::fprintf(stderr, __VA_ARGS__);       \
            std::fprintf(stderr, "\n");     \
        }                                   \
    } while (0)

// Positional reader for Unity 2021.3 ParticleSystem (class 198) payloads. The field order and
// alignment come from uTinyRipper's ParticleSystem/InitialModule/EmissionModule/ShapeModule +
// MinMaxCurve/MinMaxGradient/AnimationCurveTpl/Gradient readers, specialised to the single Unity
// version RoM ships (2021.3.21f1) so no version branching is needed. Every module begins with an
// `enabled` bool + AlignStream(4); MinMaxCurve/MinMaxGradient align after their uint16 state word;
// an AnimationCurve aligns once after its keyframe array. See formats/UnityParticle.hpp.

namespace uaro {
namespace {

struct Rd {
    const u8* p;
    usize n;
    usize at = 0;
    bool bad = false;

    bool ok(usize need) const { return at + need <= n; }
    void need(usize k) {
        if (!ok(k)) bad = true;
    }
    u8 u8v() {
        need(1);
        return bad ? 0 : p[at++];
    }
    bool boolv() { return u8v() != 0; }
    u16 u16v() {
        need(2);
        if (bad) return 0;
        const u16 v = static_cast<u16>(p[at] | (p[at + 1] << 8));
        at += 2;
        return v;
    }
    i32 i32v() {
        need(4);
        if (bad) return 0;
        const i32 v = static_cast<i32>(p[at] | (p[at + 1] << 8) | (p[at + 2] << 16) |
                                       (static_cast<u32>(p[at + 3]) << 24));
        at += 4;
        return v;
    }
    float f32v() {
        const i32 b = i32v();
        float f;
        std::memcpy(&f, &b, 4);
        return f;
    }
    void align4() { at = (at + 3) & ~usize{3}; }
    void skip(usize k) {
        need(k);
        if (!bad) at += k;
    }
};

// AnimationCurveTpl<Float>: int32 keyCount, keyCount * Keyframe, align, preInfinity i32,
// postInfinity i32. A float keyframe on 2021.3 is 8 words (time,value,inSlope,outSlope,
// weightedMode i32, inWeight, outWeight) = 32 bytes. Returns the peak |value| across keys.
float readFloatCurve(Rd& r) {
    const i32 count = r.i32v();
    float peak = 0.0f;
    if (count < 0 || count > 4096) {
        r.bad = true;
        return 0.0f;
    }
    for (i32 i = 0; i < count && !r.bad; ++i) {
        r.f32v();                       // time
        const float val = r.f32v();     // value
        r.f32v();                       // inSlope
        r.f32v();                       // outSlope
        r.i32v();                       // weightedMode (2018.1+)
        r.f32v();                       // inWeight
        r.f32v();                       // outWeight
        if (std::fabs(val) > std::fabs(peak)) peak = val;
    }
    r.align4();
    r.i32v();  // preInfinity
    r.i32v();  // postInfinity
    r.i32v();  // rotationOrder (layout.HasRotationOrder true for 2021.3, even for float curves)
    return peak;
}

// MinMaxCurve (2021.3): state uint16 + align, scalar float, minScalar float, maxCurve, minCurve.
// Collapse to a [lo,hi] scalar range usable by the emitter. Curve modes fold to their peak * scalar.
UnityMinMax readMinMaxCurve(Rd& r) {
    UnityMinMax mm;
    const u16 state = r.u16v();
    r.align4();
    const float scalar = r.f32v();
    const float minScalar = r.f32v();
    readFloatCurve(r);  // maxCurve (consumed for alignment; keys are normalised 0..1)
    readFloatCurve(r);  // minCurve
    // The scalar/minScalar carry the magnitude in every mode (curves are normalised and multiply
    // the scalar); using them directly is robust even when the stored curve is empty (count 0),
    // which several RoM effects use for curve/twoCurves modes.
    switch (state) {
        case 0:  // constant
            mm.lo = mm.hi = scalar;
            break;
        case 1:  // single curve: 0..scalar
            mm.lo = 0.0f;
            mm.hi = scalar;
            break;
        default:  // 2 twoConstants / 3 twoCurves: bounded by the two scalars
            mm.lo = minScalar;
            mm.hi = scalar;
            break;
    }
    if (mm.hi < mm.lo) std::swap(mm.lo, mm.hi);
    return mm;
}

// Gradient (newer layout): 8 * ColorRGBAf (16 bytes each), 8 * uint16 ctime, 8 * uint16 atime,
// Mode int32, numColorKeys byte, numAlphaKeys byte, align. Returns key0 RGBA (the gradient start).
void readGradient(Rd& r, float outRgba[4]) {
    float first[4] = {1, 1, 1, 1};
    for (int k = 0; k < 8; ++k) {
        const float rr = r.f32v(), gg = r.f32v(), bb = r.f32v(), aa = r.f32v();
        if (k == 0) {
            first[0] = rr;
            first[1] = gg;
            first[2] = bb;
            first[3] = aa;
        }
    }
    for (int k = 0; k < 8; ++k) r.u16v();  // ctime
    for (int k = 0; k < 8; ++k) r.u16v();  // atime
    r.i32v();                              // mode
    r.u8v();                               // numColorKeys
    r.u8v();                               // numAlphaKeys
    r.align4();
    std::memcpy(outRgba, first, sizeof first);
}

// MinMaxGradient (2021.3, placement 3 = gradients after the colour pair): state uint16 + align,
// MinColor RGBAf, MaxColor RGBAf, MaxGradient, MinGradient. Resolves a single representative colour.
u16 readMinMaxGradientChecked(Rd& r, float outRgba[4], int placement) {
    float minColor[4] = {1, 1, 1, 1}, maxColor[4] = {1, 1, 1, 1};
    float maxGradStart[4] = {1, 1, 1, 1}, minGradStart[4] = {1, 1, 1, 1};
    const u16 state = r.u16v();
    r.align4();
    if (state > 3) return state;  // let the caller reject; don't consume garbage
    if (placement == 2) {
        readGradient(r, maxGradStart);
        readGradient(r, minGradStart);
    }
    for (int i = 0; i < 4; ++i) minColor[i] = r.f32v();
    for (int i = 0; i < 4; ++i) maxColor[i] = r.f32v();
    (void)minColor;  // TwoColors mode min side is unused by the emitter (we pick a single tint)
    if (placement == 3) {
        readGradient(r, maxGradStart);
        readGradient(r, minGradStart);
    }
    // state: 0 Color(use maxColor), 1 Gradient(maxGradient start), 2 TwoColors, 3 TwoGradients.
    const float* pick = maxColor;
    if (state == 1 || state == 3) pick = maxGradStart;
    std::memcpy(outRgba, pick, sizeof(float) * 4);
    return state;
}

void moduleHeader(Rd& r, bool* enabledOut = nullptr) {
    const bool en = r.boolv();
    r.align4();
    if (enabledOut) *enabledOut = en;
}

// Parse InitialModule at the current cursor into `out` and report whether it validates. The
// module's internal layout is fixed for 2021.3; only its *start offset* varies (the preamble's
// StartDelay curve length is data-dependent), so the caller scans offsets and keeps the first
// that passes these sanity gates. Field order per uTinyRipper InitialModule.Read.
bool tryInitialModule(Rd r, UnityParticleDesc& out) {
    moduleHeader(r);
    const UnityMinMax life = readMinMaxCurve(r);
    const UnityMinMax speed = readMinMaxCurve(r);
    float color[4];
    const u16 colorState = readMinMaxGradientChecked(r, color, out.gradientPlacement);
    if (colorState > 3) return false;
    const UnityMinMax size = readMinMaxCurve(r);
    if (r.bad) return false;
    // Sanity gates that disambiguate the real module from a coincidental offset. startSpeed can be
    // negative (particles pulled inward, e.g. Blessing), so gate on magnitude.
    if (!(life.hi >= 0.01f && life.hi <= 60.0f)) return false;
    if (!(std::fabs(speed.hi) <= 100.0f && std::fabs(speed.lo) <= 100.0f)) return false;
    if (!(size.hi >= 0.001f && size.hi <= 50.0f)) return false;

    // The reliable part (colour/size/lifetime/speed) has validated — accept the effect now. The
    // remaining fixed fields (maxParticles, gravity) are best-effort: read them on the same cursor,
    // but if that overruns keep sane defaults rather than rejecting an otherwise-good parse.
    out.startLifetime = life;
    out.startSpeed = speed;
    out.startSize = size;
    std::memcpy(out.startColor, color, sizeof color);
    out.maxParticles = 1000;
    out.gravityModifier = 0.0f;

    readMinMaxCurve(r);  // startSizeY
    readMinMaxCurve(r);  // startSizeZ
    readMinMaxCurve(r);  // startRotationX
    readMinMaxCurve(r);  // startRotationY
    readMinMaxCurve(r);  // startRotation
    r.f32v();            // randomizeRotationDirection
    const i32 maxP = r.i32v();
    r.boolv();  // size3D
    r.boolv();  // rotation3D
    r.align4();
    const UnityMinMax grav = readMinMaxCurve(r);  // gravityModifier
    if (!r.bad) {
        if (maxP > 0 && maxP < 100000) out.maxParticles = maxP;
        // Best-effort field: clamp so a drifted read can never send particles flying.
        if (std::isfinite(grav.hi)) out.gravityModifier = std::clamp(grav.hi, -3.0f, 3.0f);
    }
    return true;
}

}  // namespace

bool parseUnityParticleSystem(const u8* data, usize size, const std::string& unityVersion,
                              UnityParticleDesc& out) {
    (void)unityVersion;  // pinned to 2021.3 layout
    Rd r{data, size};
    // Component base PPtr (12) + LengthInSec + SimulationSpeed are at fixed offsets.
    r.i32v();
    r.skip(8);
    out.duration = r.f32v();
    out.simulationSpeed = r.f32v();
    // looping is the first bool after the fixed header block (StopAction..RingBufferLoopRange).
    // Its offset is fixed even though the rest of the preamble varies, so read it directly.
    if (size > 0x28) out.looping = data[0x28] != 0;

    // The preamble (StopAction..RandomSeed, incl. the variable-length StartDelay curve) has a
    // data-dependent length, so locate InitialModule by scanning for the offset whose fixed
    // internal layout validates. In practice it lands near 0x78; scan a generous window.
    for (usize at = 0x28; at + 4 < size && at < 0x400; at += 4) {
        Rd probe{data, size, at};
        if (tryInitialModule(probe, out)) {
            out.ok = true;
            PSLOG(r, "InitialModule found @%zx dur=%.2f", at, out.duration);
            return true;
        }
    }
    out.ok = false;
    return false;
}

void parseUnityParticleRenderer(const u8* data, usize size, const std::string& unityVersion,
                                UnityParticleDesc& out) {
    (void)data;
    (void)size;
    (void)unityVersion;
    out.renderMode = 0;  // billboard fallback (renderer base fields precede renderMode; TODO refine)
}

}  // namespace uaro
