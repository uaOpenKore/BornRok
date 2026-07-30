#pragma once
#include <array>
#include <string>

#include "core/Types.hpp"

namespace uaro {

// Content-source routing (feat/content-sources). The player picks, per content CATEGORY,
// which archive family supplies the assets:
//   GRO  — the official client archive (GRO.grf at the client root),
//   UARO — our content (UaRO.grf / UaRO.zip + everything listed in data.ini),
//   ROM  — converted Ragnarok-Origin content (RoM.zip).
// Fallback cascade when a file is missing: ROM -> UARO -> GRO, UARO -> GRO, GRO only.
// Loose data/ dirs always override everything (unchanged CM workflow).

enum class ContentCategory : u8 {
    Sfx = 0,   // data/wav/
    Bgm,       // bgm/
    Effects,   // data/sprite/이펙트|effect/, data/texture/effect/ (except .tga status icons)
    Statuses,  // data/texture/effect/*.tga (roBrowser StatusIcons convention)
    Skills,    // data/texture/유저인터페이스|userinterface/item/ (skill + item icons)
    Mobs,      // data/sprite/몬스터|monster/, data/sprite/homun/
    Npc,       // data/sprite/npc/
    Chars,     // data/sprite/인간족|human/, 악세사리|accessory/, 로브|robe/, 방패|shield/, data/palette/
    Other,     // everything else (maps, ui, tables, ...) — always the UARO chain
    Count
};
constexpr usize kContentCategories = static_cast<usize>(ContentCategory::Count);

enum class ContentSource : u8 {
    Gro = 0,  // GRO.grf only
    Uaro,     // UaRO archives, missing files fall back to GRO
    Rom,      // RoM.zip, missing files fall back to UaRO, then GRO
};

// Which category a (GrfArchive::normalize'd) virtual path belongs to. Both the Korean
// folder names (EUC-KR bytes) and their English aliases (GrfAlias) are recognised.
ContentCategory categorizeVpath(const std::string& vpath);

// The lookup order of archive families for a given source mode (highest first,
// terminated by the returned count; longest chain is 3).
usize sourceChain(ContentSource mode, std::array<ContentSource, 3>& out);

// Display / config names.
const char* contentCategoryName(ContentCategory c);   // "sfx", "bgm", ...
const char* contentSourceLabel(ContentSource s);      // "GRO", "UaRO", "ROeM"

}  // namespace uaro
