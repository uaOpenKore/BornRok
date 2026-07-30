#include "formats/Gat.hpp"

#include <cstring>

#include "core/Log.hpp"
#include "core/io/ByteBuffer.hpp"

namespace uaro {

std::optional<Gat> Gat::parse(const std::vector<u8>& bytes) {
    if (bytes.size() < 14) {
        log::error("GAT: too small");
        return std::nullopt;
    }
    try {
        ByteReader r(bytes);
        char magic[4];
        r.read_bytes(magic, 4);
        if (std::memcmp(magic, "GRAT", 4) != 0) {
            log::error("GAT: bad signature");
            return std::nullopt;
        }
        r.skip(2);  // version (major, minor)

        Gat gat;
        gat.width_ = r.u32le();
        gat.height_ = r.u32le();

        const u64 count = static_cast<u64>(gat.width_) * gat.height_;
        if (count == 0 || count > 4'000'000ull) {  // ~ guard against corrupt dims
            log::error("GAT: implausible dimensions {}x{}", gat.width_, gat.height_);
            return std::nullopt;
        }

        gat.cells_.resize(count);
        for (auto& c : gat.cells_) {
            c.h[0] = r.f32le();
            c.h[1] = r.f32le();
            c.h[2] = r.f32le();
            c.h[3] = r.f32le();
            c.type = r.u32le();
        }
        return gat;
    } catch (const std::out_of_range&) {
        log::error("GAT: truncated/corrupt file");
        return std::nullopt;
    }
}

} // namespace uaro
