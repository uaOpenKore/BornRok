# Subsystem 13 — Game states, session flow & login

This ties the pieces together: the mode **state machine** (Subsystem 2) drives the
session from the intro through login, character selection, and into the game world, each
"mode" being a polymorphic object whose `Process` (`vtbl+0x10`) and `OnEnter` (`vtbl+0x18`)
implement that screen.

## State machine recap

`CModeMgr::Run` @ `0x0059fda0` loops while `mgr[5]` (running) and `!DAT_007735e0` (quit).
`mgr[1]` = current state id, `mgr[3]` = requested next state. On a transition it calls
`OnEnter(newState)` (`vtbl+0x18`) which **constructs the mode object**; each frame it calls
`Process` (`vtbl+0x10`). The launch **`g_mode`** (`DAT_0074b560`, from the command line,
Subsystem 1) picks the initial state and skips/forces some screens.

Scene constructors seen: world/in-game scene `FUN_0059f9c0` @ `0x0059f9c0`
(vtable `PTR_FUN_0069f4dc`), pre-game scene `FUN_005582d0` @ `0x005582d0`. The connection
phase ids `{7, 8, 0xB, 0x13}` mark "socket connected" states (used by the keep-alive,
Subsystem 2).

## Session flow

1. **Intro** — play `openning.bik` (Subsystem 1/11), load `login.rsw` as the backdrop.
2. **Login** — the login window (Subsystem 10) collects id/password; on *Connect* the
   client opens the **login-server** TCP connection (Subsystem 9) and sends the account
   packet. The login server replies with the **char-server list** (dispatchers
   `0x00645650`/`0x006445f0`).
3. **Server select** (multi-server builds) — pick a char-server; connect to it.
4. **Character select** — the char-server sends the **character list**; the char-select
   mode renders the slots (each a live sprite, Subsystem 6). Actions:
   - **Select** → request enter-game → receive the **map name + zone-server ip:port**.
   - **Make** → character-create mode (name, stats via the `arw_*` steppers, appearance).
   - **Delete** → delete flow.
5. **Map / zone login** — connect to the **map-server**, send map-login (`0x72`
   `CZ_ENTER`-class request built via `FUN_004c9fc0` / the connection builder), receive the
   accept + starting position; the world scene (`0x0059f9c0`) loads the RSW/GND/GAT
   (Subsystem 7) behind the loading screen and spawns the player.
6. **In-game** — the world mode's `Process` runs input → movement/pathfinding (GAT) →
   network dispatch (`0x00579900`, 306 cases) → actor/effect update → render. Keep-alive
   ping `0x187` every 12 s (`0x0059fe10`).
7. **Map change / warp** — server sends a change-map; `0x005a49f0` re-requests
   `"<map>.rsw"` (async, Subsystem 7) and reconnects to the new zone if needed.
8. **Logout / disconnect** — tears down the mode (`0x004f5790`), returns to char-select or
   login; `WM_CLOSE` sets `DAT_007735e0` and the loop exits to WinMain shutdown.

## Session globals

| Global | Meaning |
|--------|---------|
| `DAT_0074b560` | launch mode `g_mode` (cmdline) |
| `DAT_007735e0` | quit flag |
| `DAT_00727f48` | transition hold gate |
| `DAT_00771730` | current map name |
| `DAT_00771f7c` | account/char id (echoed in ping 0x187) |
| `DAT_0074b57c` | active connection/session object |

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| Mode state machine | `0x0059fda0` | Run loop |
| World scene ctor | `0x0059f9c0` | in-game mode |
| Pre-game scene ctor | `0x005582d0` | login/char mode |
| Mode teardown | `0x004f5790` | destroy mode |
| Map-change request | `0x005a49f0` | warp/zone change |
| Packet builder (typed) | `0x004c9fc0` | e.g. `0x72` map-login |
| Keep-alive | `0x0059fe10` | ping `0x187` |

> The exhaustive per-mode `Process`/`OnEnter` bodies (login, char-select, char-make,
> in-game) are large virtual methods reached through the mode vtables; this file maps the
> flow and entry points. The BornRok port already reproduces the whole session
> (login/char-select/create/in-game) — see the completed UI/flow tasks — so these serve as
> the behavioural reference.
