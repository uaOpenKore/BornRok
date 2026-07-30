#pragma once
#include <optional>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// One ground cell: the height of its four corners and a terrain type that
// encodes walkability/snipeability.
struct GatCell {
    f32 h[4] = {0, 0, 0, 0};  // corner heights (BL, BR, TL, TR)
    u32 type = 0;

    // Walkability, matching the server (map_getcellp / CELL_CHKPASS): every type
    // is passable except 1 (wall) and 5 (cliff/gat-block). Keeps client paths in
    // step with the server's.
    bool walkable() const { return type != 1 && type != 5; }
};

// GAT (Ground Altitude Table): per-cell heightmap + walkability grid for a map.
class Gat {
public:
    static std::optional<Gat> parse(const std::vector<u8>& bytes);

    u32 width() const { return width_; }
    u32 height() const { return height_; }
    const std::vector<GatCell>& cells() const { return cells_; }
    const GatCell& at(u32 x, u32 y) const { return cells_[y * width_ + x]; }

private:
    u32 width_ = 0;
    u32 height_ = 0;
    std::vector<GatCell> cells_;
};

} // namespace uaro
