# Subsystem 2 — WndProc, main frame loop & mode state-machine

Completes the open question from Subsystem 1: how the per-frame loop actually runs.

## Frame clock

`FUN_00421e60(fps)` @ `0x00421e60` configures the frame limiter:
`DAT_0070ec0c = timeGetTime()` (last-frame stamp) and
`DAT_0070ec10 = 1000/fps` (frame interval in ms). WinMain calls it with `0x3c`
→ **16 ms ≈ 60 Hz**. Each mode's per-frame handler paces itself against these globals.

## Main loop — `CModeMgr::Run` @ `0x0059fda0` (`FUN_0059fda0`)

The real game loop is **not** in WinMain; it is a virtual state-machine driver:

```c
running = mgr[5];
while (running != 0 && DAT_007735e0 == 0) {      // DAT_007735e0 = global QUIT flag
    if (DAT_00727f48 == 0 && mgr[3] != -1) {     // a state change is pending & allowed
        mgr[1] = mgr[3];                         // curState = nextState
        mgr[2] = 0;                              // reset frame counter
        mgr[3] = -1;                             // clear request
        (*mgr.vtbl[0x18])(newState);             // OnEnterState (build the mode object)
    }
    (*mgr.vtbl[0x10])();                         // Process(): input → update → render → present
    running = mgr[5];
    mgr[2]++;                                     // frame counter
}
FUN_004f5790(0);                                 // teardown current mode
```

- `mgr` (this) fields: `[1]`=current state id, `[2]`=frame counter, `[3]`=requested
  next state (`-1` = none), `[5]`=running flag. `DAT_00727f48` gates transitions
  (non-zero = "hold", e.g. during an async load / blocking dialog).
- **Quit** is signalled by `DAT_007735e0` (set to 1 by `WM_CLOSE` in the WndProc).
- Each **mode** (login / char-select / char-create / in-game / …) is a polymorphic
  object; the actual input+update+render work lives behind `vtbl[0x10]` (`Process`) and
  `vtbl[0x18]` (`OnEnter`). Those per-mode vtables are the subject of Subsystem 13.
- The window message pump is drained inside each mode's `Process` (peek-idle style),
  so `Run` is a tight loop and the WndProc is invoked re-entrantly from `Process`.

A parallel loop `FUN_00559b00` @ `0x00559b00` uses the same `DAT_007735e0` guard for a
blocking sub-flow (e.g. a modal loading screen).

### Per-frame keep-alive — `FUN_0059fe10` @ `0x0059fe10`

Called each frame while connected (state ∈ {7,8,0xb,0x13}). Every 12 s it sends the
**char-server ping** (`"CharServer Ping."`, packet **0x187**) via the packet builder
`FUN_00419480`/`FUN_004192f0`/`FUN_004191b0` (see Subsystem 9). `timeGetTime` drives the
12000 ms interval stored at `this+0x1364`.

## WndProc `FUN_006541e0` @ `0x006541e0` — full message map

The proc **first offers every message to the IME/input manager** `DAT_0074aea0`
(`vtbl[0x10]` filter). If the manager consumes it (`param_4._3_1_ != 0`) the proc runs
the Korean IME candidate-list path (`FUN_006718ee`, `FUN_00430f00`, `FUN_00424b50`,
`FUN_004d42d0(0x33)`) and returns. Otherwise the switch runs:

| msg | id | handling |
|-----|----|----|
| WM_CREATE / WM_DESTROY | 1 / 2 | return 0 |
| WM_MOVE | 3 | if not-fullscreen & foreground → `FUN_00404bc0(1)` (recompute DirectDraw clip rect) |
| WM_ACTIVATE | 6 | fullscreen: minimise/restore handling (`ShowWindow`, `DAT_007735e8`) |
| WM_SETFOCUS | 7 | `FUN_0050b640(0)` — resume (Bink/audio) if `DAT_007735d0` |
| WM_KILLFOCUS | 8 | `FUN_0050b640(1)` — pause; `(*DAT_0074ba90)()` |
| WM_PAINT | 0xf | `ValidateRect` (rendering owns the surface), return 0 |
| WM_CLOSE | 0x10 | **`DAT_007735e0 = 1` (quit)**, `(*DAT_0074baa4)()`, `FUN_0050e4a0()` |
| WM_SYSCOMMAND | 0x112 | block screensaver (`0xF140`), monitor-power, maximise; allow move (`0xF010`) |
| WM_ACTIVATEAPP | 0x1C | foreground↔background priority + `FUN_00419e20` (input capture) |
| WM_IME_* | 0x281 | IME context |
| input msgs (≥0x113: mouse/keyboard/wheel) | — | routed into the input subsystem (Subsystem 4) |
| default | — | `DefWindowProcA` |

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| Frame-limiter config | `0x00421e60` | sets 60 Hz interval `DAT_0070ec10` |
| **Main loop** (CModeMgr::Run) | `0x0059fda0` | state machine, quit on `DAT_007735e0` |
| Modal sub-loop | `0x00559b00` | blocking loading flow |
| Per-frame keep-alive | `0x0059fe10` | char-server ping 0x187 / 12 s |
| Mode teardown | `0x004f5790` | destroy current mode |
| QUIT flag | `DAT_007735e0` | set by WM_CLOSE |
| Transition gate | `DAT_00727f48` | hold state changes |
| IME/input mgr | `DAT_0074aea0` | first-chance message filter |
