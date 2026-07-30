#include "ui/Widgets.hpp"

#include <cmath>

#include "render/SpriteBatch.hpp"
#include "ui/Font.hpp"

namespace uaro::ui {

void panel(SpriteBatch& sb, float x, float y, float w, float h, u32 abgr) {
    sb.draw(x, y, w, h, abgr);
}

void border(SpriteBatch& sb, float x, float y, float w, float h, u32 abgr, float t) {
    sb.draw(x, y, w, t, abgr);              // top
    sb.draw(x, y + h - t, w, t, abgr);      // bottom
    sb.draw(x, y, t, h, abgr);              // left
    sb.draw(x + w - t, y, t, h, abgr);      // right
}

float window(SpriteBatch& sb, const Font& font, float x, float y, float w, float h,
             const std::string& title, u32 accent) {
    panel(sb, x, y, w, h, color::kWinBody);                  // near-white body
    panel(sb, x, y, w, kTitleBarH, color::kWinTitle);        // light title bar
    panel(sb, x, y + kTitleBarH - 2.0f, w, 2.0f, accent);    // accent underline
    border(sb, x, y, w, h, color::kWinBorder);               // pale sage frame
    if (!title.empty())
        font.draw(sb, x + 10.0f, y + 6.0f, 1.5f, color::kWinText, title);
    return y + kTitleBarH + 8.0f;  // where the content area begins
}

bool button(SpriteBatch& sb, const Font& font, const InputState& in, float x, float y, float w,
            float h, const std::string& label, float textScale, bool enabled) {
    const bool hovered = enabled && in.hit(x, y, w, h);
    const u32 fill = !enabled ? color::kWinBody : (hovered ? color::kWinButtonHi : color::kWinButton);
    panel(sb, x, y, w, h, fill);
    border(sb, x, y, w, h, color::kWinBorder);

    const float tw = font.width(label, textScale);
    const float th = 8.0f * textScale;
    font.draw(sb, x + (w - tw) * 0.5f, y + (h - th) * 0.5f, textScale,
              enabled ? color::kWinText : color::kWinTextDim, label);

    // Gamepad focus navigation (#102/#115): register this button and, if the focus point is on it,
    // draw a magenta ring (4px gap) and let a confirm pulse activate it.
    if (nav().enabled && enabled) {
        Nav& n = nav();
        n.widgets.push_back({x, y, w, h});
        if (n.fx >= 0.0f && n.contains({x, y, w, h})) {
            border(sb, x - 4.0f, y - 4.0f, w + 8.0f, h + 8.0f, rgba(255, 0, 255, 255));
            if (n.confirm) { markButtonClicked(); return true; }
        }
    }
    const bool clicked = enabled && hovered && in.mousePressed;
    if (clicked) markButtonClicked();
    return clicked;
}

bool imageButtonScaled(SpriteBatch& sb, const InputState& in, float x, float y, float w, float h,
                       const UiImage* normal, const UiImage* hover, const UiImage* press) {
    if (!normal || !normal->valid()) return false;
    const bool hovered = in.hit(x, y, w, h);
    const UiImage* show = normal;
    if (hovered && in.mouseDown && press && press->valid()) show = press;
    else if (hovered && hover && hover->valid()) show = hover;
    imageScaled(sb, *show, x, y, w, h);
    if (nav().enabled) {
        Nav& n = nav();
        n.widgets.push_back({x, y, w, h});
        if (n.fx >= 0.0f && n.contains({x, y, w, h})) {
            border(sb, x - 4.0f, y - 4.0f, w + 8.0f, h + 8.0f, rgba(255, 0, 255, 255));
            if (n.confirm) { markButtonClicked(); return true; }
        }
    }
    const bool clicked = hovered && in.mousePressed;
    if (clicked) markButtonClicked();
    return clicked;
}

// Process-wide "a ui button was clicked this frame" flag, so the app can play one click sound
// per activation without every button widget needing an Audio handle. (S. sound audit)
static bool g_btnClicked = false;
void markButtonClicked() { g_btnClicked = true; }
bool takeButtonClicked() { const bool c = g_btnClicked; g_btnClicked = false; return c; }

Nav& nav() {
    static Nav g;
    return g;
}

bool navFocusRect(Nav::R& out) {
    Nav& n = nav();
    if (n.fx < 0.0f) return false;
    for (const Nav::R& r : n.widgets)
        if (n.fx >= r.x && n.fx < r.x + r.w && n.fy >= r.y && n.fy < r.y + r.h) { out = r; return true; }
    return false;
}

bool navCell(SpriteBatch& sb, float x, float y, float w, float h, bool* confirmed) {
    if (confirmed) *confirmed = false;
    if (!nav().enabled) return false;
    Nav& n = nav();
    n.widgets.push_back({x, y, w, h});
    if (n.fx >= 0.0f && n.contains({x, y, w, h})) {
        border(sb, x - 4.0f, y - 4.0f, w + 8.0f, h + 8.0f, rgba(255, 0, 255, 255));  // magenta ring
        if (n.confirm && confirmed) { *confirmed = true; markButtonClicked(); }
        return true;
    }
    return false;
}

void navMove(int dir) {
    Nav& n = nav();
    if (n.widgets.empty()) return;
    if (n.fx < 0.0f) {  // no focus yet -> take the first registered widget
        const Nav::R& r = n.widgets[0];
        n.fx = r.x + r.w * 0.5f;
        n.fy = r.y + r.h * 0.5f;
        return;
    }
    float best = 1e30f;
    int bi = -1;
    for (usize i = 0; i < n.widgets.size(); ++i) {
        const Nav::R& r = n.widgets[i];
        const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
        const float dx = cx - n.fx, dy = cy - n.fy;
        bool ok = false;
        if (dir == 1) ok = dy < -2.0f;
        else if (dir == 2) ok = dy > 2.0f;
        else if (dir == 3) ok = dx < -2.0f;
        else if (dir == 4) ok = dx > 2.0f;
        if (!ok) continue;
        const float prim = (dir <= 2) ? (dy < 0 ? -dy : dy) : (dx < 0 ? -dx : dx);
        const float perp = (dir <= 2) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        const float cost = prim + perp * 2.0f;  // nearest along the axis, penalise sideways drift
        if (cost < best) { best = cost; bi = static_cast<int>(i); }
    }
    if (bi >= 0) {
        const Nav::R& r = n.widgets[bi];
        n.fx = r.x + r.w * 0.5f;
        n.fy = r.y + r.h * 0.5f;
    }
}

Osk& osk() {
    static Osk g;
    return g;
}

namespace {
// Ragged key grid. Letter and symbol pages share the bottom control row. Control keys are tagged with a
// leading '\x01' so a single glyph char ("a") is a plain key and multi-char tokens are controls.
const char* kOskLetters[4][11] = {
    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", nullptr},
    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", nullptr},
    {"a", "s", "d", "f", "g", "h", "j", "k", "l", nullptr, nullptr},
    {"z", "x", "c", "v", "b", "n", "m", nullptr, nullptr, nullptr, nullptr},
};
const char* kOskSymbols[4][11] = {
    {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")", nullptr},
    {"-", "_", "=", "+", "[", "]", "{", "}", "\\", "|", nullptr},
    {";", ":", "'", "\"", ",", ".", "<", ">", "/", "?", nullptr},
    {"~", "`", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
};
// Bottom control row (row index 4): labels are what we render; behaviour is by column.
const char* kOskCtl[5] = {"Shift", "123", "Space", "Del", "OK"};
int oskRowLen(int r, bool symbols) {
    if (r == 4) return 5;
    int n = 0;
    for (int c = 0; c < 11; ++c) if ((symbols ? kOskSymbols : kOskLetters)[r][c]) ++n;
    return n;
}
} // namespace

void oskInput(InputState& in) {
    Osk& k = osk();
    k.done = false;
    const InputState::Gamepad& p = in.pad;

    // Move the highlight (d-pad edges). Clamp the column into the destination row's length.
    if (p.dpadUp) k.fr = (k.fr + 4) % 5;
    if (p.dpadDown) k.fr = (k.fr + 1) % 5;
    if (p.dpadLeft) k.fc -= 1;
    if (p.dpadRight) k.fc += 1;
    int rl = oskRowLen(k.fr, k.symbols);
    if (rl < 1) rl = 1;
    if (k.fc < 0) k.fc = rl - 1;
    if (k.fc >= rl) k.fc = 0;

    // North = backspace, East = space (quick keys that skip the grid). West = shift toggle.
    if (p.north) in.keyBackspace = true;
    if (p.east) in.text.push_back(' ');
    if (p.west) k.shift = !k.shift;

    // South = press the highlighted key.
    if (p.south) {
        if (k.fr == 4) {
            switch (k.fc) {
                case 0: k.shift = !k.shift; break;                 // Shift
                case 1: k.symbols = !k.symbols; k.fc = 0; break;   // 123 / abc
                case 2: in.text.push_back(' '); break;             // Space
                case 3: in.keyBackspace = true; break;             // Del
                case 4: in.keyEnter = true; k.done = true; break;  // OK
            }
        } else {
            const char* g = (k.symbols ? kOskSymbols : kOskLetters)[k.fr][k.fc];
            if (g && g[0]) {
                char ch = g[0];
                if (k.shift && ch >= 'a' && ch <= 'z') { ch = static_cast<char>(ch - 'a' + 'A'); k.shift = false; }
                in.text.push_back(ch);
            }
        }
    }
}

void oskRender(SpriteBatch& sb, const Font& font, float screenW, float screenH) {
    Osk& k = osk();
    // Render bottom-centre. Fixed metrics so the layout is stable across resolutions.
    const float kw = 34.0f, kh = 30.0f, gap = 4.0f, pad = 8.0f;
    const float gridW = 11 * kw + 10 * gap;
    const float boardW = gridW + pad * 2.0f;
    const float boardH = 5 * kh + 4 * gap + pad * 2.0f + 18.0f;  // +header strip
    const float bx = (screenW - boardW) * 0.5f;
    const float by = screenH - boardH - 12.0f;
    panel(sb, bx, by, boardW, boardH, rgba(18, 20, 28, 240));
    border(sb, bx, by, boardW, boardH, rgba(120, 128, 150, 220));
    font.draw(sb, bx + pad, by + 4.0f, 1.0f, rgba(180, 186, 205, 255),
              k.shift ? "SHIFT ON   D-pad move   A key   X shift   B space   Y del   OK enter"
                      : "D-pad move   A key   X shift   B space   Y del   OK enter");
    const float gy = by + 22.0f;
    for (int r = 0; r < 5; ++r) {
        const int n = oskRowLen(r, k.symbols);
        const float ry = gy + r * (kh + gap);
        // Control row keys are wider; letter rows left-align.
        for (int c = 0; c < n; ++c) {
            float cw = kw;
            const char* label = nullptr;
            if (r == 4) { cw = (c == 2) ? kw * 3.0f + gap * 2.0f : kw * (c == 0 ? 2.0f : 1.5f) + gap; label = kOskCtl[c]; }
            else label = (k.symbols ? kOskSymbols : kOskLetters)[r][c];
            // Lay control keys out sequentially; letter keys on the fixed column grid.
            float cx;
            if (r == 4) {
                cx = bx + pad;
                for (int i = 0; i < c; ++i) {
                    const float pw = (i == 2) ? kw * 3.0f + gap * 2.0f : kw * (i == 0 ? 2.0f : 1.5f) + gap;
                    cx += pw + gap;
                }
            } else {
                cx = bx + pad + c * (kw + gap);
            }
            const bool foc = (r == k.fr && c == k.fc);
            panel(sb, cx, ry, cw, kh, foc ? rgba(60, 90, 150, 255) : rgba(40, 44, 58, 255));
            border(sb, cx, ry, cw, kh, foc ? rgba(255, 0, 255, 255) : rgba(70, 76, 96, 220));
            if (label) {
                std::string s = label;
                if (r != 4 && k.shift && s.size() == 1 && s[0] >= 'a' && s[0] <= 'z')
                    s[0] = static_cast<char>(s[0] - 'a' + 'A');
                if (r == 4 && c == 1) s = k.symbols ? "abc" : "123";
                const float tw = font.width(s, 1.0f);
                font.draw(sb, cx + (cw - tw) * 0.5f, ry + (kh - 8.0f) * 0.5f, 1.0f, rgba(230, 234, 245, 255), s);
            }
        }
    }
}

void image(SpriteBatch& sb, const UiImage& img, float x, float y, u32 tint) {
    if (img.valid()) sb.draw(x, y, static_cast<float>(img.w), static_cast<float>(img.h), tint, img.tex);
}

void imageScaled(SpriteBatch& sb, const UiImage& img, float x, float y, float w, float h, u32 tint) {
    if (img.valid()) sb.draw(x, y, w, h, tint, img.tex);
}

bool imageButton(SpriteBatch& sb, const InputState& in, float x, float y, const UiImage* normal,
                 const UiImage* hover, const UiImage* press) {
    if (!normal || !normal->valid()) return false;
    const float w = static_cast<float>(normal->w), h = static_cast<float>(normal->h);
    const bool hovered = in.hit(x, y, w, h);
    const UiImage* show = normal;
    if (hovered && in.mouseDown && press && press->valid()) show = press;
    else if (hovered && hover && hover->valid()) show = hover;
    image(sb, *show, x, y);
    // Gamepad focus navigation (#102/#115): image buttons (the BasicInfo main-menu column) join the same
    // focus set as text buttons, so the pad can reach Status/Item/Equip/Skill etc.
    if (nav().enabled) {
        Nav& n = nav();
        n.widgets.push_back({x, y, w, h});
        if (n.fx >= 0.0f && n.contains({x, y, w, h})) {
            border(sb, x - 4.0f, y - 4.0f, w + 8.0f, h + 8.0f, rgba(255, 0, 255, 255));
            if (n.confirm) { markButtonClicked(); return true; }
        }
    }
    const bool clicked = hovered && in.mousePressed;
    if (clicked) markButtonClicked();
    return clicked;
}

bool& text_input_wanted() {
    static bool v = false;
    return v;
}

bool TextField::update(const InputState& in, bool focused) {
    if (!focused) return false;
    text_input_wanted() = true;  // a field is being edited -> raise the OS keyboard on touch devices
    if (caret > value.size()) caret = value.size();

    // Insert typed text. SDL hands us UTF-8 (in.text); keep printable ASCII AND any multi-byte
    // sequence (bytes >= 0x80) so Cyrillic and other non-Latin input survives (S.: "кириллица в
    // чате не работает"). Only control bytes and DEL are dropped. caret and maxLen count BYTES;
    // the edit/caret ops below step over whole UTF-8 codepoints so a letter never splits.
    for (char ch : in.text) {
        const unsigned char uc = static_cast<unsigned char>(ch);
        if (uc < 0x20 || uc == 0x7F) continue;  // drop control chars + DEL, keep ASCII + UTF-8
        if (asciiOnly && uc >= 0x80) continue;   // login + char-name: English only, no Cyrillic/other (S.)
        // Character name: permit ONLY English letters + digits — no Cyrillic/hieroglyphs/other scripts,
        // no punctuation/space (S.: "запретить любые буквы и иероглифы кроме английских"). Rejects the
        // multi-byte UTF-8 too since every continuation byte is >= 0x80.
        if (alnumOnly && !((uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z') || (uc >= '0' && uc <= '9')))
            continue;
        if (value.size() >= maxLen) break;
        value.insert(value.begin() + caret, ch);
        ++caret;
    }

    // A UTF-8 continuation byte is 10xxxxxx (0x80..0xBF); a codepoint = one lead byte + its
    // continuation bytes. Backspace/Delete/arrows move by a whole codepoint so a 2-byte Cyrillic
    // letter never leaves a dangling half-byte in the buffer.
    auto isCont = [&](usize i) { return (static_cast<unsigned char>(value[i]) & 0xC0) == 0x80; };
    if (in.keyBackspace && caret > 0) {
        usize start = caret - 1;
        while (start > 0 && isCont(start)) --start;
        value.erase(value.begin() + start, value.begin() + caret);
        caret = start;
    }
    if (in.keyDelete && caret < value.size()) {
        usize end = caret + 1;
        while (end < value.size() && isCont(end)) ++end;
        value.erase(value.begin() + caret, value.begin() + end);
    }
    if (in.keyLeft && caret > 0) {
        --caret;
        while (caret > 0 && isCont(caret)) --caret;
    }
    if (in.keyRight && caret < value.size()) {
        ++caret;
        while (caret < value.size() && isCont(caret)) ++caret;
    }
    if (in.keyHome) caret = 0;
    if (in.keyEnd) caret = value.size();

    return in.keyEnter;
}

void TextField::draw(SpriteBatch& sb, const Font& font, float x, float y, float w, float h,
                     bool focused, double time, const std::string& placeholder) const {
    panel(sb, x, y, w, h, color::kField);
    border(sb, x, y, w, h, focused ? color::kBorderFocus : color::kBorder);

    const float scale = fontScale;  // per-field (default 2.0; chat fields use a smaller size, S.)
    const float pad = 6.0f;
    const float ty = y + (h - 8.0f * scale) * 0.5f;

    std::string shown = password ? std::string(value.size(), '*') : value;
    if (value.empty() && !placeholder.empty()) {
        font.draw(sb, x + pad, ty, scale, color::kTextDim, placeholder);
    } else {
        font.draw(sb, x + pad, ty, scale, color::kText, shown);
    }

    if (focused && std::fmod(time, 1.0) < 0.5) {  // blinking caret
        const usize c = caret < shown.size() ? caret : shown.size();
        const float cx = x + pad + font.width(shown.substr(0, c), scale);
        sb.draw(cx, ty, 2.0f, 8.0f * scale, color::kText);
    }
}

void TextField::drawInField(SpriteBatch& sb, const Font& font, float x, float y, float h,
                            bool focused, double time) const {
    // Smaller (was 1.5) and nudged up: the wider/taller mono font overflowed the input box and
    // sat low (S.: login fields "уменьшить и приподнять на 4 пх").
    const float scale = 1.25f;
    const float ty = y + (h - 8.0f * scale) * 0.5f - 4.0f;
    const std::string shown = password ? std::string(value.size(), '*') : value;
    font.draw(sb, x, ty, scale, color::kFieldText, shown);
    if (focused && std::fmod(time, 1.0) < 0.5) {
        const usize c = caret < shown.size() ? caret : shown.size();
        const float cx = x + font.width(shown.substr(0, c), scale);
        sb.draw(cx, ty - 1, 2.0f, 8.0f * scale + 2, color::kFieldText);
    }
}

} // namespace uaro::ui
