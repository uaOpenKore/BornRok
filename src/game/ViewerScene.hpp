#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <unordered_map>

#include "app/Scene.hpp"
#include "render/Texture.hpp"
#include "game/CharacterActor.hpp"
#include "game/RomActor.hpp"
#include "game/RomBindings.hpp"
#include "ui/Widgets.hpp"

namespace uaro {

// --view: the content browser (S.). Categories (mobs / NPC now; items, effects, chars and
// dyes are planned) with two tabs each:
//   - "Сопоставленные": a scrollable list of RO sprites that resolve to a RoM bundle — each
//     row draws the 2D sprite (left) and the live 3D model (right) with their names.
//   - "Несопоставленные": RoM bundles no sprite maps to, 3D only, plus a sprite search box
//     (type letters/digits, click a name to preview) and a "Связать" button that appends the
//     chosen pair to viewer-pairs.txt next to the client for the dev to merge.
class ViewerScene : public Scene {
public:
    void onEnter(Application& app) override;
    void onExit(Application& app) override;
    void update(Application& app, double dt) override;
    void render(Application& app) override;

private:
    enum Cat { kCatMobs = 0, kCatNpcs = 1, kCatChars = 2, kCatDyes = 3, kCatItems = 4,
               kCatHats = 5, kCatEffects = 6, kCatCount };
    enum class Tab { Mapped, Unmapped };

    struct MappedRow {
        std::string sprite;  // RO sprite base name (lowercased)
        std::string bundle;  // resolved RoM bundle vpath (basename shown)
        u16 classId = 0;     // picks the sprite folder (monster vs npc)
    };

    void buildLists(Application& app);
    CharacterActor* spriteFor(Application& app, const std::string& name, int classId);
    RomActor* modelFor(Application& app, const std::string& name);
    RomActor* modelForBundle(Application& app, const std::string& bundleVpath);
    void savePair(Application& app, const std::string& bundle, const std::string& sprite);
    void removePair(Application& app, const std::string& sprite);  // unpair a mapped sprite (S.)

    double time_ = 0.0;
    Cat cat_ = kCatMobs;
    Tab tab_ = Tab::Mapped;
    float scroll_[kCatCount][2] = {};  // per category x {mapped, unmapped} list scroll (px)

    std::vector<MappedRow> mapped_[kCatCount];
    std::vector<std::string> unmappedRom_[kCatCount];  // bundle vpaths without a sprite
    usize spritesWithoutModel_[kCatCount] = {};        // counter for the header line
    std::vector<std::string> allSprites_;              // GRF sprite basenames (search pool)

    // Lazy per-name caches; loading is capped per frame so scrolling doesn't hitch.
    std::unordered_map<std::string, std::unique_ptr<CharacterActor>> spriteCache_;
    rombind::Cache romCache_;          // keyed by sprite name (shared loader with the game)
    rombind::Cache romBundleCache_;    // keyed by bundle vpath (unmapped tab)
    int loadsThisFrame_ = 0;

    // Unmapped tab state.
    int selUnmapped_ = -1;             // selected row in the unmapped list
    std::string selMappedSprite_;      // selected MAPPED row's sprite (for the Unpair button)
    bool viewAttack_ = false;          // mapped-tab preview: play the Attack clip instead of Idle (S.)
    ui::TextField search_;
    ui::TextField mapFilter_;          // list filter (substring) — mapped tab AND unmapped left list
    // Which text field on the unmapped tab has keyboard focus: 0 = the left-list filter (mapFilter_),
    // 1 = the right sprite-search box (search_). Click-to-focus so typing never lands in both.
    int unmappedFocus_ = 1;
    std::string searchDone_;           // last query the results were built for
    std::vector<usize> searchHits_;    // indices into allSprites_
    float searchScroll_ = 0.0f;
    int dragScroll_ = 0;               // active scrollbar id while the mouse is held (0 = none)

    // Chars category: fixed job list, sex/hair toggles apply to every row.
    std::vector<u16> charJobs_;
    std::unordered_map<int, std::string> dyePick_;  // dyes tab: row -> chosen skin name
    u8 viewSex_ = 1;                   // 1 = male, 0 = female
    int viewHair_ = 1;
    bool viewRiding_ = false;          // chars tab: show the job on a peco mount

    // Items: RoM icon atlases (item_1..62). Two-phase workflow (S.): SLICE the atlas into
    // icon squares, then BIND each square to a RO item id in a separate view. Squares persist
    // in viewer-item-rects.txt.
    struct ItemSquare { int page, x, y, w, h; std::string id; };  // page 1-based, atlas px
    std::vector<ItemSquare> squares_;
    void loadSquares();
    void saveSquares();
    int itemPage_ = 0;                 // 0-based atlas page
    int itemMode_ = 0;                 // 0 = slice, 1 = bind
    ui::TextField itemIdField_;        // RO item id (bind view)
    std::unordered_map<int, Texture> atlasCache_;
    Texture* loadAtlas(Application& app, int page);
    std::string itemStatus_;
    bool selHave_ = false;               // an editable box exists (slice view)
    float selL_ = 0, selT_ = 0, selR_ = 0, selB_ = 0;  // box in ATLAS pixels
    int editEdge_ = 0;                   // bitmask L=1 R=2 T=4 B=8 while dragging a handle
    float itemZoom_ = 1.0f, itemPanX_ = 0, itemPanY_ = 0;  // atlas zoom + pan
    float panPrevX_ = 0, panPrevY_ = 0;
    int bindSel_ = -1;                   // selected unbound square index in the bind view

    // Hats: RoM head models -> RO headgear view id (manual bind, S. #135). Preview = novice
    // wearing the model. Pairs to viewer-headgear-pairs.txt.
    std::vector<std::pair<std::string, std::string>> hats_;  // {dir, name}
    std::unordered_map<std::string, std::unique_ptr<RomActor>> hatCache_;
    RomActor* hatFor(Application& app, const std::string& dir, const std::string& name);
    ui::TextField hatSearch_;
    ui::TextField hatIdField_;
    std::vector<usize> hatHits_;
    std::string hatSearchDone_;
    float hatScroll_ = 0.0f;
    int hatSel_ = -1;
    std::string hatStatus_;

    // Effects tab (#132): bind an item id to a RoM effect name (rom-item-effects.txt), and unbind.
    std::vector<std::string> effectNames_;            // all effect bundle base names (searchable)
    std::vector<usize> effectHits_;                   // filtered indices into effectNames_
    ui::TextField effectSearch_;
    ui::TextField effectIdField_;
    std::string effectSearchDone_;
    float effectScroll_ = 0.0f;
    int effectSel_ = -1;                              // selected effect (index into effectNames_)
    std::string effectStatus_;
    std::vector<std::pair<std::string, std::string>> effectPairs_;  // {itemId, effect} from the file
    void loadEffectPairs();                           // (re)read rom-item-effects.txt
    void writeEffectPairs();                          // rewrite the file from effectPairs_

    std::string previewSprite_;        // clicked search result (2D preview)
    std::string status_;               // last save confirmation line
};

}  // namespace uaro
