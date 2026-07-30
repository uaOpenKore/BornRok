#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "app/Scene.hpp"
#include "game/StrEffect.hpp"
#include "ui/Widgets.hpp"

#include <bgfx/bgfx.h>

namespace uaro {

class CharacterActor;  // 2D sprite preview for the Mobs/Npcs/Chars types (owned via unique_ptr)

// --view2d: a sprite-effect (.str) binding tool (S.). Two tabs like --view:
//   - "Привязанные": effects already bound (to an item-use or an object action) -> shows the
//     binding and an "Отвязать" button to remove a wrong pairing.
//   - "Непривязанные": effects with no binding yet -> pick one, enter an id, choose the target
//     (item / action) and "Привязать" it. Bindings persist to viewer-effect-bindings.txt next
//     to the client for the dev to merge. The right pane plays the selected effect on a loop.
class ViewerScene2D : public Scene {
public:
    ~ViewerScene2D() override;                 // out-of-line: unique_ptr<CharacterActor> is fwd-declared
    void onEnter(Application& app) override;
    void onExit(Application& app) override;
    void update(Application& app, double dt) override;
    void render(Application& app) override;

private:
    // Top-level content type (S.: view NPC/chars/mobs like the simple --view). Effects = the .str
    // binding tool (default); the others browse + 2D-preview the sprites of that kind.
    enum class Type { Effects, Mobs, Npcs, Chars };
    enum class Tab { Unbound, Bound };
    struct Bind {
        std::string effect;  // .str base name (lowercased)
        std::string kind;    // "item" or "action"
        int id = 0;          // item id or action/effect id it is bound to
    };

    void loadBindings();                       // (re)read viewer-effect-bindings.txt
    void saveBindings();                       // rewrite the file from bindings_
    bool isBound(const std::string& eff) const;
    void select(Application& app, const std::string& name);  // load + start the effect preview
    void renderSpriteBrowse(Application& app);  // Mobs/Npcs/Chars: list + 2D sprite preview (S.)

    double time_ = 0.0;
    Type type_ = Type::Effects;                // active content type (Effects / Mobs / Npcs / Chars)
    Tab tab_ = Tab::Unbound;

    // Sprite-browsing pools for the Mobs/Npcs/Chars types (mirrors --view). Enumerated in onEnter.
    std::vector<std::string> mobs_;            // monster sprite basenames (data/sprite/몬스터/)
    std::vector<std::string> npcs_;            // npc sprite basenames (data/sprite/npc/)
    std::vector<u16> charJobs_;                // job class ids for the Chars type
    std::unique_ptr<CharacterActor> spritePreview_;  // loaded 2D sprite for the current selection
    std::string spriteName_;                   // its name (so we don't reload every frame)

    std::vector<std::string> effects_;         // every effect .str base name (search pool)
    std::vector<std::pair<unsigned int, std::string>> items_;  // ItemDb id->name, sorted (Итем channel)
    // Effects the CLIENT already plays for a server effect-id (from effectFxStr) -- the "Привязанные" tab.
    std::vector<std::pair<unsigned short, std::string>> used_;  // {effect-id, .str name}
    std::unordered_map<std::string, unsigned short> usedByName_;  // name -> effect-id (fast isBound)
    std::unordered_map<unsigned short, std::string> usedById_;    // effect-id -> .str the client code plays
    std::vector<usize> hits_;                  // filtered indices into effects_ (unbound tab)
    std::string searchDone_;                   // last query the hits were built for
    ui::TextField search_;
    ui::TextField idField_;                    // id to bind to (unbound tab)
    ui::TextField bindField_;                  // .str name to attach to the selected server call (S.)
    int selId_ = -1;                           // id of the selected server call (per active channel)
    std::string selCodeFx_;                    // .str the client code already plays for the selection (read-only)
    int kind_ = 0;                             // 0 = item, 1 = action (new binding target)
    int boundCat_ = 0;                         // Bound-tab sub-tab: 0 action, 1 skill, 2 item, 3 other (S.)
    int focusField_ = 0;                       // which text box has focus: 0 = search, 1 = id
    float listScroll_ = 0.0f;

    std::string selName_;                      // selected effect .str (played in the preview)
    std::string selKey_;                        // unique per-ROW highlight key (label w/ id) so two rows
                                                // sharing a name (dfear = id 668 & 670) select separately (S.)
    std::unique_ptr<StrEffect> preview_;       // loaded .str/.ezv effect for the selected name
    std::unique_ptr<CharacterActor> effectSprPreview_;  // .spr effect (fireball/waterball) when no .str
    double previewStart_ = 0.0;                // time_ when the current loop started

    std::vector<Bind> bindings_;               // parsed from the file
    std::string status_;                       // last action confirmation line

    // Own sprite pass (mirrors GameScene::initSpritePass): the effect is a billboard on view 0.
    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle prog_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle bias_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fade_ = BGFX_INVALID_HANDLE;
};

}  // namespace uaro
