#include "world/RomModel.hpp"

#include <cctype>
#include <cmath>
#include <cstring>
#include <tuple>
#include <unordered_map>

#include "core/Log.hpp"
#include "formats/UnityFs.hpp"
#include "formats/UnitySerialized.hpp"

namespace uaro {

bool appendRomBundle(RomModel& m, const std::vector<u8>& bundleBytes) {
    UnityFsBundle bundle;
    if (!bundle.parse(bundleBytes)) return false;

    // .resS resolver over the bundle's own nodes (textures + vertex data stream there).
    auto resRead = [&](const std::string& path, u64 off,
                       u32 size) -> std::optional<std::vector<u8>> {
        for (usize i = 0; i < bundle.nodes().size(); ++i)
            if (bundle.nodes()[i].path == path) {
                auto d = bundle.nodeData(i);
                if (!d || off + size > d->size()) return std::nullopt;
                return std::vector<u8>(d->begin() + static_cast<std::ptrdiff_t>(off),
                                       d->begin() + static_cast<std::ptrdiff_t>(off + size));
            }
        return std::nullopt;
    };

    for (usize ni = 0; ni < bundle.nodes().size(); ++ni) {
        if (!(bundle.nodes()[ni].flags & 4)) continue;  // SerializedFiles only
        auto data = bundle.nodeData(ni);
        UnitySerializedFile sf;
        if (!data || !sf.parse(*data)) continue;

        // Pre-pass: GameObject (1) names, keyed by pathId — the Transforms reference them.
        std::unordered_map<i64, std::string> goName;
        for (usize oi = 0; oi < sf.objects().size(); ++oi) {
            if (sf.objects()[oi].classId != 1) continue;
            auto od = sf.objectData(oi);
            if (!od || od->size() < 8) continue;
            u32 nComp;
            std::memcpy(&nComp, od->data(), 4);
            usize at = 4 + static_cast<usize>(nComp) * 12 + 4;  // components (PPtr each) + layer
            if (at + 4 > od->size()) continue;
            u32 nameLen;
            std::memcpy(&nameLen, od->data() + at, 4);
            at += 4;
            if (nameLen > 256 || at + nameLen > od->size()) continue;
            goName.emplace(sf.objects()[oi].pathId,
                           std::string(reinterpret_cast<const char*>(od->data() + at), nameLen));
        }

        for (usize oi = 0; oi < sf.objects().size(); ++oi) {
            const i32 cls = sf.objects()[oi].classId;
            if (cls != 43 && cls != 90 && cls != 28 && cls != 74 && cls != 21 && cls != 4)
                continue;
            auto od = sf.objectData(oi);
            if (!od) continue;
            switch (cls) {
                case 43:  // Mesh: keep the one with the RICHEST SKELETON (bones, then verts)
                          // — bundles carry accessory/effect meshes too and neither "first"
                          // nor "largest" is safe (dokebi: club 162v/1b before body 863v/25b;
                          // mimic: mouth EFFECT 728v/3b vs body 418v/6b — "largest" drew the
                          // translucent effect and the mimic went invisible, S.). Proper
                          // multi-mesh render is a follow-up (#126).
                    if (auto mesh = parseUnityMesh(*od, resRead)) {
                        const auto rank = [&m](const UnityMesh& x) {
                            std::string low = x.name;
                            for (char& ch : low)
                                ch = static_cast<char>(std::tolower(static_cast<u8>(ch)));
                            const bool exact = !m.wantName.empty() && low == m.wantName;
                            return std::make_tuple(exact, x.boneNameHashes.size(),
                                                   static_cast<usize>(x.vertexCount));
                        };
                        // Accessory meshes (dokebi's club: tiny rig, name prefixed by the
                        // body's) render alongside the body. Variant bodies (Poring_001c1:
                        // rig as rich as the main) and translucent "*effect*" meshes do not.
                        const auto keepAsExtra = [](const UnityMesh& x) {
                            std::string low = x.name;
                            for (char& ch : low)
                                ch = static_cast<char>(std::tolower(static_cast<u8>(ch)));
                            return x.vertexCount > 0 && !x.boneNameHashes.empty() &&
                                   x.boneNameHashes.size() <= 4 &&
                                   low.find("effect") == std::string::npos;
                        };
                        if (rank(*mesh) > rank(m.mesh)) {
                            if (keepAsExtra(m.mesh))
                                m.extras.push_back({std::move(m.mesh), {}});
                            m.mesh = std::move(*mesh);
                            m.name = m.mesh.name;
                        } else if (keepAsExtra(*mesh)) {
                            m.extras.push_back({std::move(*mesh), {}});
                        }
                    }
                    break;
                case 90:  // Avatar: the optimized-rig skeleton
                    if (m.skeleton.parents.empty())
                        if (auto av = parseUnityAvatar(*od)) m.skeleton = std::move(*av);
                    break;
                case 28:  // Texture2D
                    if (auto tex = parseUnityTexture2D(*od, resRead)) {
                        tex->pathId = sf.objects()[oi].pathId;
                        m.textures.push_back(std::move(*tex));
                    }
                    break;
                case 4: {  // Transform: named prefab attach points (CP_*/EP_* on players).
                    const std::vector<u8>& tb = *od;
                    if (tb.size() < 12 + 40) break;
                    i64 goId;
                    std::memcpy(&goId, tb.data() + 4, 8);  // m_GameObject PPtr {fileID, pathID}
                    auto nit = goName.find(goId);
                    if (nit == goName.end()) break;
                    RomAttachPoint ap;
                    ap.name = nit->second;
                    std::memcpy(ap.t, tb.data() + 12 + 16, 12);  // after localRotation (quat)
                    m.attachPoints.push_back(std::move(ap));
                    break;
                }
                case 21:  // Material: grab the _MainTex PPtr pathID (fileID is ignored: the
                          // texture lives in the _ext bundle and pathIDs don't collide there).
                    if (m.mainTexPathId == 0) {
                        const std::vector<u8>& mb = *od;
                        for (usize p = 4; p + 8 + 16 <= mb.size(); ++p) {
                            if (std::memcmp(mb.data() + p, "_MainTex", 8) != 0) continue;
                            u32 len;
                            std::memcpy(&len, mb.data() + p - 4, 4);
                            if (len != 8) continue;
                            const usize q = (p + 8 + 3) & ~usize{3};  // align, then PPtr
                            i64 pid;
                            std::memcpy(&pid, mb.data() + q + 4, 8);  // fileID i32, pathID i64
                            if (pid != 0) {
                                m.mainTexPathId = pid;
                                break;
                            }
                        }
                    }
                    break;
                case 74:  // AnimationClip
                    if (auto clip = parseUnityAnimClip(*od)) {
                        // Group the baked curve slots into per-bone TRS tracks by path hash.
                        RomClip rc;
                        rc.name = clip->name;
                        rc.sampleRate = clip->sampleRate;
                        rc.duration = clip->duration;
                        rc.frameCount = clip->frameCount;
                        std::unordered_map<u32, usize> trackByHash;
                        for (const auto& bd : clip->bindings) {
                            if (bd.attribute < 1 || bd.attribute > 3) continue;  // t/q/s only
                            auto [it, added] =
                                trackByHash.try_emplace(bd.pathHash, rc.tracks.size());
                            if (added) rc.tracks.emplace_back();
                            RomBoneTrack& tk = rc.tracks[it->second];
                            auto& dst = bd.attribute == 1 ? tk.t
                                        : bd.attribute == 2 ? tk.q
                                                            : tk.s;
                            dst.resize(static_cast<usize>(rc.frameCount) * bd.dim);
                            for (u32 f = 0; f < rc.frameCount; ++f)
                                for (u32 d2 = 0; d2 < bd.dim; ++d2)
                                    dst[static_cast<usize>(f) * bd.dim + d2] =
                                        clip->baked[static_cast<usize>(f) * clip->slotCount +
                                                    bd.firstSlot + d2];
                            // Rotation curves come out of the cubic evaluation NON-unit on
                            // sparse-key clips (spore walk: |q| 0.7..1.13) — a non-unit q in
                            // the TRS matrix acts as scale/shear and collapsed the mesh
                            // (S.: "споре когда ходит - исчезает"). Normalize per frame,
                            // like Unity does at sample time.
                            if (bd.attribute == 2 && bd.dim == 4)
                                for (u32 f = 0; f < rc.frameCount; ++f) {
                                    float* q = &dst[static_cast<usize>(f) * 4];
                                    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] +
                                                              q[2] * q[2] + q[3] * q[3]);
                                    if (n > 1e-6f)
                                        for (int k = 0; k < 4; ++k) q[k] /= n;
                                }
                            // Remember which hash this track belongs to via avatarNode below.
                            tk.avatarNode = -static_cast<i32>(bd.pathHash);  // temp: hash marker
                        }
                        m.clips.emplace(rc.name, std::move(rc));
                    }
                    break;
            }
        }
    }
    return true;
}

// Resolve hashes: mesh bone -> skeleton node, and clip tracks -> skeleton node. Call once
// after all bundles are appended (clip tracks carry the raw hash until then).
void finalizeRomModel(RomModel& m) {
    std::unordered_map<u32, i32> nodeByHash;
    for (usize i = 0; i < m.skeleton.nameHashes.size(); ++i)
        nodeByHash.emplace(m.skeleton.nameHashes[i], static_cast<i32>(i));
    m.meshBoneToNode.resize(m.mesh.boneNameHashes.size(), -1);
    usize matched = 0;
    for (usize i = 0; i < m.mesh.boneNameHashes.size(); ++i) {
        auto it = nodeByHash.find(m.mesh.boneNameHashes[i]);
        if (it != nodeByHash.end()) {
            m.meshBoneToNode[i] = it->second;
            ++matched;
        }
    }
    for (auto& ex : m.extras) {
        ex.boneToNode.assign(ex.mesh.boneNameHashes.size(), -1);
        for (usize i = 0; i < ex.mesh.boneNameHashes.size(); ++i) {
            auto it = nodeByHash.find(ex.mesh.boneNameHashes[i]);
            if (it != nodeByHash.end()) ex.boneToNode[i] = it->second;
        }
    }
    for (auto& [name, clip] : m.clips)
        for (RomBoneTrack& tk : clip.tracks) {
            const u32 hash = static_cast<u32>(-tk.avatarNode);
            auto it = nodeByHash.find(hash);
            tk.avatarNode = it != nodeByHash.end() ? it->second : -1;
        }
    log::info("RomModel '{}': {} verts, {} bones ({} matched), {} tex, {} clip(s)", m.name,
              m.mesh.vertexCount, m.mesh.boneNameHashes.size(), matched, m.textures.size(),
              m.clips.size());
}

std::optional<RomModel> loadRomModel(const std::vector<u8>& bundleBytes) {
    RomModel m;
    if (!appendRomBundle(m, bundleBytes)) return std::nullopt;
    if (m.mesh.vertexCount == 0 || m.skeleton.parents.empty()) return std::nullopt;
    finalizeRomModel(m);
    return m;
}

}  // namespace uaro
