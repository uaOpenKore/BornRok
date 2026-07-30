#pragma once
#include <optional>
#include <string>

#include "resource/Vfs.hpp"
#include "world/MapData.hpp"

namespace uaro {

// Loads and assembles all CPU-side data for a map from the VFS:
//   data/<name>.rsw -> GND/GAT -> ground mesh -> textures (data/texture/...) ->
//   models (data/model/...). Ties the whole asset pipeline together.
class MapLoader {
public:
    static std::optional<MapData> load(Vfs& vfs, const std::string& mapName);
};

} // namespace uaro
