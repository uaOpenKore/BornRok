# Gamepad Controls

The client supports full gamepad play (Xbox, PlayStation 4, Nintendo Switch, Steam Deck and compatible pads). Buttons are bound by POSITION, so the layout is identical on every controller — the on-screen glyphs match your pad. Below, A/B/X/Y = bottom/right/left/top face buttons (on PS: ✕/○/□/△).

## Entering and leaving gamepad mode

- If a pad is connected at launch, the client enters gamepad mode **automatically**.
- If a pad is connected during play, a **"Switch to gamepad control mode?"** prompt appears (A — yes, B — no).
- To leave: manually via **Settings → General → Control: Keyboard/Gamepad**, or the mode turns off by itself once all pads are disconnected.
- Mouse and keyboard keep working in gamepad mode (handy for chat).

A strip at the bottom of the screen lists the active modes (e.g. `Standard · Camera`).

## Modes (switch with the D-pad)

The D-pad selects the base mode; pressing the same direction again returns to **Standard**:

- **D-pad ↑** — Menu mode
- **D-pad ↓** — target Monsters
- **D-pad ←** — target NPCs
- **D-pad →** — target Players
- **L3 (left stick click)** — Mouse mode (cursor)
- **R3 (right stick click)** — Camera mode

Mouse/Camera modes and the skill panels are overlays and can be active together.

## Standard mode (movement and combat)

**Movement (tank-style):**
- Left stick forward/back — walk along your facing; back moonwalks without turning.
- Left stick left/right — strafe (slide sideways, facing unchanged).
- Right stick left/right — turn the character 45°.
- Right stick up/down — tilt the camera.

**Face buttons (no shoulder held):**
- A (bottom) — attack the nearest target.
- Y (top) — sit / stand.
- X (left) — toggle auto-attack.
- B (right) — pick up an item.

## Camera

- By default the camera sits behind the character (with Camera Lock on it follows your turns). Camera Lock turns on automatically when you enter gamepad mode.
- **R3** enters Camera mode: the right stick orbits the camera (works even with Camera Lock on), up/down zooms.
- **Back (View / Share / Select)** toggles Camera Lock on/off. The status (`Cam Lock` / `Cam Free`) is shown in the bottom mode strip.
- With Camera Lock off, movement/turns do not move the camera — it stays where you left it.

## Targeting modes (Monsters / NPCs / Players)

The selected target is outlined with a magenta frame.

- B (right) — select the nearest target of that type.
- Right stick — pick a target by direction relative to the current one.
- A (bottom) — action on the target (attack a monster / talk to an NPC).
- X (left) — context menu (on a player: deal / chat / friend).
- Y (top) — close the context menu; if none is open, next target.

The camera auto-centres on the selected target.

## Quick-slot bar (skills and items)

In gamepad mode the bar is 2 rows of 8 slots (4 — gap — 4 = 4 blocks of 4). Each slot shows your pad's button glyph.

**Hold a shoulder → the face buttons fire that block's slots:**
- L2 — row 1, block 1  R2 — row 1, block 2
- L1 — row 2, block 1  R1 — row 2, block 2
- Within a block: A=slot 1, B=slot 2, X=slot 3, Y=slot 4.

**Assign a slot:** context menu on a skill/item → "Slot" → pick the group tab and slot.

## Menu mode (window navigation)

Opened with **D-pad ↑** or the **Start (≡)** button (Start opens the ESC menu and enables menu mode). The focused element is outlined with a magenta frame.

- Entering menu mode focuses the "Status" button in the main menu.
- Right stick — move the focus between elements.
- A (bottom) — select / activate (open a window).
- Y (top) — close the window.
- X (left) — back.
- B (right) — the element's context menu.
- Left stick — move the window around the screen.
- L1/R1 — tabs within the window.
- L2/R2 — switch between open windows.

## Cursor

- Mouse mode (L3): the left stick drives the cursor. A=LMB, X=RMB.
- PS4: the touchpad drives the cursor in any mode (touchpad press = LMB).
- Steam Deck: the left trackpad is the cursor, the right trackpad is the camera.

## Text input (on-screen keyboard)

When a text field is focused in gamepad mode (login account/password, in-game chat) an on-screen QWERTY appears at the bottom:

- **D-pad** — move the highlight, **A** — press the key.
- **X** — Shift (next letter uppercase), **B** — space, **Y** — backspace.
- **123 / abc** key — toggle the symbol/number page, **OK** — Enter (send / confirm).
- On login, **Start** switches between the account and password fields.

While the keyboard is up it owns the pad, so the d-pad/face buttons type instead of driving the game.

## Item quantity (drop / move)

When you drop or move a stack, a quantity prompt appears:

- **D-pad Left / Down** — decrease, **D-pad Right / Up** — increase.
- **A** — OK, **Y** — Cancel.

## Login and character select

A pad alone is enough to get into the game:

- **Login:** D-pad picks a server, **A / Start** proceeds, **B** exits; type the account and password with the on-screen keyboard.
- **Character select:** D-pad Left/Right walks the slots, **A** enters the game (or opens creation on an empty slot), **X** deletes, **B / Start** goes back. New characters start with a balanced 5/5/5 stat spread.

## GamePad Setup (stick axis inversion)

Some controllers (especially in DirectInput mode) report a stick axis reversed. Open **ESC → GamePad Setup** and toggle the inversion per axis:

- Left stick — Invert X / Invert Y
- Right stick — Invert X / Invert Y

The setting is saved and applied everywhere (movement, camera, cursor, menu). The button appears only in gamepad mode.

The same panel holds **Swap strafe/turn**, **Rotate sensitivity** and **Vibration**.

## Vibration

In **GamePad Setup** each event has its own toggle (all on by default):

- Taking damage — short buzz
- Killing a mob/player — normal buzz
- Base/job level-up — long buzz
- Menu action confirm — light tick
- Character death — double tick
- Critical hit with your own attack — single tick

## ESC menu

The **Start (≡)** button opens/closes the ESC menu in any mode. In the Help panel the d-pad up/down scrolls the page.
