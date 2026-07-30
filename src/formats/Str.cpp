#include "formats/Str.hpp"

#include <cstring>
#include <exception>

#include "core/Log.hpp"
#include "core/io/ByteBuffer.hpp"

namespace uaro {

std::optional<Str> Str::parse(const std::vector<u8>& bytes) {
    try {
        ByteReader r(bytes);
        char magic[4] = {0, 0, 0, 0};
        r.read_bytes(magic, 4);
        if (std::memcmp(magic, "STRM", 4) != 0) {
            log::warn("Str: bad magic (not STRM)");
            return std::nullopt;
        }
        const u32 version = r.u32le();
        if (version != 0x94) {
            log::warn("Str: unsupported version 0x{:x}", version);
            return std::nullopt;
        }
        Str s;
        s.fps_ = r.u32le();
        s.maxKey_ = r.u32le();
        const u32 layerNum = r.u32le();
        r.skip(16);  // reserved (display/group/type/...)

        constexpr u32 kMaxLayers = 4096, kMaxTex = 4096, kMaxKeys = 1'000'000;
        if (layerNum > kMaxLayers) {
            log::warn("Str: implausible layer count {}", layerNum);
            return std::nullopt;
        }
        s.layers_.reserve(layerNum);
        for (u32 li = 0; li < layerNum; ++li) {
            StrLayer layer;
            const u32 texCnt = r.u32le();
            if (texCnt > kMaxTex) return std::nullopt;
            layer.textures.reserve(texCnt);
            for (u32 ti = 0; ti < texCnt; ++ti)
                layer.textures.push_back(r.read_cstring(128));  // fixed 128-byte name fields

            const u32 keyCnt = r.u32le();
            if (keyCnt > kMaxKeys) return std::nullopt;
            layer.keys.reserve(keyCnt);
            for (u32 ki = 0; ki < keyCnt; ++ki) {
                StrKeyframe k;
                k.frame = r.i32le();
                k.type = r.u32le();
                k.pos[0] = r.f32le();
                k.pos[1] = r.f32le();
                for (f32& u : k.uv) u = r.f32le();
                for (f32& x : k.xy) x = r.f32le();
                k.aniframe = r.f32le();
                k.anitype = r.u32le();
                k.delay = r.f32le();
                k.angle = r.f32le() / (1024.0f / 360.0f);  // 1024-per-revolution -> degrees
                for (f32& c : k.color) c = r.f32le() / 255.0f;  // 0..255 -> 0..1
                k.srcAlpha = r.u32le();
                k.destAlpha = r.u32le();
                r.u32le();  // mtpreset (multitexture preset) — read past, unused by the renderer
                layer.keys.push_back(k);
            }
            s.layers_.push_back(std::move(layer));
        }
        return s;
    } catch (const std::exception& e) {
        log::warn("Str: parse failed: {}", e.what());
        return std::nullopt;
    }
}

Str Str::build(u32 fps, u32 maxKey, std::vector<StrLayer> layers) {
    Str s;
    s.fps_ = fps;
    s.maxKey_ = maxKey;
    s.layers_ = std::move(layers);
    return s;
}

}  // namespace uaro
