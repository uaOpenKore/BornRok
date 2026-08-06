#pragma once
#include <string>

namespace uaro {

// Per-frame input/window snapshot, refreshed by Platform::pump(). Holds enough
// for the UI layer: typed text, the edit/navigation keys a text field needs, and
// the mouse. Persistent fields (mouse position, button-held) carry across frames;
// the per-frame fields (text, edge-triggered keys, pressed/released) are cleared
// at the top of every pump().
struct InputState {
    bool quit = false;     // genuine OS quit (SDL_EVENT_QUIT / SIGTERM); ends the loop
    bool closeRequested = false;  // window close (Alt+F4 / X) this frame — scenes decide
    bool escape = false;   // Esc pressed this frame
    bool resized = false;  // framebuffer size changed this frame
    int width = 0;         // current drawable width  (pixels)
    int height = 0;        // current drawable height (pixels)
    // App lifecycle (Android): appSuspended LEVEL-persists while backgrounded (main loop stops
    // rendering to the destroyed surface); appResumed is a one-frame EDGE the loop uses to re-acquire
    // the render surface. Never set on desktop. (S. 2026-08-06: black screen after minimise -> restore.)
    bool appSuspended = false;
    bool appResumed = false;

    // Text typed this frame (UTF-8, may be several characters with an IME).
    std::string text;

    // Edge-triggered keys: true only on the frame they are pressed (or repeated).
    bool keyBackspace = false;
    bool keyEnter = false;
    bool keyTab = false;
    bool keyDelete = false;
    bool keyLeft = false;
    bool keyRight = false;
    bool keyUp = false;
    bool keyDown = false;
    // Held state of the arrow keys (persist across frames; for smooth tank-style navigation). Set on
    // key-down, cleared on key-up -- unlike the per-frame edges above which only pulse on key-repeat.
    bool leftHeld = false;
    bool rightHeld = false;
    bool upHeld = false;
    bool downHeld = false;
    bool keyHome = false;
    bool keyEnd = false;
    bool keyInsert = false;  // Insert pressed this frame (in-game: toggle sit/stand)
    bool shift = false;  // a shift modifier is held this frame
    bool ctrl = false;   // a ctrl modifier is held this frame
    // Window hotkeys (the classic RO Alt+letter keymap), edge-triggered. Set by the
    // platform only when Alt is held, so plain typing into the chat box is unaffected.
    bool keyInventory = false;  // Alt+E -> inventory window
    bool keyEquip = false;      // Alt+Q -> equipment window
    bool keyStatus = false;     // Alt+A -> status/stats window
    bool keySkills = false;     // Alt+S -> skill window
    bool keyBasicInfo = false;  // Alt+V -> cycle the BasicInfo panel (full/short/collapsed)
    bool keyCart = false;       // Alt+W -> pushcart window (merchant cart)
    bool keyBinds = false;      // Alt+M -> open the Shortcut List (bind editor) window
    bool keyFriends = false;    // Alt+H -> friend list window (#78)
    bool keyChatRoom = false;   // Alt+C -> create chat room window (#78)
    bool keyGuild = false;      // Alt+G -> guild window (#78)
    bool keyBuyStore = false;   // Alt+B -> buying-store window (#69)
    bool keyParty = false;      // Alt+Z -> friend window on the Party tab (#78)
    bool keyQuest = false;      // Alt+U -> quest journal window (#136)
    bool keyChatToggle = false; // Alt+X -> collapse/expand chat (S.)
    int altDigit = -1;          // Alt+1..9 -> 1..9, Alt+0 -> 0 (fires that chat bind); -1 = none
    int numDigit = -1;          // numpad 1..9/0 (no Alt) -> fires that bind slot (S.); -1 = none

    // Quick-slot (hotkey) bar: a physical key pressed this frame maps to a (row, col), so any
    // keyboard layout works. row 0 = F1-F12, 1 = QWERTY top, 2 = home, 3 = bottom, 4 = number row.
    // (-1,-1) when no hotbar key fired. The game ignores it while a text field (chat) is focused.
    int hotkeyRow = -1;
    int hotkeyCol = -1;

    // Mouse, in framebuffer pixels (same space as SpriteBatch::begin()). At UI-scale > 1 the game
    // rescales mouseX/mouseY into LOGICAL space (physical / uiScale) once per frame; mousePhysX/Y
    // keep the untouched physical value so that rescale derives from a clean source and never
    // compounds (#134). The platform sets all four to the physical position.
    int mouseX = 0;
    int mouseY = 0;
    int mousePhysX = 0;  // physical framebuffer x (never rescaled by the game)
    int mousePhysY = 0;
    bool mouseDown = false;      // left button currently held (persists)
    bool mousePressed = false;   // left button went down this frame
    bool mouseDoubleClick = false;  // left button double-click this frame (SDL clicks==2)
    bool mouseReleased = false;  // left button went up this frame
    bool rightDown = false;      // right button currently held (persists)
    int mouseDX = 0, mouseDY = 0;  // relative mouse motion this frame (pixels)
    float wheel = 0.0f;          // mouse-wheel delta this frame (+ = up/away)

    // Gamepad (SDL_Gamepad), refreshed each pump (#102/#115). Buttons are POSITION-based
    // (SDL South/East/West/North = bottom/right/left/top) so binds are identical on every controller;
    // only the on-screen glyph differs by pad type. Sticks are deadzoned to [-1,1] with up = +y.
    // Face + d-pad + stick-clicks + Start are EDGE (pulse the frame pressed); shoulders are HELD
    // (skill panels are hold-to-activate); *Held mirror the face/dpad for repeat navigation.
    enum class PadType { None, Xbox, PlayStation, Nintendo, Generic };
    struct Gamepad {
        bool connected = false;
        PadType type = PadType::None;
        float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;  // sticks, deadzoned, up = +y
        // Face buttons by POSITION (edge this frame): south=bottom, east=right, west=left, north=top.
        bool south = false, east = false, west = false, north = false;
        bool southHeld = false, eastHeld = false, westHeld = false, northHeld = false;
        // D-pad (edge) + held.
        bool dpadUp = false, dpadDown = false, dpadLeft = false, dpadRight = false;
        bool dpadUpHeld = false, dpadDownHeld = false, dpadLeftHeld = false, dpadRightHeld = false;
        // Shoulders: bumpers L1/R1 + triggers L2/R2 (HELD; trigger past threshold).
        bool l1 = false, r1 = false, l2 = false, r2 = false;
        // Stick clicks (edge) + Start/Menu (edge) + Back/Select (edge, = Camera Lock toggle).
        bool l3 = false, r3 = false, start = false, back = false;
        // PS4 / Steam Deck touchpad-as-mouse: absolute position in [0,1] and finger-down.
        bool touchActive = false;   // a finger is on the touchpad this frame
        float touchX = 0.0f, touchY = 0.0f;
        bool touchPress = false;    // touchpad physically clicked this frame (edge) = LMB
    };
    Gamepad pad;

    // Main-screen multi-touch (touchscreen game mode #102/#114). Up to 2 fingers (2nd = pinch-zoom).
    // Positions/deltas are in framebuffer pixels; down/up are per-finger EDGES this frame. `present`
    // is set once at startup if the device reports a touchscreen (drives the auto-enable of touch mode).
    struct ScreenTouch {
        bool present = false;   // a touch device exists (SDL_GetTouchDevices non-empty)
        int count = 0;          // fingers currently down
        struct Finger {
            long long id = -1;  // SDL finger id (-1 = slot free)
            float x = 0.0f, y = 0.0f;    // current position (fb pixels)
            float x0 = 0.0f, y0 = 0.0f;  // position where this finger first went down (for tap/drag/long-press)
            float dx = 0.0f, dy = 0.0f;  // motion this frame (fb pixels)
            double downTime = 0.0;       // time (s) the finger went down
            bool down = false;  // went down this frame (edge)
            bool up = false;    // lifted this frame (edge)
            bool held = false;  // currently on screen
        };
        Finger f[2];            // f[0] = primary, f[1] = secondary
    };
    ScreenTouch touch;

    // True when a point lies inside the given rect (UI hit-testing helper).
    bool hit(float x, float y, float w, float h) const {
        return mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + h;
    }
};

} // namespace uaro
