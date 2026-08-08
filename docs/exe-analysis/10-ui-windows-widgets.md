# Subsystem 10 — UI windows & widgets

The client UI is **code-built** (not data-driven): each window is a C++ builder function
that instantiates widget objects and positions them, using **skin bitmaps** loaded from the
`유저인터페이스\` (UI) folder through the VFS (Subsystem 5). Widgets draw as 2D textured
quads over the world through the D3D device (Subsystem 3).

## Widget model

- **CButton** — 0xA4-byte object, ctor `FUN_004405f0` @ `0x004405f0`. Each button takes a
  **three-bitmap skin triplet**: `<name>.bmp` (normal), `<name>_a.bmp` (hover),
  `<name>_b.bmp` (pressed) — e.g. `btn_ok/btn_ok_a/btn_ok_b`,
  `btn_cancel*`, `login_interface\btn_exit*`. The button tracks hover/press state from
  the mouse (Subsystem 4) and fires a callback on click.
- Other widget classes follow the same pattern: **CEditControl** (text input, IME-aware),
  **CTextView / CLabel**, **CListBox**, **CScrollBar**, **CCheckBox**, **CWindow**
  (draggable frame + titlebar). Each is `operator_new`'d in the window builder and added to
  the window's child list.
- Skins may be `.bmp` (indexed/truecolor) or, in HD content, `.tga`/`.png` — resolved by
  the same loader that prefers PNG (port docs `hi-res-textures`).

## Window builders (examples)

- **Login window** (`~0x0128xxx`/`0x75xxx` region): builds the id/password edit boxes and
  the `login_interface\btn_exit`, connect, and OK/cancel buttons.
- **Char-create**: stat arrows `login_interface\arw_str0/arw_agi0/arw_vit0/arw_int0/…`
  (the `arw_*` up/down step controls), name field, appearance nav.
- In-game windows (inventory, equip, stats, skills, storage, NPC dialog, party/guild,
  quest, minimap, hotbar) are each their own builder — the port re-implemented all of
  these (tasks #39–#98, #136), so the exe builders are the reference for exact skins,
  slot rects and button ids.

## Window manager / input routing

- A **UI root manager** owns the open windows and receives the **first-chance** window
  messages from the WndProc via `DAT_0074aea0` (`vtbl+0x10`, Subsystem 2) — it dispatches
  mouse move/click/drag and keyboard/IME to the focused widget before the world gets them.
- **Z-order**: windows are stacked; the top-most window under the cursor **captures the
  click** and raises to front; clicks that land on a window must **not** leak to the world
  (this is exactly the behaviour the port had to reproduce — memory
  `all-windows-draggable-and-block-clicks`, tasks #42/#82/#91/#96/#97).
- **Dragging**: `CWindow` moves by its titlebar; modal dialogs (NPC, message box) block
  the world entirely.

## Text / fonts / i18n

- Localised UI strings come from the string table via `FUN_00504fb0(id)` (Subsystem 1);
  message boxes and prompts use it (`FUN_004f6cb0` / `FUN_004f6ce0` wrappers).
- Text is rendered with GDI-baked font glyph textures; Korean is EUC-KR, and the server
  chat is cp1251 on this fork (port memory `Cyrillic … cp1251`).

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| CButton ctor | `0x004405f0` | 0xA4 obj, 3-state bmp skin |
| Message-box/dialog builder | `~0x0128xxx` | btn_ok/cancel/exit layout |
| String-table lookup | `0x00504fb0` | localised UI text |
| MessageBox wrappers | `0x004f6cb0` / `0x004f6ce0` | error/info popups |
| UI/input root | `DAT_0074aea0` | first-chance message dispatch |

> The full per-window builder catalogue (hundreds of functions) is best enumerated on
> demand against a specific window; this file establishes the shared widget/skin/manager
> model. The BornRok port already re-implements the window set (see the many completed
> UI tasks), so exe builders serve as the pixel-accurate reference.
