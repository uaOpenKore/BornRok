#include "game/CharSelectScene.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "game/GameScene.hpp"
#include "game/SceneAudio.hpp"
#include "net/JobNames.hpp"
#include "platform/FileSystem.hpp"
#include "core/Lang.hpp"
#include "ui/Widgets.hpp"

namespace uaro {

namespace {
constexpr float kWinW = 576, kWinH = 342;
constexpr float kBtnW = 42, kBtnH = 20;

// Per-account "last selected character" memory. PACKETVER 7 has no server-side
// notion of a default char, so we remember the last-entered charId in a small
// file keyed by the ACCOUNT ID (always set after login-accept and stable across
// reconnects, unlike the typed login string which may be empty on a re-entry).
// File: settings/acc<accountId>-lastchar.cfg. On the next char-list we highlight
// that character's slot instead of always the first. (S.)
std::string lastCharPath(u32 accountId) {
    if (accountId == 0) return {};
    return (std::filesystem::path(fs::data_dir()) / "settings" /
            ("acc" + std::to_string(accountId) + "-lastchar.cfg"))
        .string();
}
u32 readLastChar(u32 accountId) {
    const std::string path = lastCharPath(accountId);
    if (path.empty()) return 0;
    std::ifstream in(path);
    u32 id = 0;
    if (in) in >> id;
    return id;
}
void writeLastChar(u32 accountId, u32 charId) {
    const std::string path = lastCharPath(accountId);
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (out) out << charId << "\n";
    log::info("charselect: saved last char {} for account {}", charId, accountId);
}

// Centred delete-confirmation dialog. Shared by update() (hit-test) and render()
// (draw) so the Yes/No buttons line up. Yes = left, No = right.
struct DelDialog {
    float x, y, w, h, yesX, noX, btnY, bw, bh;
    float fieldX, fieldY, fieldW, fieldH;  // account-email input (this server requires it to delete)
};
DelDialog delDialog(float W, float H) {
    DelDialog d{};
    d.w = 280.0f;
    d.h = 150.0f;  // taller than a plain yes/no box to hold the required email input
    d.x = (W - d.w) * 0.5f;
    d.y = (H - d.h) * 0.5f;
    d.fieldX = d.x + 20.0f;
    d.fieldY = d.y + 64.0f;
    d.fieldW = d.w - 40.0f;
    d.fieldH = 22.0f;
    d.bw = 92.0f;
    d.bh = 28.0f;
    d.btnY = d.y + d.h - d.bh - 14.0f;
    d.yesX = d.x + d.w * 0.5f - d.bw - 8.0f;
    d.noX = d.x + d.w * 0.5f + 8.0f;
    return d;
}

// Character-creation dialog geometry: a centred box with a name field, a 6-row
// stat distributor (label + [-] value [+]), a remaining-points readout and
// Create/Cancel. Shared by update() (hit-test) and render() (draw) so they agree.
struct MakeDialog {
    float x, y, w, h;
    float fieldX, fieldY, fieldW, fieldH;       // name input
    float statX, statY, statRowH;               // first stat row top-left + row pitch
    float statLabelW, statBtnW, statValW;       // label / +/- button / value column widths
    float pointsY;                              // "Points left" line
    float makeX, cancelX, btnY, bw, bh;         // Create / Cancel buttons
    // Top-left of the [-]/[+] buttons and the value cell for stat row r (0..5).
    float minusX() const { return statX + statLabelW; }
    float valueX() const { return statX + statLabelW + statBtnW; }
    float plusX() const { return statX + statLabelW + statBtnW + statValW; }
    float rowY(int r) const { return statY + r * statRowH; }
};
MakeDialog makeDialog(float W, float H) {
    MakeDialog d{};
    d.w = 300.0f;
    d.h = 314.0f;  // taller than the old name-only box to hold the 6 stat rows
    d.x = (W - d.w) * 0.5f;
    d.y = (H - d.h) * 0.5f;
    d.fieldX = d.x + 16.0f;
    d.fieldY = d.y + 46.0f;
    d.fieldW = d.w - 32.0f;
    d.fieldH = 26.0f;
    d.statRowH = 24.0f;
    d.statX = d.x + 24.0f;
    d.statY = d.fieldY + d.fieldH + 18.0f;
    d.statLabelW = 44.0f;
    d.statBtnW = 24.0f;
    d.statValW = 40.0f;
    d.pointsY = d.rowY(6) + 6.0f;
    d.bw = 92.0f;
    d.bh = 28.0f;
    d.btnY = d.y + d.h - d.bh - 14.0f;
    d.makeX = d.x + d.w * 0.5f - d.bw - 8.0f;
    d.cancelX = d.x + d.w * 0.5f + 8.0f;
    return d;
}

constexpr const char* kStatLabels[6] = {"STR", "AGI", "VIT", "INT", "DEX", "LUK"};
} // namespace

void CharSelectScene::onEnter(Application& app) {
    auto& s = app.session();
    phase_ = Phase::Connecting;
    sentEnter_ = aidConsumed_ = sentSelect_ = false;
    selIdx_ = -1;
    page_ = 0;
    confirmDelete_ = false;
    delIdx_ = -1;
    creating_ = false;
    chars_.clear();
    app.audio().playBgm(app.vfs(), titleBgmPath(app.vfs()));  // keep the title theme on char-select (#103)
    for (auto& a : actors_) a.destroy();
    actors_.clear();
    status_ = "Connecting to char-server ...";
    if (s.charHost.empty() || s.charPort == 0) {
        phase_ = Phase::Failed;
        status_ = "No char-server address";
        return;
    }
    conn_.connect(s.charHost, s.charPort);
    log::info("charselect: connecting to {}:{}", s.charHost, s.charPort);
}

void CharSelectScene::onExit(Application&) {
    conn_.disconnect();
    for (auto& a : actors_) a.destroy();
    actors_.clear();
}

int CharSelectScene::charAtSlot(int slot) const {
    for (usize i = 0; i < chars_.size(); ++i)
        if (static_cast<int>(chars_[i].slot) == slot) return static_cast<int>(i);
    return -1;
}

CharSelectScene::Layout CharSelectScene::layout(Application& app) const {
    Layout L{};
    // Logical canvas = physical / UI-scale (#134), so the char-select window scales with the UI-scale
    // the same way the login screen + in-game HUD do (S.: "окно выбора чаров не скейлится, маленькое").
    const float uis = app.uiScale();
    L.W = static_cast<float>(app.render().width()) / uis;
    L.H = static_cast<float>(app.render().height()) / uis;
    // Draw the char-select window (backing + slots + buttons + text) at half the 576x342 skin size,
    // the same way the login/service windows shrink (S.: "чарселект так же уменьшить в 2 раза подложку
    // и кнопки"). Every window-relative offset multiplies by `s`, so hit-testing stays in sync.
    const float s = 1.0f;  // full size (S.: 0.5x made the char-select window too small -> back to 1x)
    L.winW = kWinW * s; L.winH = kWinH * s;
    L.btnW = kBtnW * s; L.btnH = kBtnH * s;
    L.txt = s;
    L.wx = (L.W - L.winW) * 0.5f;
    L.wy = (L.H - L.winH) * 0.5f;
    L.slotY = L.wy + 40 * s;
    L.slotW = 120 * s;
    L.slotH = 135 * s;
    L.slotX[0] = L.wx + 62 * s;
    L.slotX[1] = L.wx + 225 * s;
    L.slotX[2] = L.wx + 388 * s;
    L.leftArrowX = L.wx + 12 * s;
    L.rightArrowX = L.wx + L.winW - 54 * s;
    L.arrowY = L.slotY + L.slotH * 0.5f - L.btnH * 0.5f;
    L.stX = L.wx + 15 * s;
    L.stY = L.wy + 207 * s;
    L.rowH = 15 * s;
    L.btnY = L.wy + L.winH - 22 * s;  // sit lower in the window's bottom panel (S.: too high)
    L.cancelX = L.wx + 16 * s;
    L.makeX = L.wx + 62 * s;
    L.okX = L.wx + 108 * s;
    L.deleteX = L.wx + 154 * s;  // Delete button, after OK in the bottom button row
    return L;
}

void CharSelectScene::pumpList(Application& app) {
    auto& rx = conn_.rx();
    if (!aidConsumed_) {
        if (rx.size() < 4) return;
        conn_.consume(4);
        aidConsumed_ = true;
    }
    while (rx.size() >= 2) {
        const u16 id = net::peekId(rx);
        if (id == net::PKT_HC_ACCEPT_ENTER) {
            const usize c = net::decodeCharList(rx.data(), rx.size(), chars_);
            if (c == 0) break;
            conn_.consume(c);
            phase_ = Phase::Select;
            restoreSelection(app);  // highlight the last-used char (or the first)
            status_ = std::to_string(chars_.size()) + " character(s)";
            log::info("charselect: {} character(s) received", chars_.size());
            // Compose each character's sprite (body + head) from the GRF.
            const u8 sex = app.session().sex;
            actors_.clear();
            actors_.resize(chars_.size());
            for (usize i = 0; i < chars_.size(); ++i)
                actors_[i].load(app.vfs(), chars_[i].class_, sex, chars_[i].hair,
                                chars_[i].headBottom, chars_[i].headMid, chars_[i].headTop,
                                false, 0, 0, chars_[i].hairColor, chars_[i].clothesColor);
            return;
        }
        if (id == net::PKT_HC_REFUSE_ENTER) {
            u8 code = 0;
            const usize c = net::decodeRefuseEnter(rx.data(), rx.size(), code);
            if (c == 0) break;
            conn_.consume(c);
            phase_ = Phase::Failed;
            status_ = "Char-server rejected the connection";
            return;
        }
        if (id == net::PKT_SC_NOTIFY_BAN) {
            u8 code = 0;
            const usize c = net::decodeNotifyBan(rx.data(), rx.size(), code);
            if (c == 0) break;
            conn_.consume(c);
            phase_ = Phase::Failed;
            status_ = net::banErrorText(code);
            return;
        }
        log::warn("charselect: unexpected packet 0x{:04x}", id);
        conn_.consume(2);
    }
}

void CharSelectScene::pumpZone(Application& app) {
    auto& rx = conn_.rx();
    while (rx.size() >= 2) {
        const u16 id = net::peekId(rx);
        if (id == net::PKT_HC_NOTIFY_ZONESVR) {
            net::ZoneServer z;
            const usize c = net::decodeZoneServer(rx.data(), rx.size(), z);
            if (c == 0) break;
            conn_.consume(c);
            auto& s = app.session();
            s.charId = z.charId;
            s.mapName = z.mapName;
            s.mapHost = z.ip;
            s.mapPort = z.port;
            phase_ = Phase::Done;
            log::info("charselect: zone-server {}:{} map '{}' (char {})", z.ip, z.port, z.mapName,
                      z.charId);
            conn_.disconnect();
            app.scenes().push(app, std::make_unique<GameScene>());
            return;
        }
        if (id == net::PKT_HC_REFUSE_ENTER) {
            u8 code = 0;
            const usize c = net::decodeRefuseEnter(rx.data(), rx.size(), code);
            if (c == 0) break;
            conn_.consume(c);
            phase_ = Phase::Failed;
            status_ = "Character selection rejected";
            return;
        }
        if (id == net::PKT_SC_NOTIFY_BAN) {
            u8 code = 0;
            const usize c = net::decodeNotifyBan(rx.data(), rx.size(), code);
            if (c == 0) break;
            conn_.consume(c);
            phase_ = Phase::Failed;
            status_ = net::banErrorText(code);
            return;
        }
        log::warn("charselect: unexpected packet 0x{:04x} while entering", id);
        conn_.consume(2);
    }
}

void CharSelectScene::pumpDelete(Application& app) {
    auto& rx = conn_.rx();
    while (rx.size() >= 2) {
        const u16 id = net::peekId(rx);
        if (id == net::PKT_HC_DELETE_ACCEPT) {
            const usize c = net::decodeDeleteAccept(rx.data(), rx.size());
            if (c == 0) break;
            conn_.consume(c);
            // Drop the deleted character locally and rebuild the composed sprites
            // (actors_ is parallel to chars_), as pumpList does.
            if (delIdx_ >= 0 && delIdx_ < static_cast<int>(chars_.size()))
                chars_.erase(chars_.begin() + delIdx_);
            for (auto& a : actors_) a.destroy();
            actors_.clear();
            actors_.resize(chars_.size());
            const u8 sex = app.session().sex;
            for (usize i = 0; i < chars_.size(); ++i)
                actors_[i].load(app.vfs(), chars_[i].class_, sex, chars_[i].hair,
                                chars_[i].headBottom, chars_[i].headMid, chars_[i].headTop,
                                false, 0, 0, chars_[i].hairColor, chars_[i].clothesColor);
            restoreSelection(app);  // re-highlight the last-used char after the list shrank
            delIdx_ = -1;
            phase_ = Phase::Select;
            status_ = "Character deleted";
            return;
        }
        if (id == net::PKT_HC_DELETE_REFUSE) {
            u8 reason = 0;
            const usize c = net::decodeDeleteRefuse(rx.data(), rx.size(), reason);
            if (c == 0) break;
            conn_.consume(c);
            delIdx_ = -1;
            phase_ = Phase::Select;
            status_ = "Deletion refused (incorrect email)";
            return;
        }
        log::warn("charselect: unexpected packet 0x{:04x} during delete", id);
        conn_.consume(2);
    }
}

void CharSelectScene::restoreSelection(Application& app) {
    if (chars_.empty()) {
        selIdx_ = -1;
        selSlot_ = 0;
        page_ = 0;
        return;
    }
    int idx = 0;  // default to the first character
    const u32 last = readLastChar(app.session().accountId);
    if (last != 0)
        for (usize i = 0; i < chars_.size(); ++i)
            if (chars_[i].charId == last) {
                idx = static_cast<int>(i);
                break;
            }
    selIdx_ = idx;
    selSlot_ = chars_[idx].slot;
    page_ = selSlot_ / 3;
}

void CharSelectScene::selectSlot(Application& app, const net::CharInfo& c) {
    writeLastChar(app.session().accountId, c.charId);  // remember this char as the account default (S.)
    app.session().charName = c.name;
    app.session().charClass = c.class_;
    app.session().charHair = c.hair;
    app.session().charHairColor = c.hairColor;  // hair dye (#86)
    app.session().charClothColor = c.clothesColor;  // clothes dye (#86)
    app.session().charWeapon = c.weapon;
    app.session().charShield = c.shield;
    app.session().charHeadBottom = c.headBottom;
    app.session().charHeadMid = c.headMid;
    app.session().charHeadTop = c.headTop;
    app.session().baseLevel = c.baseLevel;
    app.session().jobLevel = static_cast<u16>(c.jobLevel);
    app.session().baseExp = c.baseExp;  // seed exp/zeny from char-select: the load-end burst sends
    app.session().jobExp = c.jobExp;     // only NEXT-exp, so the bars + zeny would read 0 otherwise
    app.session().zeny = c.zeny;
    conn_.send(net::buildCharSelect(static_cast<u8>(c.slot)));
    sentSelect_ = true;
    phase_ = Phase::WaitZone;
    status_ = "Entering game as " + c.name + " ...";
    log::info("charselect: selecting slot {} ({})", c.slot, c.name);
}

void CharSelectScene::update(Application& app, double dt) {
    time_ += dt;
    InputState in = app.input();  // logical-space copy so UI hit-tests match the scaled canvas (#134)
    if (app.uiScale() > 1.001f) {
        in.mouseX = static_cast<int>(in.mouseX / app.uiScale());
        in.mouseY = static_cast<int>(in.mouseY / app.uiScale());
    }
    const Layout L = layout(app);

    // Gamepad on-screen keyboard for the two modal text fields (character name / delete e-mail). The
    // injected keystrokes alias through `in` into the makeName_ / deleteEmail_ updates in the modal
    // blocks below; Start cancels either modal. Off in the slot-select view (d-pad navigates there).
    if (app.gamepadMode() && (creating_ || confirmDelete_)) {
        ui::osk().enabled = ui::osk().active = true;
        ui::oskInput(app.inputMutable());
        if (in.pad.start) {
            creating_ = false;
            confirmDelete_ = false;
            delIdx_ = -1;
            ui::osk().active = false;
            return;
        }
    } else {
        ui::osk().enabled = ui::osk().active = false;
    }

    // Modal delete-confirmation dialog swallows all other input until resolved.
    if (confirmDelete_) {
        const DelDialog d = delDialog(L.W, L.H);
        const bool valid = delIdx_ >= 0 && delIdx_ < static_cast<int>(chars_.size());
        // Type the account e-mail; Enter in the field confirms (S.: "для удаления нужен ввод
        // емейла"). The server checks it against the account and refuses on mismatch (0x70).
        const bool enter = deleteEmail_.update(in, /*focused=*/true);
        const bool yes = valid && (enter ||
                                   (in.mousePressed && in.hit(d.yesX, d.btnY, d.bw, d.bh)));
        const bool no = in.escape || in.closeRequested ||
                        (in.mousePressed && in.hit(d.noX, d.btnY, d.bw, d.bh));
        if (yes) {
            conn_.send(net::buildCharDelete(chars_[delIdx_].charId, deleteEmail_.value));
            status_ = "Deleting " + chars_[delIdx_].name + " ...";
            phase_ = Phase::WaitDelete;
            confirmDelete_ = false;  // fall through to the WaitDelete pump below
        } else {
            if (no || !valid) {
                confirmDelete_ = false;
                delIdx_ = -1;
            }
            return;
        }
    } else if (creating_) {
        // Modal character-creation dialog: type a name, distribute the six starting stats
        // with the [-]/[+] buttons (each 1..9, sum fixed at 30, paired sums <= 10), then
        // Create (Enter / button, only when the stats are valid) or Cancel (Esc / button).
        const MakeDialog d = makeDialog(L.W, L.H);
        const bool enter = makeName_.update(in, /*focused=*/true);
        // Stat +/- buttons. "+" is blocked when it would break a server rule (stat > 9,
        // points overspent, or a paired sum > 10); "-" is blocked below 1.
        if (in.mousePressed) {
            for (int r = 0; r < 6; ++r) {
                if (in.hit(d.minusX(), d.rowY(r), d.statBtnW, d.statBtnW) && makeStats_[r] > 1) {
                    --makeStats_[r];
                } else if (in.hit(d.plusX(), d.rowY(r), d.statBtnW, d.statBtnW) &&
                           makeStats_[r] < 9 && statPointsLeft() > 0) {
                    const int pair[6] = {3, 5, 4, 0, 2, 1};  // str<->int, agi<->luk, vit<->dex
                    if (makeStats_[r] + 1 + makeStats_[pair[r]] <= 10) ++makeStats_[r];
                }
            }
        }
        const bool cancel = in.escape || in.closeRequested ||
                            (in.mousePressed && in.hit(d.cancelX, d.btnY, d.bw, d.bh));
        const bool make = enter || (in.mousePressed && in.hit(d.makeX, d.btnY, d.bw, d.bh));
        if (cancel) {
            creating_ = false;
        } else if (make && !makeName_.value.empty() && createValid()) {
            // The six distributed stats (already constrained to the server's rules) + default
            // hair; the server replies HC_ACCEPT_MAKECHAR (0x6d) or HC_REFUSE_MAKECHAR (0x6e).
            conn_.send(net::buildMakeChar(makeName_.value, makeStats_[0], makeStats_[1],
                                          makeStats_[2], makeStats_[3], makeStats_[4], makeStats_[5],
                                          static_cast<u8>(makeSlot_), /*hairStyle=*/1,
                                          /*hairColor=*/0));
            status_ = "Creating " + makeName_.value + " ...";
            creating_ = false;
            phase_ = Phase::WaitMake;
        }
    } else {
        // ESC / window-close = back to login (same as Cancel).
        if (in.escape || in.closeRequested) {
            conn_.disconnect();
            app.scenes().requestPop();
            return;
        }
        // Enter = OK: enter the game as the highlighted character.
        if (in.keyEnter && phase_ == Phase::Select && selIdx_ >= 0 &&
            selIdx_ < static_cast<int>(chars_.size())) {
            selectSlot(app, chars_[selIdx_]);
            return;
        }
        // Del = open the delete-confirmation dialog for the highlighted character.
        if (in.keyDelete && phase_ == Phase::Select && selIdx_ >= 0 &&
            selIdx_ < static_cast<int>(chars_.size())) {
            confirmDelete_ = true;
            delIdx_ = selIdx_;
            deleteEmail_.value.clear();
            deleteEmail_.caret = 0;
            return;
        }
        // Left/Right arrow keys step the highlight one SLOT (not one existing character), so the
        // cursor visits empty slots too and never jumps over the gap between non-adjacent chars
        // (S.: "со 2 слота прыгает сразу на 9"). The page follows the slot so it stays on-screen.
        if (phase_ == Phase::Select && (in.keyLeft || in.keyRight)) {
            selSlot_ = in.keyRight ? (selSlot_ + 1) % net::kMaxChars
                                   : (selSlot_ + net::kMaxChars - 1) % net::kMaxChars;
            page_ = selSlot_ / 3;
            selIdx_ = charAtSlot(selSlot_);  // -1 when the slot is empty
        }
        // Gamepad: d-pad L/R walks slots, A enters the game (or creates on an empty slot), X deletes,
        // B / Start backs out to login. Mirrors the keyboard/mouse paths above.
        if (app.gamepadMode() && phase_ == Phase::Select) {
            if (in.pad.dpadRight || in.pad.dpadLeft) {
                selSlot_ = in.pad.dpadRight ? (selSlot_ + 1) % net::kMaxChars
                                            : (selSlot_ + net::kMaxChars - 1) % net::kMaxChars;
                page_ = selSlot_ / 3;
                selIdx_ = charAtSlot(selSlot_);
            }
            const bool have = selIdx_ >= 0 && selIdx_ < static_cast<int>(chars_.size());
            if (in.pad.south) {
                if (have) { selectSlot(app, chars_[selIdx_]); return; }
                openCreate(selSlot_);  // empty slot -> creation dialog
            }
            if (in.pad.west && have) {
                confirmDelete_ = true;
                delIdx_ = selIdx_;
                deleteEmail_.value.clear();
                deleteEmail_.caret = 0;
            }
            if (in.pad.east || in.pad.start) {
                conn_.disconnect();
                app.scenes().requestPop();
                return;
            }
        }

        if (in.mousePressed) {
            if (in.hit(L.cancelX, L.btnY, L.btnW, L.btnH)) {  // Cancel -> back to login
                conn_.disconnect();
                app.scenes().requestPop();
                return;
            }
            if (phase_ == Phase::Select) {
                // Page paging arrows: step one page, clamped to 0..maxPage (3 pages of 3).
                if (in.hit(L.leftArrowX, L.arrowY, L.btnW, L.btnH) && page_ > 0) --page_;
                if (in.hit(L.rightArrowX, L.arrowY, L.btnW, L.btnH) && page_ < maxPage()) ++page_;
                for (int v = 0; v < 3; ++v) {
                    if (in.hit(L.slotX[v], L.slotY, L.slotW, L.slotH)) {
                        const int absSlot = page_ * 3 + v;  // visible card -> absolute slot
                        selSlot_ = absSlot;                 // clicking a slot moves the cursor there
                        const int idx = charAtSlot(absSlot);
                        if (idx >= 0) {
                            selIdx_ = idx;
                            // Double-click a character = enter the game as them.
                            if (in.mouseDoubleClick) {
                                selectSlot(app, chars_[idx]);
                                return;
                            }
                        } else {
                            selIdx_ = -1;
                            // Click an empty card -> create a character in that exact slot.
                            openCreate(absSlot);
                        }
                    }
                }
                if (in.hit(L.okX, L.btnY, L.btnW, L.btnH) && selIdx_ >= 0 &&
                    selIdx_ < static_cast<int>(chars_.size()))
                    selectSlot(app, chars_[selIdx_]);
                // Delete button: open the confirm dialog for the highlighted character (same as
                // the Del key — S.: "нету кнопки удалить чара").
                if (in.hit(L.deleteX, L.btnY, L.btnW, L.btnH) && selIdx_ >= 0 &&
                    selIdx_ < static_cast<int>(chars_.size())) {
                    confirmDelete_ = true;
                    delIdx_ = selIdx_;
                    deleteEmail_.value.clear();
                    deleteEmail_.caret = 0;
                }
                // The "New" toolbar button creates in the first empty card of this page,
                // falling back to the first empty slot overall.
                if (in.hit(L.makeX, L.btnY, L.btnW, L.btnH)) {
                    int slot = -1;
                    for (int v = 0; v < 3 && slot < 0; ++v)
                        if (charAtSlot(page_ * 3 + v) < 0) slot = page_ * 3 + v;
                    for (int s = 0; s < net::kMaxChars && slot < 0; ++s)
                        if (charAtSlot(s) < 0) slot = s;
                    if (slot >= 0) openCreate(slot);
                }
            }
        }
    }

    conn_.update();
    if (phase_ == Phase::Connecting) {
        if (conn_.connected() && !sentEnter_) {
            auto& s = app.session();
            conn_.send(net::buildCHEnter(s.accountId, s.loginId1, s.loginId2, s.sex,
                                         static_cast<u16>(s.clientType)));
            sentEnter_ = true;
            phase_ = Phase::WaitList;
            status_ = "Loading characters ...";
        } else if (conn_.closed()) {
            phase_ = Phase::Failed;
            status_ = "Cannot reach the char-server";
        }
    } else if (phase_ == Phase::WaitList) {
        pumpList(app);
        if (phase_ == Phase::WaitList && conn_.closed()) {
            phase_ = Phase::Failed;
            status_ = "Char-server closed the connection";
        }
    } else if (phase_ == Phase::WaitZone) {
        pumpZone(app);
        if (phase_ == Phase::WaitZone && conn_.closed()) {
            phase_ = Phase::Failed;
            status_ = "Disconnected while entering game";
        }
    } else if (phase_ == Phase::WaitDelete) {
        pumpDelete(app);
        if (phase_ == Phase::WaitDelete && conn_.closed()) {
            phase_ = Phase::Failed;
            status_ = "Disconnected during deletion";
        }
    } else if (phase_ == Phase::WaitMake) {
        pumpMake(app);
        if (phase_ == Phase::WaitMake && conn_.closed()) {
            phase_ = Phase::Failed;
            status_ = "Disconnected during character creation";
        }
    }
}

void CharSelectScene::renderSkinned(Application& app, const Layout& L) {
    SpriteBatch& sb = app.sprites();
    Font& font = app.font();
    ui::UiSkin& skin = app.uiSkin();
    InputState in = app.input();  // logical-space copy so UI hit-tests match the scaled canvas (#134)
    if (app.uiScale() > 1.001f) {
        in.mouseX = static_cast<int>(in.mouseX / app.uiScale());
        in.mouseY = static_cast<int>(in.mouseY / app.uiScale());
    }
    auto img = [&](const char* n) { return skin.get(n); };

    // Background image removed (S.: "убрать фоновую картинку"); render() draws the dark-grey fill.
    if (const ui::UiImage* w = img("win_select.bmp"))
        ui::imageScaled(sb, *w, L.wx, L.wy, L.winW, L.winH);

    // Character slots (3 visible of the current page).
    for (int v = 0; v < 3; ++v) {
        const int absSlot = page_ * 3 + v;
        const float x = L.slotX[v], y = L.slotY;
        // Highlight the selected SLOT even when it is empty, so arrow-key navigation onto an
        // empty slot is visible (the cursor can land there to create a character).
        if (absSlot == selSlot_) ui::border(sb, x, y, L.slotW, L.slotH, ui::color::kBorderFocus, 2);
        const int idx = charAtSlot(absSlot);
        if (idx < 0) continue;
        const auto& c = chars_[idx];
        // Composed character sprite (body + head), feet near the slot bottom.
        if (static_cast<usize>(idx) < actors_.size() && actors_[idx].ready())
            actors_[idx].renderScaled(sb, x + L.slotW * 0.5f, y + L.slotH - 24 * L.txt, L.txt, time_);
        const std::string nm = c.name.empty() ? "(unnamed)" : c.name;
        font.draw(sb, x + (L.slotW - font.width(nm, L.txt)) * 0.5f, y + L.slotH - 28 * L.txt, L.txt,
                  ui::color::kSkinText, nm);
        const std::string lv = "Lv " + std::to_string(c.baseLevel);
        font.draw(sb, x + (L.slotW - font.width(lv, L.txt)) * 0.5f, y + L.slotH - 16 * L.txt, L.txt,
                  ui::color::kSkinText, lv);
    }

    // Page rotation arrows.
    ui::imageButtonScaled(sb, in, L.leftArrowX, L.arrowY, L.btnW, L.btnH, img("btn_back.bmp"), nullptr, nullptr);
    ui::imageButtonScaled(sb, in, L.rightArrowX, L.arrowY, L.btnW, L.btnH, img("btn_next.bmp"), nullptr, nullptr);

    // Stats panel for the highlighted character (values overlaid on baked labels).
    if (selIdx_ >= 0 && selIdx_ < static_cast<int>(chars_.size())) {
        const auto& c = chars_[selIdx_];
        const u32 col = ui::color::kSkinText;
        const float vx = L.stX + 52 * L.txt, sx = L.stX + 190 * L.txt, ts = L.txt;
        auto row = [&](int r) { return L.stY + r * L.rowH; };
        font.draw(sb, vx, row(0), ts, col, c.name);
        font.draw(sb, vx, row(1), ts, col, net::jobName(c.class_));  // name, not raw class id
        font.draw(sb, vx, row(2), ts, col, std::to_string(c.baseLevel));
        font.draw(sb, vx, row(3), ts, col, std::to_string(c.baseExp));
        font.draw(sb, vx, row(4), ts, col, std::to_string(c.hp) + "/" + std::to_string(c.maxHp));
        font.draw(sb, vx, row(5), ts, col, std::to_string(c.sp) + "/" + std::to_string(c.maxSp));
        font.draw(sb, sx, row(0), ts, col, std::to_string(c.str));
        font.draw(sb, sx, row(1), ts, col, std::to_string(c.agi));
        font.draw(sb, sx, row(2), ts, col, std::to_string(c.vit));
        font.draw(sb, sx, row(3), ts, col, std::to_string(c.int_));
        font.draw(sb, sx, row(4), ts, col, std::to_string(c.dex));
        font.draw(sb, sx, row(5), ts, col, std::to_string(c.luk));
    }

    ui::imageButtonScaled(sb, in, L.okX, L.btnY, L.btnW, L.btnH, img("btn_ok.bmp"), img("btn_ok_a.bmp"),
                          img("btn_ok_b.bmp"));
    ui::imageButtonScaled(sb, in, L.makeX, L.btnY, L.btnW, L.btnH, img("btn_make.bmp"), nullptr, nullptr);
    ui::imageButtonScaled(sb, in, L.cancelX, L.btnY, L.btnW, L.btnH, img("btn_cancel.bmp"), nullptr, nullptr);
    // No delete-button skin image ships in the GRF, so draw a text button (S.: "нету кнопки
    // удалить чара"). Clicking it opens the same confirm dialog as the Del key.
    ui::button(sb, font, in, L.deleteX, L.btnY, L.btnW, L.btnH, tr("charselect.del"), L.txt);

    if (!status_.empty()) {
        const u32 col = (phase_ == Phase::Failed) ? ui::color::kError : ui::color::kWhite;
        font.draw(sb, L.wx, L.wy - 18, 1.0f, col, status_);
    }
}

void CharSelectScene::renderFlat(Application& app, const Layout& L) {
    SpriteBatch& sb = app.sprites();
    Font& font = app.font();
    InputState in = app.input();  // logical-space copy so UI hit-tests match the scaled canvas (#134)
    if (app.uiScale() > 1.001f) {
        in.mouseX = static_cast<int>(in.mouseX / app.uiScale());
        in.mouseY = static_cast<int>(in.mouseY / app.uiScale());
    }

    ui::panel(sb, L.wx, L.wy, L.winW, L.winH, ui::color::kWinBody);
    ui::border(sb, L.wx, L.wy, L.winW, L.winH, ui::color::kWinBorder);
    const float titleS = 2.0f * L.txt;
    font.draw(sb, L.wx + (L.winW - font.width(tr("charselect.title"), titleS)) * 0.5f, L.wy + 10 * L.txt,
              titleS, ui::color::kWinText, tr("charselect.title"));

    for (int v = 0; v < 3; ++v) {
        const float x = L.slotX[v], y = L.slotY;
        const int idx = charAtSlot(page_ * 3 + v);
        ui::panel(sb, x, y, L.slotW, L.slotH,
                  idx == selIdx_ ? ui::color::kWinSelect : ui::color::kWinContent);
        ui::border(sb, x, y, L.slotW, L.slotH, ui::color::kWinBorder);
        if (static_cast<usize>(idx) < actors_.size() && idx >= 0 && actors_[idx].ready())
            actors_[idx].renderScaled(sb, x + L.slotW * 0.5f, y + L.slotH - 24 * L.txt, L.txt, time_);
        if (idx >= 0) {
            const auto& c = chars_[idx];
            font.draw(sb, x + 8 * L.txt, y + L.slotH - 30 * L.txt, L.txt, ui::color::kWinText,
                      c.name.empty() ? "(unnamed)" : c.name);
            font.draw(sb, x + 8 * L.txt, y + L.slotH - 16 * L.txt, L.txt, ui::color::kWinTextDim,
                      "Lv " + std::to_string(c.baseLevel));
        }
    }
    ui::button(sb, font, in, L.leftArrowX, L.arrowY, L.btnW, L.btnH, "<", L.txt);
    ui::button(sb, font, in, L.rightArrowX, L.arrowY, L.btnW, L.btnH, ">", L.txt);
    ui::button(sb, font, in, L.okX, L.btnY, L.btnW, L.btnH, tr("common.ok"), L.txt);
    ui::button(sb, font, in, L.makeX, L.btnY, L.btnW, L.btnH, tr("charselect.new"), L.txt);
    ui::button(sb, font, in, L.cancelX, L.btnY, L.btnW, L.btnH, tr("menu.back"), L.txt);
    ui::button(sb, font, in, L.deleteX, L.btnY, L.btnW, L.btnH, tr("charselect.del"), L.txt);
    if (!status_.empty()) {
        const u32 col = (phase_ == Phase::Failed) ? ui::color::kError : ui::color::kWinTextDim;
        font.draw(sb, L.wx, L.wy - 18, 1.5f, col, status_);
    }
}

void CharSelectScene::renderDeleteDialog(Application& app) {
    SpriteBatch& sb = app.sprites();
    Font& font = app.font();
    InputState in = app.input();  // logical-space copy so UI hit-tests match the scaled canvas (#134)
    if (app.uiScale() > 1.001f) {
        in.mouseX = static_cast<int>(in.mouseX / app.uiScale());
        in.mouseY = static_cast<int>(in.mouseY / app.uiScale());
    }
    const float W = static_cast<float>(app.render().width()) / app.uiScale();   // logical canvas (#134)
    const float H = static_cast<float>(app.render().height()) / app.uiScale();
    const DelDialog d = delDialog(W, H);

    sb.draw(0.0f, 0.0f, W, H, ui::rgba(0, 0, 0, 130));  // dim the screen behind
    ui::panel(sb, d.x, d.y, d.w, d.h, ui::color::kWinBody);
    ui::border(sb, d.x, d.y, d.w, d.h, ui::color::kWinBorder);

    const std::string nm = (delIdx_ >= 0 && delIdx_ < static_cast<int>(chars_.size()))
                               ? chars_[delIdx_].name
                               : std::string("?");
    const std::string q = "Delete \"" + nm + "\"?";
    font.draw(sb, d.x + (d.w - font.width(q, 1.5f)) * 0.5f, d.y + 22, 1.5f, ui::color::kWinText, q);
    const char* warn = "Enter your account e-mail to confirm:";
    font.draw(sb, d.x + (d.w - font.width(warn, 1.0f)) * 0.5f, d.y + 46, 1.0f, ui::color::kWinTextDim,
              warn);
    deleteEmail_.draw(sb, font, d.fieldX, d.fieldY, d.fieldW, d.fieldH, /*focused=*/true, time_,
                      "e-mail");
    ui::button(sb, font, in, d.yesX, d.btnY, d.bw, d.bh, tr("common.delete"), 1.5f);
    ui::button(sb, font, in, d.noX, d.btnY, d.bw, d.bh, tr("common.cancel"), 1.5f);
}

int CharSelectScene::maxPage() const {
    // RO allows MAX_CHARS (9) slots = 3 pages of 3 cards (page 0:0-2, 1:3-5, 2:6-8).
    return (net::kMaxChars - 1) / 3;  // 2
}

void CharSelectScene::openCreate(int absSlot) {
    creating_ = true;  // open the character-creation dialog for this absolute slot
    makeName_.value.clear();
    makeName_.caret = 0;
    makeName_.asciiOnly = true;  // character name: English only, no Cyrillic/other scripts (S.)
    makeName_.alnumOnly = true;  // stricter: ONLY [A-Za-z0-9] — block every non-English letter/hieroglyph (S.)
    makeSlot_ = absSlot;
    for (auto& s : makeStats_) s = 5;  // reset to the balanced, always-valid default
}

int CharSelectScene::statPointsLeft() const {
    int sum = 0;
    for (u8 s : makeStats_) sum += s;
    return 30 - sum;  // server requires the six stats to total exactly 30
}

bool CharSelectScene::createValid() const {
    // Mirror make_new_char_sql: each stat 1..9, sum == 30, paired sums <= 10.
    for (u8 s : makeStats_)
        if (s < 1 || s > 9) return false;
    if (statPointsLeft() != 0) return false;
    return (makeStats_[0] + makeStats_[3]) <= 10 &&   // str+int
           (makeStats_[1] + makeStats_[5]) <= 10 &&   // agi+luk
           (makeStats_[2] + makeStats_[4]) <= 10;     // vit+dex
}

void CharSelectScene::pumpMake(Application& app) {
    auto& rx = conn_.rx();
    while (rx.size() >= 2) {
        const u16 id = net::peekId(rx);
        if (id == net::PKT_HC_ACCEPT_MAKECHAR) {
            net::CharInfo nc;
            const usize c = net::decodeMakeCharAccept(rx.data(), rx.size(), nc);
            if (c == 0) break;
            conn_.consume(c);
            chars_.push_back(nc);
            // Rebuild the parallel composed sprites (actors_ stays aligned with chars_).
            for (auto& a : actors_) a.destroy();
            actors_.clear();
            actors_.resize(chars_.size());
            const u8 sex = app.session().sex;
            for (usize i = 0; i < chars_.size(); ++i)
                actors_[i].load(app.vfs(), chars_[i].class_, sex, chars_[i].hair,
                                chars_[i].headBottom, chars_[i].headMid, chars_[i].headTop,
                                false, 0, 0, chars_[i].hairColor, chars_[i].clothesColor);
            selIdx_ = static_cast<int>(chars_.size()) - 1;
            page_ = chars_.back().slot / 3;
            phase_ = Phase::Select;
            status_ = "Created " + nc.name;
            return;
        }
        if (id == net::PKT_HC_REFUSE_MAKECHAR) {
            u8 code = 0;
            const usize c = net::decodeMakeCharRefuse(rx.data(), rx.size(), code);
            if (c == 0) break;
            conn_.consume(c);
            phase_ = Phase::Select;
            status_ = (code == 0x00)   ? "Name already taken"
                      : (code == 0x01) ? "Creation denied (underaged)"
                                       : "Character creation denied";
            return;
        }
        log::warn("charselect: unexpected packet 0x{:04x} during make", id);
        conn_.consume(2);
    }
}

void CharSelectScene::renderCreateDialog(Application& app) {
    SpriteBatch& sb = app.sprites();
    Font& font = app.font();
    InputState in = app.input();  // logical-space copy so UI hit-tests match the scaled canvas (#134)
    if (app.uiScale() > 1.001f) {
        in.mouseX = static_cast<int>(in.mouseX / app.uiScale());
        in.mouseY = static_cast<int>(in.mouseY / app.uiScale());
    }
    const float W = static_cast<float>(app.render().width()) / app.uiScale();   // logical canvas (#134)
    const float H = static_cast<float>(app.render().height()) / app.uiScale();
    const MakeDialog d = makeDialog(W, H);

    sb.draw(0.0f, 0.0f, W, H, ui::rgba(0, 0, 0, 130));  // dim the screen behind
    ui::panel(sb, d.x, d.y, d.w, d.h, ui::color::kWinBody);
    ui::border(sb, d.x, d.y, d.w, d.h, ui::color::kWinBorder);
    font.draw(sb, d.x + 16.0f, d.y + 14.0f, 1.5f, ui::color::kWinText, tr("charselect.create_title"));
    font.draw(sb, d.fieldX, d.fieldY - 15.0f, 1.0f, ui::color::kWinTextDim,
              "Name (slot " + std::to_string(makeSlot_) + ")");
    makeName_.draw(sb, font, d.fieldX, d.fieldY, d.fieldW, d.fieldH, /*focused=*/true, time_,
                   "character name");

    // Six stat rows: LABEL [-] value [+]. The buttons reuse ui::button so they share the
    // skin's hover/press feedback; render-only state must match update()'s gating exactly.
    const int pair[6] = {3, 5, 4, 0, 2, 1};
    const int left = statPointsLeft();
    for (int r = 0; r < 6; ++r) {
        const float y = d.rowY(r);
        font.draw(sb, d.statX, y + 4.0f, 1.5f, ui::color::kWinText, kStatLabels[r]);
        const bool canDec = makeStats_[r] > 1;
        const bool canInc = makeStats_[r] < 9 && left > 0 &&
                            (makeStats_[r] + 1 + makeStats_[pair[r]] <= 10);
        ui::button(sb, font, in, d.minusX(), y, d.statBtnW, d.statBtnW, "-", 1.5f, canDec);
        const std::string v = std::to_string(makeStats_[r]);
        font.draw(sb, d.valueX() + (d.statValW - font.width(v, 1.5f)) * 0.5f, y + 4.0f, 1.5f,
                  ui::color::kWinText, v);
        ui::button(sb, font, in, d.plusX(), y, d.statBtnW, d.statBtnW, "+", 1.5f, canInc);
    }
    const std::string pts = "Points left: " + std::to_string(left);
    font.draw(sb, d.statX, d.pointsY, 1.5f, left == 0 ? ui::color::kWinTextDim : ui::color::kError,
              pts);

    const bool ok = !makeName_.value.empty() && createValid();
    ui::button(sb, font, in, d.makeX, d.btnY, d.bw, d.bh, tr("common.create"), 1.5f, ok);
    ui::button(sb, font, in, d.cancelX, d.btnY, d.bw, d.bh, tr("common.cancel"), 1.5f);
}

void CharSelectScene::render(Application& app) {
    SpriteBatch& sb = app.sprites();
    if (!sb.ready()) return;
    const Layout L = layout(app);
    // Draw into the logical canvas (physical / UI-scale) so the whole char-select screen scales (#134).
    const float uis = app.uiScale();
    sb.begin(app.render().width(), app.render().height(), 0,
             static_cast<int>(app.render().width() / uis),
             static_cast<int>(app.render().height() / uis));
    // Dark-grey background instead of the old picture (S.: "тёмно-серый фон").
    sb.draw(0.0f, 0.0f, L.W, L.H, ui::rgba(48, 48, 52, 255));
    if (app.uiSkin().ready())
        renderSkinned(app, L);
    else
        renderFlat(app, L);
    if (confirmDelete_) renderDeleteDialog(app);
    if (creating_) renderCreateDialog(app);
    if (ui::osk().active) ui::oskRender(sb, app.font(), L.W, L.H);  // gamepad on-screen keyboard, on top
    sb.end();
}

} // namespace uaro
