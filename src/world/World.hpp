#pragma once
// STUB — implemented in v3 (map/world rendering).
//
// Holds the loaded map: ground (GND), walkability (GAT), world objects/lighting
// (RSW), placed models (RSM), and the entities on it. Owns the 3D camera and
// drives the map/model renderers built on render/.
#include "core/Types.hpp"

namespace uaro {

class World {
public:
    // Planned interface:
    //   bool loadMap(ResourceManager& res, const std::string& mapName);
    //   void update(double dt);
    //   void render(RenderDevice& dev);
};

} // namespace uaro
