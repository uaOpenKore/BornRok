#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

class Vfs;

// Item display names and icon-resource names, parsed from the classic client's GRF
// tables (data/idnum2itemdisplaynametable.txt and data/idnum2itemresnametable.txt).
// Lets the merchant shop show "Red Potion" + its inventory icon instead of a raw
// nameid. PACKETVER 7 has no itemInfo.lua, so these txt tables are the source.
class ItemDb {
public:
    void load(const Vfs& vfs);
    bool ready() const { return !names_.empty(); }

    // Display name ("Red Potion") for an item id, or "#<id>" if the id is unknown. A carded
    // equip gets its slot count appended RO-style ("Blade [3]"); 0-slot items get no suffix (S.).
    std::string name(u32 id) const;

    // Display name decorated with the inserted cards' prefixes, RO-style: e.g. a Knife with a
    // Vadon Card (prefix "Cold") + a Vital Card -> "Cold Vital Knife [3]". Prefix words come from
    // data/cardprefixnametable.txt (verified against uaRO.exe, which has NO Double/Triple multiplier
    // — this old client concatenates per-card in slot order). Falls back to plain name() when the item
    // has no prefix cards, is forged/named (card[0]=0x00FE/0x00FF encodes the smith, not a card), or
    // cards is null. The "[N]" slot suffix stays at the very end. Repeated identical prefixes collapse
    // to a Double/Triple/Quadruple multiplier (e.g. 4x Poring -> "Quadruple Lucky ..."). Postfix cards
    // are intentionally ignored (S.: "постфикс не нужен совсем"). (S.: карты дают префиксы.)
    std::string nameWithCards(u32 id, const u16 cards[4]) const;

    // Card-slot count for an item (data/itemslotcounttable.txt), or 0 if unslotted/unknown.
    int slots(u32 id) const;

    // VFS path of the item's small inventory-icon BMP
    // (data/texture/<유저인터페이스>/item/<resname>.bmp), or "" if the resname is
    // unknown. The bytes are magenta-keyed like the rest of RO's UI bitmaps.
    std::string iconPath(u32 id) const;

    // Path to the item's large COLLECTION illustration shown in the description popup
    // (data/texture/<유저인터페이스>/collection/<resname>.bmp), or "" if the resname is unknown.
    // Magenta-keyed like the icon. Cards have their own collection art under the same folder.
    std::string collectionPath(u32 id) const;
    // HD-pack fallback path (flat data/texture/collection/<resname>.bmp) tried after collectionPath().
    std::string collectionPathHd(u32 id) const;

    // Path to a CARD's illustration (data/texture/<유저인터페이스>/cardbmp/<resname>.bmp), or "" if the
    // resname is unknown. Cards use this folder, NOT collection/ (roBrowser CardIllustration).
    std::string cardImagePath(u32 id) const;

    // Path to a card's illustration derived from the DROPPING MONSTER's sprite (cardbmp/ill_<sprite>_card
    // .bmp), which is how the real files are named; "" if the card isn't in the mob_db-derived table.
    std::string cardIllustPath(u32 id) const;

    // Multi-line item description ("^000088An HP recovery item...") for an id, or "" if unknown.
    // Colour codes (^RRGGBB) are kept; the caller strips/uses them. Lines joined with '\n'.
    std::string description(u32 id) const;

    // All known item ids -> display names (for the --view2d "Итем" channel to enumerate the whole DB).
    const std::unordered_map<u32, std::string>& names() const { return names_; }

    // id -> icon/sprite resource name (EUC-KR). Used to background-warm every item's graphics
    // (icon + collection texture + dropped-item .spr/.act) into the VFS byte cache. (S.)
    const std::unordered_map<u32, std::string>& resNames() const { return res_; }

    // Pure parser for an "id#value#" table (CRLF-tolerant, '//' comment lines skipped).
    // Exposed for offline unit-testing. `underscoreToSpace` turns display-name "_" into
    // spaces (RO stores "Red_Potion"); leave false for the EUC-KR resource names.
    static std::unordered_map<u32, std::string> parseTable(const std::vector<u8>& bytes,
                                                           bool underscoreToSpace);
    // Pure parser for idnum2itemdesctable.txt: blocks of "<id>#\\n<lines...>\\n#". Exposed for tests.
    static std::unordered_map<u32, std::string> parseDescTable(const std::vector<u8>& bytes);

private:
    std::unordered_map<u32, std::string> names_;  // id -> display name (spaces)
    std::unordered_map<u32, std::string> res_;    // id -> icon/sprite resource name (EUC-KR)
    std::unordered_map<u32, std::string> desc_;   // id -> multi-line description (raw colour codes)
    std::unordered_map<u32, int> slots_;          // id -> card-slot count (itemslotcounttable.txt)
    std::unordered_map<u32, std::string> cardPrefix_;   // cardId -> prefix word (cardprefixnametable.txt)
};

} // namespace uaro
