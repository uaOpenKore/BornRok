#pragma once
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "platform/Input.hpp"
#include "ui/UiSkin.hpp"

namespace uaro {

class SpriteBatch;
class Font;

namespace ui {

// Pack RGBA into the ABGR word SpriteBatch/Font expect (low byte = red).
constexpr u32 rgba(u8 r, u8 g, u8 b, u8 a = 255) {
    return (static_cast<u32>(a) << 24) | (static_cast<u32>(b) << 16) |
           (static_cast<u32>(g) << 8) | static_cast<u32>(r);
}

// Shared palette for the login / char-select screens.
namespace color {
constexpr u32 kPanel = rgba(28, 30, 40, 235);
constexpr u32 kPanelHi = rgba(46, 50, 66, 245);
constexpr u32 kField = rgba(16, 17, 23, 255);
constexpr u32 kBorder = rgba(70, 76, 96, 255);
constexpr u32 kBorderFocus = rgba(120, 170, 235, 255);
constexpr u32 kText = rgba(225, 228, 235, 255);
constexpr u32 kTextDim = rgba(120, 126, 140, 255);
constexpr u32 kButton = rgba(54, 86, 140, 255);
constexpr u32 kButtonHi = rgba(72, 116, 184, 255);
constexpr u32 kError = rgba(232, 96, 96, 255);
constexpr u32 kOk = rgba(120, 210, 130, 255);
constexpr u32 kSelect = rgba(58, 96, 150, 255);
constexpr u32 kFieldText = rgba(30, 32, 42, 255);  // dark text on the skin's light fields
constexpr u32 kSkinText = rgba(40, 44, 70, 255);   // labels/values over the light skin
constexpr u32 kWhite = rgba(255, 255, 255, 255);

// Classic-RO light window theme — matches roBrowser's in-game windows (NpcBox, NpcMenu,
// NpcStore, Escape): a near-white body, a pale sage frame, dark text and a light-blue
// selection. Used by window()/button() and the in-game dialog/shop/menu/death panels so
// they read like the reference instead of the old dark slabs. (The HUD over the 3D world
// keeps the light kText/kOk/... above, which stay legible against the map.)
constexpr u32 kWinBody = rgba(247, 248, 250, 255);          // near-white window body
constexpr u32 kWinTitle = rgba(228, 232, 240, 255);         // title-bar strip (light)
constexpr u32 kWinBorder = rgba(193, 198, 194, 255);        // #c1c6c2 pale sage frame
constexpr u32 kWinText = rgba(40, 44, 54, 255);             // dark body text
constexpr u32 kWinTextDim = rgba(120, 126, 138, 255);       // dim body text
constexpr u32 kWinContent = rgba(239, 244, 240, 255);       // #eff4f0 content panel
constexpr u32 kWinSelect = rgba(205, 224, 255, 255);        // #cde0ff menu selection
constexpr u32 kWinSelectStrong = rgba(115, 156, 238, 255);  // #739cee list selection
constexpr u32 kWinAccent = rgba(88, 112, 158, 255);         // #58709e titles/values accent
constexpr u32 kZeny = rgba(36, 88, 214, 255);               // vivid blue for zeny amounts (S.: броский)
constexpr u32 kWinButton = rgba(224, 228, 236, 255);        // light button face
constexpr u32 kWinButtonHi = rgba(208, 222, 246, 255);      // hovered button face
} // namespace color

// Filled rectangle.
void panel(SpriteBatch& sb, float x, float y, float w, float h, u32 abgr);
// 1px (thickness) outline rectangle.
void border(SpriteBatch& sb, float x, float y, float w, float h, u32 abgr, float thickness = 2.0f);

// A standard modal window: panel + a title bar (accent strip + title) + border, so
// every in-game window (dialog, shop, death, menu) shares one frame. Returns the y
// at which the content area begins, just under the title bar. `accent` colours the
// title underline (e.g. kError for the death window).
float window(SpriteBatch& sb, const Font& font, float x, float y, float w, float h,
             const std::string& title, u32 accent = color::kBorderFocus);
// Height of window()'s title bar (so callers can size the panel to fit their content).
constexpr float kTitleBarH = 26.0f;

// A clickable button. Returns true on the frame it is clicked. Centres `label`.
bool button(SpriteBatch& sb, const Font& font, const InputState& in, float x, float y, float w,
            float h, const std::string& label, float textScale = 2.0f, bool enabled = true);

// Gamepad focus navigation (#102/#115). While enabled, every ui::button registers its rect this frame
// and the one under the focus point (fx,fy) gets a magenta ring (4px gap, per S.); a `confirm` pulse
// makes that button return true (activate). The scene clears `widgets` before drawing UI, drives the
// focus point from the pad, calls navMove() AFTER the UI is drawn to hop to the nearest widget in a
// direction, and reads focusRect() to move a window / anchor a context menu.
struct Nav {
    bool enabled = false;
    float fx = -1.0f, fy = -1.0f;  // focus point in screen px; <0 = unset
    bool confirm = false;          // activate the focused widget this frame
    struct R { float x, y, w, h; };
    std::vector<R> widgets;        // rects registered this frame (cleared by the scene each frame)
    bool contains(const R& r) const {
        return fx >= r.x && fx < r.x + r.w && fy >= r.y && fy < r.y + r.h;
    }
};
Nav& nav();  // process-wide focus-nav state

// UI click sound (S. sound audit): ui::button / ui::imageButton flag a click here; the app polls
// takeButtonClicked() once per frame (which self-clears) and plays a click wav. markButtonClicked()
// is exposed so other bespoke clickable widgets can opt in.
void markButtonClicked();
bool takeButtonClicked();
void navMove(int dir);  // 1=up 2=down 3=left 4=right: hop focus to the nearest registered widget
bool navFocusRect(Nav::R& out);  // the widget under the focus point, if any (for window move / context)

// Register an arbitrary cell rect for gamepad focus-nav — for list rows (inventory/skill/…) that the
// immediate-mode ui::button/imageButton don't cover, so the pad can focus them and A activates. Draws
// the magenta focus ring when focused. Returns true while focused; sets *confirmed if the confirm
// pulse (A) fired on it this frame. No-op (returns false) when nav is disabled, so mouse/keyboard use
// is completely unaffected. (#102/#115)
bool navCell(SpriteBatch& sb, float x, float y, float w, float h, bool* confirmed = nullptr);

// On-screen keyboard (#102/#115): a pad-navigable QWERTY shown when a text field is focused in gamepad
// mode. It injects synthetic keystrokes into the InputState (appends typed chars to in.text, raises
// keyBackspace / keyEnter) so the existing TextField::update consumes them unchanged. The scene sets
// enabled while a field has focus and calls oskDrive() once per frame; while active it also owns the
// pad, so the scene should suppress its own d-pad / face handling (check osk().active).
struct Osk {
    bool enabled = false;    // a text field is focused in gamepad mode this frame
    bool active = false;     // the keyboard is shown and owns the pad
    int fr = 0, fc = 0;      // highlighted key (row, col)
    bool shift = false;      // next letter uppercase
    bool symbols = false;    // symbol / number page
    bool done = false;       // Enter was pressed on the OSK this frame (scene may submit + hide)
};
Osk& osk();  // process-wide on-screen-keyboard state
// Step the highlight from in.pad and inject the pressed key into `in` (call in update(), before the
// TextField updates that consume it). Renders nothing.
void oskInput(InputState& in);
// Draw the keyboard bottom-centre using the current highlight (call in render(), where a batch exists).
void oskRender(SpriteBatch& sb, const Font& font, float screenW, float screenH);

// Draw a skin image at its native size (or scaled), tinted by `tint`.
void image(SpriteBatch& sb, const UiImage& img, float x, float y, u32 tint = color::kWhite);
void imageScaled(SpriteBatch& sb, const UiImage& img, float x, float y, float w, float h,
                 u32 tint = color::kWhite);

// A button drawn from skin textures (normal/hover/pressed). Hit area = normal's
// size. Returns true on click. Any of hover/press may be null (falls back).
bool imageButton(SpriteBatch& sb, const InputState& in, float x, float y, const UiImage* normal,
                 const UiImage* hover, const UiImage* press);

// Like imageButton but drawn (and hit-tested) at an explicit w×h instead of the skin's native size —
// for the shrunk login / char-select windows (S.: подложку и кнопки в 2 раза). Same hover/press/nav.
bool imageButtonScaled(SpriteBatch& sb, const InputState& in, float x, float y, float w, float h,
                       const UiImage* normal, const UiImage* hover, const UiImage* press);

// Single-line editable text field with a caret and optional password masking.
struct TextField {
    std::string value;
    bool password = false;
    usize caret = 0;
    usize maxLen = 23;  // RO account/password fields are capped at 23 chars
    bool asciiOnly = false;  // reject non-ASCII (Cyrillic/other scripts): login + char-name fields (S.)
    bool alnumOnly = false;  // reject anything but [A-Za-z0-9]: character name = English letters/digits ONLY (S.)
    float fontScale = 2.0f;  // text size in draw() (8px * scale); chat fields set this smaller

    // Apply this frame's input when focused. Returns true if Enter was pressed.
    bool update(const InputState& in, bool focused);

    void draw(SpriteBatch& sb, const Font& font, float x, float y, float w, float h, bool focused,
              double time, const std::string& placeholder = "") const;

    // Draw only the text + caret (no box) inside a field whose background is baked
    // into a skin window. Text is dark to read on the light field graphic.
    void drawInField(SpriteBatch& sb, const Font& font, float x, float y, float h, bool focused,
                     double time) const;
};

// Set true by any focused TextField's update() each frame; Application polls + resets it to raise or
// hide the native OS keyboard on touch devices (Window::setTextInput). Harmless on desktop, where
// text input is always on. (#102 native-keyboard refinement.)
bool& text_input_wanted();

} // namespace ui
} // namespace uaro
