#include "formats/Rsw.hpp"

#include <cstring>

#include "core/Log.hpp"
#include "core/io/ByteBuffer.hpp"

namespace uaro {

std::optional<Rsw> Rsw::parse(const std::vector<u8>& bytes) {
    if (bytes.size() < 6 + 40 * 4) {
        log::error("RSW: too small");
        return std::nullopt;
    }
    try {
        ByteReader r(bytes);
        char magic[4];
        r.read_bytes(magic, 4);
        if (std::memcmp(magic, "GRSW", 4) != 0) {
            log::error("RSW: bad signature");
            return std::nullopt;
        }
        const u8 major = r.u8v();
        const u8 minor = r.u8v();

        Rsw rsw;
        rsw.version_ = static_cast<u16>(major) * 0x100 + minor;
        const u16 v = rsw.version_;

        rsw.ini_ = r.read_cstring(40);
        rsw.gnd_ = r.read_cstring(40);
        rsw.gat_ = r.read_cstring(40);
        rsw.scr_ = r.read_cstring(40);

        if (v >= 0x103) {
            rsw.water_.level = r.f32le();
            rsw.water_.type = r.i32le();
            rsw.water_.waveHeight = r.f32le();
            rsw.water_.waveSpeed = r.f32le();
            rsw.water_.wavePitch = r.f32le();
            if (v >= 0x108) rsw.water_.animSpeed = r.i32le();
        }

        rsw.light_.longitude = r.i32le();
        rsw.light_.latitude = r.i32le();
        for (int i = 0; i < 3; ++i) rsw.light_.diffuse[i] = r.f32le();
        for (int i = 0; i < 3; ++i) rsw.light_.ambient[i] = r.f32le();
        if (v >= 0x107) rsw.light_.opacity = r.f32le();

        if (v >= 0x106)
            for (int i = 0; i < 4; ++i) rsw.groundBox_[i] = r.i32le();

        rsw.objectCount_ = r.i32le();
        if (rsw.objectCount_ < 0 || rsw.objectCount_ > 1'000'000) {
            log::error("RSW: implausible object count {}", rsw.objectCount_);
            return std::nullopt;
        }

        auto rd3 = [&r](f32* a) {
            a[0] = r.f32le();
            a[1] = r.f32le();
            a[2] = r.f32le();
        };

        // Parse EVERY object by type (layouts from roBrowser's World.js). The old
        // code stopped at the first non-model, which silently dropped every model
        // placed after it — e.g. ~371 of prontera's 1647 models (its castle flags and
        // trees) and most of a dungeon's props — since objects are not grouped
        // model-first. Lights and effects are captured too (for map atmosphere);
        // sounds are consumed but not stored.
        for (i32 o = 0; o < rsw.objectCount_; ++o) {
            const i32 type = r.i32le();
            switch (type) {
                case 1: {  // Model placement (.rsm)
                    RswModel m;
                    m.name = r.read_cstring(40);
                    m.animType = r.i32le();
                    m.animSpeed = r.f32le();
                    m.blockType = r.i32le();
                    m.filename = r.read_cstring(80);
                    m.nodeName = r.read_cstring(80);
                    rd3(m.pos);
                    rd3(m.rot);
                    rd3(m.scale);
                    rsw.models_.push_back(std::move(m));
                    break;
                }
                case 2: {  // Point light: name[80], pos[3]f, colour[3] FLOAT 0..1, range f
                    RswLightSource L;
                    r.read_cstring(80);
                    rd3(L.pos);
                    L.color[0] = r.f32le();  // RSW stores light colour as float 0..1 (not long),
                    L.color[1] = r.f32le();  // matching roBrowser Rsw.js — reading it as i32 gave
                    L.color[2] = r.f32le();  // float bit-patterns (0x3ECCCCCD == 0.4f) -> junk colours.
                    L.range = r.f32le();
                    rsw.lightSources_.push_back(L);
                    break;
                }
                case 3: {  // Sound: name[80], file[80], pos[3]f, vol f, w/h long, range f, cycle f(>=2.0)
                    r.read_cstring(80);
                    r.read_cstring(80);
                    r.skip(12);   // pos
                    r.f32le();    // vol
                    r.i32le();    // width
                    r.i32le();    // height
                    r.f32le();    // range
                    if (v >= 0x200) r.f32le();  // cycle
                    break;
                }
                case 4: {  // Effect: name[80], pos[3]f, id long, delay f, param[4]f
                    RswEffect e;
                    r.read_cstring(80);
                    rd3(e.pos);
                    e.id = r.i32le();
                    e.delay = r.f32le();
                    for (int i = 0; i < 4; ++i) e.param[i] = r.f32le();
                    rsw.effects_.push_back(e);
                    break;
                }
                default:
                    // Unknown type: its size is unknown, so stop to avoid a desync.
                    rsw.stoppedAtObject_ = o;
                    o = rsw.objectCount_;  // end the loop
                    break;
            }
        }

        rsw.trailing_ = r.remaining();
        return rsw;
    } catch (const std::out_of_range&) {
        log::error("RSW: truncated/corrupt file");
        return std::nullopt;
    }
}

} // namespace uaro
