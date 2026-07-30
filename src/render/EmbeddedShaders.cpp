// Registry backing render/Shader.hpp's embedded_shader_bytes(). By itself this holds nothing, so
// the on-disk shaders under shaders/<profile>/ are used exactly as before. The build may also
// compile a generated translation unit (EmbedSpriteShaders.cmake -> bin2c) that calls
// register_embedded_shader() for the sprite shaders, so a single exe can render the patcher UI
// with no external shader files. Additive: no generated TU -> empty registry -> disk path.
#include <map>
#include <string>
#include <utility>

#include "core/Types.hpp"

namespace uaro {

// key = "<profile>/<name>" (e.g. "dx11/vs_sprite").
static std::map<std::string, std::pair<const unsigned char*, u32>>& embedded_registry() {
    static std::map<std::string, std::pair<const unsigned char*, u32>> reg;
    return reg;
}

void register_embedded_shader(const char* key, const unsigned char* data, u32 size) {
    embedded_registry()[key] = {data, size};
}

const unsigned char* embedded_shader_bytes(const std::string& profile, const std::string& name,
                                           u32& outSize) {
    auto it = embedded_registry().find(profile + "/" + name);
    if (it == embedded_registry().end()) {
        outSize = 0;
        return nullptr;
    }
    outSize = it->second.second;
    return it->second.first;
}

}  // namespace uaro
