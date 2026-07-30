#include "resource/ContentRoute.hpp"

namespace uaro {

namespace {

bool startsWith(const std::string& s, const char* pfx) {
    return s.rfind(pfx, 0) == 0;
}
bool endsWith(const std::string& s, const char* sfx) {
    const usize n = std::char_traits<char>::length(sfx);
    return s.size() >= n && s.compare(s.size() - n, n, sfx) == 0;
}

// EUC-KR folder names as stored in the GRF (see GrfAlias.hpp for the alias pairs).
constexpr const char* kMon = "data/sprite/\xb8\xf3\xbd\xba\xc5\xcd/";     // 몬스터
constexpr const char* kHuman = "data/sprite/\xc0\xce\xb0\xa3\xc1\xb7/";   // 인간족
constexpr const char* kAcc = "data/sprite/\xbe\xc7\xbc\xbc\xbb\xe7\xb8\xae/";  // 악세사리
constexpr const char* kRobe = "data/sprite/\xb7\xce\xba\xea/";            // 로브
constexpr const char* kShield = "data/sprite/\xb9\xe6\xc6\xd0/";          // 방패
constexpr const char* kFx = "data/sprite/\xc0\xcc\xc6\xd1\xc6\xae/";      // 이팩트
constexpr const char* kUiItem =
    "data/texture/\xc0\xaf\xc0\xfa\xc0\xce\xc5\xcd\xc6\xe4\xc0\xcc\xbd\xba/item/";  // 유저인터페이스/item

}  // namespace

ContentCategory categorizeVpath(const std::string& p) {
    if (startsWith(p, "bgm/")) return ContentCategory::Bgm;
    if (startsWith(p, "data/wav/")) return ContentCategory::Sfx;
    if (startsWith(p, kMon) || startsWith(p, "data/sprite/monster/") ||
        startsWith(p, "data/sprite/homun/"))
        return ContentCategory::Mobs;
    if (startsWith(p, "data/sprite/npc/")) return ContentCategory::Npc;
    if (startsWith(p, kHuman) || startsWith(p, "data/sprite/human/") || startsWith(p, kAcc) ||
        startsWith(p, "data/sprite/accessory/") || startsWith(p, kRobe) ||
        startsWith(p, "data/sprite/robe/") || startsWith(p, kShield) ||
        startsWith(p, "data/sprite/shield/") || startsWith(p, "data/palette/"))
        return ContentCategory::Chars;
    if (startsWith(p, kFx) || startsWith(p, "data/sprite/effect/"))
        return ContentCategory::Effects;
    if (startsWith(p, "data/texture/effect/"))
        // Status icons are the .tga files in the effect texture folder (roBrowser
        // StatusIcons); everything else there (.str + their .bmp/.tga-free textures) is FX.
        return endsWith(p, ".tga") ? ContentCategory::Statuses : ContentCategory::Effects;
    if (startsWith(p, kUiItem) || startsWith(p, "data/texture/userinterface/item/"))
        return ContentCategory::Skills;  // skill (and item) icons share this folder
    return ContentCategory::Other;
}

usize sourceChain(ContentSource mode, std::array<ContentSource, 3>& out) {
    switch (mode) {
        case ContentSource::Rom:
            out = {ContentSource::Rom, ContentSource::Uaro, ContentSource::Gro};
            return 3;
        case ContentSource::Uaro:
            out = {ContentSource::Uaro, ContentSource::Gro, ContentSource::Gro};
            return 2;
        case ContentSource::Gro:
        default:
            out = {ContentSource::Gro, ContentSource::Gro, ContentSource::Gro};
            return 1;
    }
}

const char* contentCategoryName(ContentCategory c) {
    switch (c) {
        case ContentCategory::Sfx: return "sfx";
        case ContentCategory::Bgm: return "bgm";
        case ContentCategory::Effects: return "effects";
        case ContentCategory::Statuses: return "statuses";
        case ContentCategory::Skills: return "skills";
        case ContentCategory::Mobs: return "mobs";
        case ContentCategory::Npc: return "npc";
        case ContentCategory::Chars: return "chars";
        default: return "other";
    }
}

const char* contentSourceLabel(ContentSource s) {
    switch (s) {
        case ContentSource::Gro: return "GRO";
        case ContentSource::Rom: return "ROeM";
        case ContentSource::Uaro:
        default: return "UaRO";
    }
}

}  // namespace uaro
