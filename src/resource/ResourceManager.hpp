#pragma once
// STUB — implemented in v1/v2.
//
// Caches decoded assets (textures, sprites, models, maps) keyed by virtual path,
// loading them through the Vfs on demand and sharing them across the scene.
#include "resource/Vfs.hpp"

namespace uaro {

class ResourceManager {
public:
    // Planned interface:
    //   explicit ResourceManager(Vfs& vfs);
    //   template <class T> std::shared_ptr<T> load(const std::string& vpath);
    //   void collectUnused();
};

} // namespace uaro
