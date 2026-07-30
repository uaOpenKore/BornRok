#include "ui/UiSkin.hpp"

#include <string>
#include <unordered_map>
#include <utility>

#include "core/Log.hpp"
#include "formats/Bmp.hpp"
#include "formats/ImageIO.hpp"  // decodeImage: PNG/BMP/TGA by magic (hi-res PNG skin override, S.)
#include "resource/Vfs.hpp"

namespace uaro::ui {

namespace {
// "data/texture/<유저인터페이스>/" exactly as the GRF index stores it (CP949 run
// through the archive's lowercasing normalize(), which is why the directory bytes look
// mangled — they match because normalize() is idempotent).
// The STANDARD interface folder (raw EUC-KR 유저인터페이스). This is the one that actually matches this
// data.grf (in-game basic_interface loads fine from here); the earlier custom "a_a..." pack path was a
// hard-coded guess whose bytes did NOT match the GRF, so login skins loaded 0 (S.: login/charselect UI
// blank). Load BOTH login_interface and basic_interface from here.
const std::string kStdUiFolder = std::string("data/texture/") +
    "\xc0\xaf\xc0\xfa\xc0\xce\xc5\xcd\xc6\xe4\xc0\xcc\xbd\xba" + "/";
const std::string kDir = kStdUiFolder + "login_interface/";

// The login UI bitmaps the client uses (the names actually present in the GRF's
// login_interface folder; only btn_connect/btn_exit ship hover/pressed states).
const char* kFiles[] = {
    "bgi_temp.bmp",      "win_service.bmp", "win_login.bmp", "win_select.bmp",
    "win_make.bmp",      "btn_connect.bmp", "btn_connect_a.bmp", "btn_connect_b.bmp",
    "btn_exit.bmp",      "btn_exit_a.bmp",  "btn_exit_b.bmp", "btn_cancel.bmp",
    "btn_back.bmp",      "btn_close.bmp",   "btn_ok.bmp",     "btn_make.bmp",
    "btn_next.bmp",
};

// In-game window skin bitmaps (basic_interface). Each window is one full background
// (NOT a 9-slice): statwin_bg/collection_bg/equipwin_bg/chatwin0_bg, plus the shared
// close button. The labels (Str/Atk/...) are printed on the bg; we draw only the
// values on top, matching the original client.
const std::string kBasicDir = kStdUiFolder + "basic_interface/";
const char* kBasicFiles[] = {
    "statwin_bg.bmp", "collection_bg.bmp", "equipwin_bg.bmp", "chatwin0_bg.bmp",
    "exchange_bg.bmp", "arw_right.bmp",    "arw_right_on.bmp",
    "basewin_bg.bmp",  // the Basic Information window background (name/HP/SP/EXP/weight/zeny)
    // window title bar: tiled middle strip + the close button (roBrowser titlebar)
    "titlebar_mid.bmp", "sys_close_off.bmp", "sys_close_on.bmp",
    // quick-access (hotbar) buttons: normal (_off) + hover/pressed (_on)
    "btn_status_off.bmp", "btn_status_on.bmp", "btn_items_off.bmp", "btn_items_on.bmp",
    "btn_equip_off.bmp",  "btn_equip_on.bmp",  "btn_skill_off.bmp", "btn_skill_on.bmp",
    // rest of the basic-info button grid (S. reference: status/option, items/equip, skill/map,
    // comm/friend — a 2x4 grid on the right of the window).
    "btn_option_off.bmp", "btn_option_on.bmp", "btn_map_off.bmp",    "btn_map_on.bmp",
    "btn_comm_off.bmp",   "btn_comm_on.bmp",   "btn_friend_off.bmp", "btn_friend_on.bmp",
};

// The real GRF data\texture\...\basic_interface\*.bmp logical sizes. RO window layout (button grids,
// value slots, titlebar) is hardcoded against these pixel sizes, so an HD 2x/4x skin override MUST NOT
// change them — return the fixed logical size for the layout-critical skins (nullptr = use the loaded
// size). Verified by extracting each .bmp from the GRF and reading its BMP header (280-wide windows).
const std::pair<int, int>* logicalSizeFor(const char* file) {
    static const std::unordered_map<std::string, std::pair<int, int>> kSizes = {
        {"basewin_bg.bmp", {280, 120}},  {"statwin_bg.bmp", {280, 103}},
        {"equipwin_bg.bmp", {280, 130}}, {"collection_bg.bmp", {280, 120}},
        {"chatwin0_bg.bmp", {280, 103}}, {"exchange_bg.bmp", {280, 103}},
        {"btn_status_off.bmp", {30, 20}}, {"btn_status_on.bmp", {30, 20}},
        {"btn_items_off.bmp", {30, 20}},  {"btn_items_on.bmp", {30, 20}},
        {"btn_equip_off.bmp", {30, 20}},  {"btn_equip_on.bmp", {30, 20}},
        {"btn_skill_off.bmp", {30, 20}},  {"btn_skill_on.bmp", {30, 20}},
        {"btn_option_off.bmp", {30, 20}}, {"btn_option_on.bmp", {30, 20}},
        {"btn_map_off.bmp", {30, 20}},    {"btn_map_on.bmp", {30, 20}},
        {"btn_comm_off.bmp", {30, 20}},   {"btn_comm_on.bmp", {30, 20}},
        {"btn_friend_off.bmp", {30, 20}}, {"btn_friend_on.bmp", {30, 20}},
        {"sys_close_off.bmp", {11, 11}},  {"sys_close_on.bmp", {11, 11}},
        {"titlebar_mid.bmp", {12, 17}},
        {"arw_right.bmp", {11, 11}}, {"arw_right_on.bmp", {11, 11}},  // stat (+) up-arrows
    };
    auto it = kSizes.find(file);
    return it == kSizes.end() ? nullptr : &it->second;
}
} // namespace

void UiSkin::init(const Vfs& vfs) {
    auto loadList = [&](const std::string& dir, const char* const* files, usize n) {
        int loaded = 0;
        for (usize i = 0; i < n; ++i) {
            auto bytes = vfs.readPreferPng(dir + files[i]);  // hi-res .png skin over the .bmp (S.)
            if (!bytes) continue;
            auto img = decodeImage(*bytes);  // PNG/BMP/TGA by magic
            if (!img || !img->valid()) continue;
            // Key + despill the transparent magenta INCLUDING the anti-aliased pink rim, so a scaled
            // skin's titlebar X / button edges don't bleed pink (S.: "магенту убрать - пробивается на
            // контурах"). Shared with the texture/effect path (keyAndDespillMagenta) for one behaviour.
            keyAndDespillMagenta(*img);
            UiImage ui;
            // RO window layout is in ORIGINAL (.bmp) pixels — button-grid offsets, titlebar sizes etc.
            // are hardcoded against them. A hi-res PNG override is authored at 2x/4x, so if we took the
            // PNG's own pixel size as the logical size the whole window would blow up 4x and misalign
            // (S.: "текстуры ХД в пнг — окна кривые, приходится уменьшать с х4"). Keep the logical size
            // = the .bmp's; upload the dense PNG pixels as the texture (bilinear, UV 0..1) so the window
            // stays the right size but sharp. Falls back to the image's own size when no .bmp exists.
            ui.w = static_cast<int>(img->width);
            ui.h = static_cast<int>(img->height);
            const bool texIsPng = bytes->size() >= 4 && (*bytes)[0] == 0x89 && (*bytes)[1] == 'P';
            if (texIsPng)
                if (auto raw = vfs.read(dir + files[i]))  // the original .bmp (read() does NOT swap to png)
                    if (auto rawImg = decodeImage(*raw); rawImg && rawImg->valid()) {
                        ui.w = static_cast<int>(rawImg->width);
                        ui.h = static_cast<int>(rawImg->height);
                    }
            // Hard override to the KNOWN RO logical size for the hardcoded-layout window skins. The
            // .bmp fallback above fails when the content maker ships an HD skin as a 2x .BMP (texIsPng
            // false -> no fallback) OR replaces the GRF .bmp itself, so the window blew up 2x (S.:
            // "подложка должна быть в 2 раза меньше, и кнопки"). These logical sizes are the real GRF
            // basic_interface\*.bmp dimensions; the HD pixels still upload as the texture (sharp).
            if (const auto* ls = logicalSizeFor(files[i])) { ui.w = ls->first; ui.h = ls->second; }
            ui.tex.create(static_cast<u16>(img->width), static_cast<u16>(img->height),
                          img->rgba.data(), /*smooth=*/true);  // bilinear so scaled skin is clean
            imgs_.emplace(files[i], std::move(ui));
            ++loaded;
        }
        return loaded;
    };
    const int login = loadList(kDir, kFiles, sizeof(kFiles) / sizeof(kFiles[0]));
    const int basic = loadList(kBasicDir, kBasicFiles, sizeof(kBasicFiles) / sizeof(kBasicFiles[0]));
    ready_ = imgs_.count("win_login.bmp") > 0;  // login essentials present
    log::info("UiSkin: {} login + {} in-game UI texture(s) loaded ({})", login, basic,
              ready_ ? "original skin active" : "incomplete -> flat UI fallback");
}

void UiSkin::destroy() {
    for (auto& [name, img] : imgs_) img.tex.destroy();
    imgs_.clear();
    ready_ = false;
}

const UiImage* UiSkin::get(const std::string& name) const {
    auto it = imgs_.find(name);
    return it == imgs_.end() ? nullptr : &it->second;
}

} // namespace uaro::ui
