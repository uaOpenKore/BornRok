#include "resource/ItemDb.hpp"

#include <cstdlib>
#include <utility>
#include <vector>

#include "core/Log.hpp"
#include "resource/CardIllust.hpp"
#include "resource/Vfs.hpp"

namespace uaro {

namespace {
// "유저인터페이스" in CP949 (the UI texture folder), as stored in the GRF index.
const std::string kUiDir = "\xc0\xaf\xc0\xfa\xc0\xce\xc5\xcd\xc6\xe4\xc0\xcc\xbd\xba";
} // namespace

std::unordered_map<u32, std::string> ItemDb::parseTable(const std::vector<u8>& bytes,
                                                        bool underscoreToSpace) {
    std::unordered_map<u32, std::string> out;
    const std::string s(bytes.begin(), bytes.end());
    usize pos = 0;
    while (pos < s.size()) {
        const usize nl = s.find('\n', pos);
        usize beg = pos;
        usize end = (nl == std::string::npos) ? s.size() : nl;
        pos = (nl == std::string::npos) ? s.size() : nl + 1;
        while (end > beg && s[end - 1] == '\r') --end;  // trim trailing CR
        if (end - beg < 3) continue;                    // too short to hold "id#"
        if (s[beg] == '/' && s[beg + 1] == '/') continue;  // comment line

        usize i = beg;
        u32 id = 0;
        bool anyDigit = false;
        while (i < end && s[i] >= '0' && s[i] <= '9') {
            id = id * 10 + static_cast<u32>(s[i] - '0');
            ++i;
            anyDigit = true;
        }
        if (!anyDigit || i >= end || s[i] != '#') continue;
        ++i;  // past the first '#'
        const usize h2 = s.find('#', i);
        if (h2 == std::string::npos || h2 >= end) continue;
        std::string val = s.substr(i, h2 - i);
        if (underscoreToSpace)
            for (char& c : val)
                if (c == '_') c = ' ';
        out[id] = std::move(val);
    }
    return out;
}

std::unordered_map<u32, std::string> ItemDb::parseDescTable(const std::vector<u8>& bytes) {
    // idnum2itemdesctable.txt: repeated blocks of
    //   <id>#            (id alone on its line, followed by '#')
    //   <description line>*   (may carry ^RRGGBB colour codes)
    //   #                (a line that is just '#' ends the block)
    std::unordered_map<u32, std::string> out;
    const std::string s(bytes.begin(), bytes.end());
    usize pos = 0;
    bool inEntry = false;
    u32 curId = 0;
    std::string acc;
    while (pos < s.size()) {
        const usize nl = s.find('\n', pos);
        usize beg = pos, end = (nl == std::string::npos) ? s.size() : nl;
        pos = (nl == std::string::npos) ? s.size() : nl + 1;
        while (end > beg && s[end - 1] == '\r') --end;  // trim trailing CR
        const std::string line = s.substr(beg, end - beg);
        if (!inEntry) {
            if (line.size() >= 2 && line[0] == '/' && line[1] == '/') continue;  // comment
            usize i = 0;
            u32 id = 0;
            bool d = false;
            while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
                id = id * 10 + static_cast<u32>(line[i] - '0');
                ++i;
                d = true;
            }
            if (d && i < line.size() && line[i] == '#') {  // "<id>#" opens a block
                inEntry = true;
                curId = id;
                acc.clear();
            }
        } else if (line == "#") {  // closes the block
            out[curId] = acc;
            inEntry = false;
        } else {
            if (!acc.empty()) acc += '\n';
            acc += line;
        }
    }
    return out;
}

void ItemDb::load(const Vfs& vfs) {
    // Display NAMES: prefer the English table too (this GRF ships data/english/, data/french/,
    // data/german/… and the root table can be a non-English locale -> S. saw Italian/Spanish item
    // text). english/ first, root as the fallback. Resource (sprite) names are NOT localized, so they
    // stay on the root table.
    bool nameEng = true;
    auto nameBytes = vfs.read("data/english/idnum2itemdisplaynametable.txt");
    if (!nameBytes) { nameBytes = vfs.read("data/idnum2itemdisplaynametable.txt"); nameEng = false; }
    if (nameBytes) names_ = parseTable(*nameBytes, /*underscoreToSpace=*/true);
    if (auto b = vfs.read("data/idnum2itemresnametable.txt"))
        res_ = parseTable(*b, /*underscoreToSpace=*/false);
    // Descriptions: prefer the English table to match the (English) display names — this GRF's
    // default idnum2itemdesctable.txt is French (S.: "описание не на английском"). Fall back to the
    // default only if the english/ folder is absent.
    bool descEng = true;
    auto descBytes = vfs.read("data/english/idnum2itemdesctable.txt");
    if (!descBytes) { descBytes = vfs.read("data/idnum2itemdesctable.txt"); descEng = false; }
    if (descBytes) desc_ = parseDescTable(*descBytes);
    // Diagnostic (S.: "язык в описании итемов не английский"): show whether the ENGLISH table won or we
    // fell back to the (localised) root table. If descEng=0, this GRF has no data/english/ desc table.
    log::info("ItemDb: names from {} table, desc from {} table",
              nameEng ? "english" : "ROOT(localised)", descEng ? "english" : "ROOT(localised)");
    // Card-slot counts: "itemid#count#" table, same shape as the name table (S.: show "[N]" slots).
    if (auto b = vfs.read("data/itemslotcounttable.txt"))
        for (const auto& [id, val] : parseTable(*b, /*underscoreToSpace=*/false)) {
            const int n = std::atoi(val.c_str());
            if (n > 0) slots_[id] = n;
        }
    // Merge GRO.grf's tables so items present only in GRO (newer/custom, ~2300 ids that data.grf's
    // tables miss) still get a display name + icon resource -> their inventory/shop icons resolve
    // (S.: "GRO.grf - подтяни ещё иконки предметов"). readFrom targets ONLY the GRO source; emplace
    // keeps the primary (data.grf) entry on overlap and fills the gaps.
    auto mergeGro = [&](std::unordered_map<u32, std::string>& dst, const char* vpath, bool us) {
        if (auto b = vfs.readFrom(ContentSource::Gro, vpath))
            for (auto& kv : parseTable(*b, us)) dst.emplace(kv.first, kv.second);
    };
    mergeGro(names_, "data/idnum2itemdisplaynametable.txt", /*underscoreToSpace=*/true);
    mergeGro(res_, "data/idnum2itemresnametable.txt", /*underscoreToSpace=*/false);
    if (auto b = vfs.readFrom(ContentSource::Gro, "data/english/idnum2itemdesctable.txt"))
        for (auto& kv : parseDescTable(*b)) desc_.emplace(kv.first, kv.second);
    if (auto b = vfs.readFrom(ContentSource::Gro, "data/itemslotcounttable.txt"))
        for (auto& [id, val] : parseTable(*b, false)) {
            const int n = std::atoi(val.c_str());
            if (n > 0) slots_.emplace(id, n);
        }

    // Card prefixes (S.: inserted cards prefix the item name). "cardId#Prefix#" table, prefer the
    // english/ folder to match the english display names; fall back to the root, then merge GRO.
    if (auto b = vfs.read("data/english/cardprefixnametable.txt"))
        cardPrefix_ = parseTable(*b, /*underscoreToSpace=*/true);
    if (cardPrefix_.empty())
        if (auto b = vfs.read("data/cardprefixnametable.txt"))
            cardPrefix_ = parseTable(*b, /*underscoreToSpace=*/true);
    if (auto b = vfs.readFrom(ContentSource::Gro, "data/english/cardprefixnametable.txt"))
        for (auto& kv : parseTable(*b, true)) cardPrefix_.emplace(kv.first, kv.second);

    log::info("ItemDb: {} item names, {} icon resnames, {} descriptions, {} slotted, {} card prefixes",
              names_.size(), res_.size(), desc_.size(), slots_.size(), cardPrefix_.size());
}

std::string ItemDb::nameWithCards(u32 id, const u16 cards[4]) const {
    // Raw display name (WITHOUT the "[N]" slot suffix, which must stay at the very end).
    const auto it = names_.find(id);
    std::string raw = (it != names_.end() && !it->second.empty()) ? it->second : "#" + std::to_string(id);
    const int sl = slots(id);
    // Collect card PREFIX words in slot order, counting repeats of the SAME word so N identical cards
    // become a Double/Triple/Quadruple multiplier (S.: "должно быть Double/Triple"). Grouped by WORD
    // (not card id) so two different "Lucky" cards still read "Double Lucky". First-seen order kept.
    // (Postfix cards intentionally do NOTHING — S.: "постфикс не нужен совсем".)
    std::vector<std::pair<std::string, int>> pre;
    // card[0] == 0x00FE/0x00FF marks a forged/named item (the slots then encode the smith's char id,
    // element and star crumbs, NOT real cards) -> never treat those as prefix cards.
    if (cards && cards[0] != 0x00FE && cards[0] != 0x00FF) {
        for (int i = 0; i < 4; ++i) {
            const u16 c = cards[i];
            if (c == 0) continue;
            const auto pit = cardPrefix_.find(c);
            if (pit == cardPrefix_.end() || pit->second.empty()) continue;
            const std::string& w = pit->second;
            bool bumped = false;
            for (auto& p : pre)
                if (p.first == w) { ++p.second; bumped = true; break; }
            if (!bumped) pre.push_back({w, 1});
        }
    }
    auto mult = [](int n) -> const char* {
        return n == 2 ? "Double " : n == 3 ? "Triple " : n == 4 ? "Quadruple " : "";
    };
    std::string prefix;
    for (const auto& [w, n] : pre) {
        if (!prefix.empty()) prefix += ' ';
        prefix += mult(n);  // "" for a single card
        prefix += w;
    }
    std::string out;
    if (!prefix.empty()) out = prefix + " ";
    out += raw;
    if (sl > 0) out += " [" + std::to_string(sl) + "]";
    return out;
}

int ItemDb::slots(u32 id) const {
    const auto it = slots_.find(id);
    return (it != slots_.end()) ? it->second : 0;
}

std::string ItemDb::description(u32 id) const {
    const auto it = desc_.find(id);
    return (it != desc_.end()) ? it->second : std::string();
}

std::string ItemDb::name(u32 id) const {
    const auto it = names_.find(id);
    std::string nm = (it != names_.end() && !it->second.empty()) ? it->second : "#" + std::to_string(id);
    if (const int s = slots(id); s > 0) nm += " [" + std::to_string(s) + "]";  // RO slot suffix (S.)
    return nm;
}

std::string ItemDb::iconPath(u32 id) const {
    const auto it = res_.find(id);
    if (it == res_.end() || it->second.empty()) return {};
    return "data/texture/" + kUiDir + "/item/" + it->second + ".bmp";
}

std::string ItemDb::collectionPath(u32 id) const {
    const auto it = res_.find(id);
    if (it == res_.end() || it->second.empty()) return {};
    // CLASSIC path where the item-info collection art actually ships (uaro.zip:
    // data/texture/<유저인터페이스>/collection/<resname>.bmp — same folder as the item icon at .../item/,
    // which loads fine). S. 2026-07-27: "в инфо не грузятся картинки из uaro.zip" — the old flat
    // data/texture/collection/ path (an HD texture_x4 reroot) didn't match uaro.zip. That flat path is
    // now tried as a FALLBACK (collectionPathHd) so the HD pack still works.
    return "data/texture/" + kUiDir + "/collection/" + it->second + ".bmp";
}

std::string ItemDb::collectionPathHd(u32 id) const {
    const auto it = res_.find(id);
    if (it == res_.end() || it->second.empty()) return {};
    // The HD pack (texture_x4) reroots the collection art flat at data/texture/collection/<resname>.
    // Fallback after the classic <유저인터페이스>/collection/ path above.
    return "data/texture/collection/" + it->second + ".bmp";
}

std::string ItemDb::cardImagePath(u32 id) const {
    const auto it = res_.find(id);
    if (it == res_.end() || it->second.empty()) return {};
    // Card illustrations live under <유저인터페이스>/cardbmp/<resname>.bmp (roBrowser CardIllustration.js:
    // INTERFACE_PATH + 'cardbmp/' + illustResourcesName + '.bmp'); the pack ships 1168 as .webp there.
    return "data/texture/" + kUiDir + "/cardbmp/" + it->second + ".bmp";
}

std::string ItemDb::cardIllustPath(u32 id) const {
    // The real card illustration is named after the DROPPING MONSTER's sprite, NOT the item resname
    // (Swordfish Card 4089 -> ill_sword_fish_card, from mob SWORD_FISH). cardIllustSprite() gives the
    // base sprite from the mob_db-derived table; the file is cardbmp/ill_<sprite>_card.bmp.
    if (const char* spr = cardIllustSprite(id))
        return "data/texture/" + kUiDir + "/cardbmp/ill_" + spr + "_card.bmp";
    return {};
}

} // namespace uaro
