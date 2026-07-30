#pragma once
#include "core/Types.hpp"

namespace uaro {

// Headgear view id -> accessory sprite name (EUC-KR, leading '_'), from the
// client's compiled accessoryid/accname tables (AccessorySprites.cpp, generated).
// Returns nullptr for an unknown id (e.g. 0 = no headgear). The loader builds
// data/sprite/<accessory>/<sex>/<sex><name>.{spr,act}.
const char* accessoryName(u16 id);

} // namespace uaro
