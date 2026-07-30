#include "formats/UnitySkeleton.hpp"

#include <cstring>
#include <map>

namespace uaro {

namespace {

struct LeReader {
    const u8* p;
    usize n;
    usize at = 0;

    bool ok(usize need) const { return at + need <= n; }
    u32 u32v() {
        const u32 v = p[at] | (static_cast<u32>(p[at + 1]) << 8) |
                      (static_cast<u32>(p[at + 2]) << 16) | (static_cast<u32>(p[at + 3]) << 24);
        at += 4;
        return v;
    }
    i64 i64v() {
        u64 v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | p[at + i];
        at += 8;
        return static_cast<i64>(v);
    }
    float f32v() {
        float f;
        const u32 v = u32v();
        std::memcpy(&f, &v, 4);
        return f;
    }
    void align4() { at = (at + 3) & ~usize{3}; }
    bool skip(usize k) {
        if (!ok(k)) return false;
        at += k;
        return true;
    }
    // PPtr<T>: i32 fileID + (align) + i64 pathID (SerializedFile v14+ uses i64, 4-aligned).
    std::optional<i64> pptr() {
        align4();
        if (!ok(12)) return std::nullopt;
        u32v();  // fileID (0 = this file)
        return i64v();
    }
};

}  // namespace

std::optional<UnitySkeleton> buildUnitySkeleton(const UnitySerializedFile& sf) {
    struct Tr {
        i64 gameObject = 0;
        i64 father = 0;
        float rot[4], pos[3], scale[3];
    };
    std::map<i64, Tr> transforms;         // pathId -> data
    std::map<i64, std::string> goNames;   // GameObject pathId -> name

    for (usize i = 0; i < sf.objects().size(); ++i) {
        const auto& o = sf.objects()[i];
        if (o.classId == 4) {  // Transform
            auto d = sf.objectData(i);
            if (!d) continue;
            LeReader r{d->data(), d->size()};
            Tr t{};
            auto go = r.pptr();
            if (!go || !r.ok(4 * 10)) continue;
            t.gameObject = *go;
            for (float& f : t.rot) f = r.f32v();
            for (float& f : t.pos) f = r.f32v();
            for (float& f : t.scale) f = r.f32v();
            if (!r.ok(4)) continue;
            const u32 nCh = r.u32v();
            for (u32 c = 0; c < nCh; ++c)
                if (!r.pptr()) break;  // children (rebuilt from m_Father instead)
            auto father = r.pptr();
            t.father = father ? *father : 0;
            transforms[o.pathId] = t;
        } else if (o.classId == 1) {  // GameObject: components + layer + name
            auto d = sf.objectData(i);
            if (!d) continue;
            LeReader r{d->data(), d->size()};
            if (!r.ok(4)) continue;
            const u32 nComp = r.u32v();
            bool bad = false;
            for (u32 c = 0; c < nComp && !bad; ++c)
                if (!r.pptr()) bad = true;
            if (bad || !r.skip(4) || !r.ok(4)) continue;  // m_Layer
            const u32 len = r.u32v();
            if (len > 4096 || !r.ok(len)) continue;
            goNames[o.pathId] =
                std::string(reinterpret_cast<const char*>(r.p + r.at), len);
        }
    }
    if (transforms.empty()) return std::nullopt;

    // Emit parents-before-children: roots first, then BFS by father links.
    UnitySkeleton sk;
    std::map<i64, i32> indexOf;  // transform pathId -> bone index
    bool progress = true;
    while (progress && indexOf.size() < transforms.size()) {
        progress = false;
        for (const auto& [pid, t] : transforms) {
            if (indexOf.count(pid)) continue;
            const bool isRoot = t.father == 0 || !transforms.count(t.father);
            if (!isRoot && !indexOf.count(t.father)) continue;
            UnityBone b;
            auto nm = goNames.find(t.gameObject);
            b.name = nm != goNames.end() ? nm->second : "bone";
            b.parent = isRoot ? -1 : indexOf[t.father];
            std::memcpy(b.rot, t.rot, sizeof b.rot);
            std::memcpy(b.pos, t.pos, sizeof b.pos);
            std::memcpy(b.scale, t.scale, sizeof b.scale);
            b.transformPathId = pid;
            indexOf[pid] = static_cast<i32>(sk.bones.size());
            sk.bones.push_back(std::move(b));
            progress = true;
        }
    }
    return sk;
}

}  // namespace uaro
