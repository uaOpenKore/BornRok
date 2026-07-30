#include "formats/UnityTexture.hpp"

#include <cstring>

#include "core/Log.hpp"

namespace uaro {

namespace {

struct LeReader {
    const u8* p;
    usize n;
    usize at = 0;

    bool ok(usize need) const { return at + need <= n; }
    u8 u8v() { return p[at++]; }
    u32 u32v() {
        const u32 v = p[at] | (static_cast<u32>(p[at + 1]) << 8) |
                      (static_cast<u32>(p[at + 2]) << 16) | (static_cast<u32>(p[at + 3]) << 24);
        at += 4;
        return v;
    }
    i32 i32v() { return static_cast<i32>(u32v()); }
    u64 u64v() {
        u64 v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | p[at + i];
        at += 8;
        return v;
    }
    void align4() { at = (at + 3) & ~usize{3}; }
    std::optional<std::string> alignedString() {
        if (!ok(4)) return std::nullopt;
        const u32 len = u32v();
        if (len > 4096 || !ok(len)) return std::nullopt;
        std::string s(reinterpret_cast<const char*>(p + at), len);
        at += len;
        align4();
        return s;
    }
};

}  // namespace

std::optional<UnityTexture2D> parseUnityTexture2D(
    const std::vector<u8>& objectBytes,
    const std::function<std::optional<std::vector<u8>>(const std::string&, u64, u32)>& resRead) {
    LeReader r{objectBytes.data(), objectBytes.size()};
    UnityTexture2D t;
    auto name = r.alignedString();
    if (!name) return std::nullopt;
    t.name = *name;
    if (!r.ok(4 + 2 + 2 + 4 * 6)) return std::nullopt;
    r.i32v();   // m_ForcedFallbackFormat
    r.u8v();    // m_DownscaleFallback
    r.u8v();    // m_IsAlphaChannelOptional (2020.2+)
    r.align4();
    t.width = r.i32v();
    t.height = r.i32v();
    const u32 completeSize = r.u32v();
    r.i32v();   // m_MipsStripped (2020.1+)
    t.format = r.i32v();
    t.mipCount = r.i32v();
    if (!r.ok(4 + 4 + 4 * 2 + 4 * 6 + 4 * 2)) return std::nullopt;
    r.u8v();    // m_IsReadable
    r.u8v();    // m_IsPreProcessed (2020.1+)
    r.u8v();    // m_IgnoreMasterTextureLimit (2019.3+)
    r.u8v();    // m_StreamingMipmaps (2018.2+)
    r.align4();
    r.i32v();   // m_StreamingMipmapsPriority
    r.i32v();   // m_ImageCount
    r.i32v();   // m_TextureDimension
    r.i32v();   // GLTextureSettings.m_FilterMode
    r.i32v();   // m_Aniso
    r.u32v();   // m_MipBias (float)
    r.i32v();   // m_WrapU
    r.i32v();   // m_WrapV
    r.i32v();   // m_WrapW
    r.i32v();   // m_LightmapFormat
    r.i32v();   // m_ColorSpace
    // m_PlatformBlob (2020.2+): byte array.
    if (!r.ok(4)) return std::nullopt;
    const u32 blob = r.u32v();
    if (!r.ok(blob)) return std::nullopt;
    r.at += blob;
    r.align4();
    // image data: inline bytes, or empty + StreamingInfo into the .resS.
    if (!r.ok(4)) return std::nullopt;
    const u32 imgSize = r.u32v();
    if (imgSize > 0) {
        if (!r.ok(imgSize)) return std::nullopt;
        t.data.assign(r.p + r.at, r.p + r.at + imgSize);
        r.at += imgSize;
        return t;
    }
    r.align4();
    if (!r.ok(8 + 4 + 4)) return std::nullopt;
    const u64 offset = r.u64v();      // StreamingInfo.offset (u64 in 2020.1+)
    const u32 size = r.u32v();
    auto path = r.alignedString();   // "archive:/CAB-xxx/CAB-xxx.resS"
    if (!path || size == 0) return std::nullopt;
    // Strip the archive:/CAB-xxx/ prefix down to the node name.
    std::string p = *path;
    if (const auto slash = p.rfind('/'); slash != std::string::npos) p = p.substr(slash + 1);
    auto bytes = resRead(p, offset, size);
    if (!bytes) {
        log::warn("UnityTexture2D '{}': .resS read failed ({} @{} +{})", t.name, p,
                  static_cast<u64>(offset), size);
        return std::nullopt;
    }
    (void)completeSize;
    t.data = std::move(*bytes);
    return t;
}

}  // namespace uaro
