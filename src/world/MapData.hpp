#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "formats/Gat.hpp"
#include "formats/Gnd.hpp"
#include "formats/Image.hpp"
#include "formats/Rsm.hpp"
#include "formats/Rsw.hpp"
#include "world/GroundMesh.hpp"

namespace uaro {

// Everything needed to render one map, loaded from the VFS: the world definition,
// ground geometry + textures, and the placed models (deduplicated by filename).
// CPU-side only — GPU upload happens in the renderer (v3+).
struct MapData {
    std::string name;
    Rsw rsw;
    Gnd gnd;
    std::optional<Gat> gat;
    GroundMesh ground;

    // Decoded GND textures, parallel to gnd.textures() (nullopt if missing/undecodable).
    std::vector<std::optional<Image>> textures;

    // Unique loaded models, and the placements that reference them by index (-1 = failed).
    std::vector<Rsm> rsmCache;
    std::vector<std::pair<RswModel, int>> placements;

    usize texturesLoaded() const {
        usize n = 0;
        for (const auto& t : textures)
            if (t && t->valid()) ++n;
        return n;
    }
    usize placementsResolved() const {
        usize n = 0;
        for (const auto& p : placements)
            if (p.second >= 0) ++n;
        return n;
    }
};

} // namespace uaro
