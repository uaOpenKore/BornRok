#pragma once
#include <string>
#include <vector>

#include "app/Scene.hpp"
#include "core/Types.hpp"
#include "game/CharacterActor.hpp"
#include "net/Connection.hpp"
#include "net/Protocol.hpp"
#include "ui/Widgets.hpp"

namespace uaro {

// Connects to the char-server (address from the login reply), authenticates with
// CH_ENTER, shows the returned character list in the original win_select skin
// (three slots + a stats panel), and on confirm sends CH_SELECT_CHAR and reads
// HC_NOTIFY_ZONESVR before handing off to the in-game scene. Falls back to flat
// panels when the original UI bitmaps are unavailable.
class CharSelectScene : public Scene {
public:
    void onEnter(Application& app) override;
    void onExit(Application& app) override;
    void onResume(Application& app) override { onEnter(app); }
    void update(Application& app, double dt) override;
    void render(Application& app) override;

private:
    enum class Phase { Connecting, WaitList, Select, WaitZone, WaitDelete, WaitMake, Failed, Done };

    struct Layout {
        float W, H, wx, wy;          // win_select (576x342) origin
        float slotX[3], slotY, slotW, slotH;
        float leftArrowX, rightArrowX, arrowY;  // page rotation arrows
        float stX, stY, rowH;        // stats panel
        float okX, makeX, cancelX, deleteX, btnY;
        float winW, winH, btnW, btnH, txt;  // scaled window/button size + text scale (S.: half size)
    };
    Layout layout(Application& app) const;

    void pumpList(Application& app);
    void pumpZone(Application& app);
    void pumpDelete(Application& app);              // handle HC delete accept/refuse
    void renderDeleteDialog(Application& app);      // modal "delete <name>?" overlay
    void pumpMake(Application& app);                // handle HC make-char accept/refuse
    void renderCreateDialog(Application& app);      // modal "create character" overlay
    void openCreate(int absSlot);                   // open the create form for an absolute slot
    bool createValid() const;                       // stats satisfy the server's CZ_MAKE_CHAR rules
    int statPointsLeft() const;                     // 30 - sum(stats); OK enabled only when 0
    void selectSlot(Application& app, const net::CharInfo& c);
    int charAtSlot(int slot) const;  // index into chars_ for visible slot, or -1
    int maxPage() const;             // highest selectable page (slots 0..MAX_CHARS-1 / 3 cards)
    void restoreSelection(Application& app);  // highlight the last-used char (per-account) or the first
    void renderSkinned(Application& app, const Layout& L);
    void renderFlat(Application& app, const Layout& L);

    net::Connection conn_;
    bool sentEnter_ = false;
    bool aidConsumed_ = false;
    bool sentSelect_ = false;
    std::vector<net::CharInfo> chars_;
    std::vector<CharacterActor> actors_;  // parallel to chars_; composed sprites
    int selIdx_ = -1;  // index into chars_ of the highlighted character (-1 if the slot is empty)
    int selSlot_ = 0;  // currently highlighted SLOT (0..kMaxChars-1); arrow keys step this so the
                       // cursor visits empty slots too and never jumps over gaps between chars
    int page_ = 0;     // visible page of 3 slots (0..2 -> slots 0-2/3-5/6-8)
    bool confirmDelete_ = false;  // the delete-confirmation dialog is open
    int delIdx_ = -1;  // index into chars_ pending deletion (while dialog/WaitDelete)
    bool creating_ = false;       // the character-creation dialog is open
    ui::TextField makeName_;      // name input for the new character
    ui::TextField deleteEmail_;   // account-email input the server requires to delete a character
    int makeSlot_ = 0;            // target (absolute) slot for the new character
    // Starting stats for the new character, each 1..9. The server (make_new_char_sql)
    // requires sum == 30 and the paired sums str+int / agi+luk / vit+dex each <= 10,
    // so they default to a balanced 5/5/5/5/5/5 (sum 30, every pair 10 — always valid).
    u8 makeStats_[6] = {5, 5, 5, 5, 5, 5};  // order: str, agi, vit, int, dex, luk
    Phase phase_ = Phase::Connecting;
    std::string status_;
    double time_ = 0.0;
};

} // namespace uaro
