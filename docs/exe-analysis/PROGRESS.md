# uaRO.exe — Reverse-Engineering Progress Log

**Purpose:** resumable master log for the full algorithmic disassembly of `winEXE/uaRO.exe`.
Records every step, subsystem, and key address so the work can be **paused and continued**
from the exact point it stopped. The algorithmic descriptions themselves live in the
per-subsystem `docs/exe-analysis/<subsystem>.md` files (English). This file is the index +
running journal (steps/addresses/status).

## Target binary

- File: `winEXE/uaRO.exe`
- Format: PE32, x86 (32-bit), Windows GUI subsystem
- Size: 3,072,086 bytes; `.text` ≈ 2.6 MB, stripped / unpacked (no packer)
- Notable imports: `DDRAW.DLL` (DirectDraw), `DINPUT.DLL` (DirectInput), `binkw32.dll`
  (Bink video), `ijl15.dll` (Intel JPEG Library), plus standard MSVCP/MSVCRT/WINMM.
- Entry-point language: MSVC (RTTI + vftables present) — classic Gravity RO client codebase.
- Known anchor so far: `CEffect` dispatch @ `0x5c2630` (see `docs/effect-ids-from-exe.md`).

## Decompilation source

- Tool: **Ghidra 11.3** (owner's machine), Java script `DumpDecomp.java` → `uaRO_decomp.c`
  (full decompile of every function, header `/* ==== <addr>  <name> ==== */` per function).
- Owner is preparing `uaRO_decomp.c` and will deliver it as a zip. **Not yet received.**

## Workflow (incremental, per owner's directive 2026-08-09)

1. Owner delivers `uaRO_decomp.c` (Ghidra full decompile).
2. Work subsystem-by-subsystem. For each subsystem:
   - locate its functions in the decompile (by address / vftable / string xref),
   - write an English algorithmic description to `docs/exe-analysis/<subsystem>.md`,
   - record the covered address ranges + status in the table below,
   - post a staged report to the owner (Discord).
3. Keep this file updated **after every step** so an interrupted run can resume.

## Subsystem coverage map

Status: ⬜ not started · 🟨 in progress · ✅ done

| # | Subsystem | Key anchors / addresses | Doc | Status |
|---|-----------|-------------------------|-----|--------|
| 1  | Entry / WinMain / init order          | `0x006555f0`     | `01-entry-winmain.md` | ✅ |
| 2  | Window + message loop (WndProc)       | WndProc `0x006541e0`; **main loop `0x0059fda0`** | `02-message-loop-and-frame.md` | ✅ |
| 3  | DirectDraw render backend             | `0x00403f30` init; device `DAT_006ee3d8` | `03-directdraw-render-backend.md` | ✅ |
| 4  | DirectInput (keyboard/mouse)          | mouse `0x00419b50`; capture `0x00419e20` | `04-directinput.md` | ✅ |
| 5  | File / GRF virtual filesystem         | `0x00512b70` resNameTable | —   | 🟨 |
| 6  | Sprite (.spr/.act) load + animation   | (tbd)            | —   | ⬜ |
| 7  | Map load (RSW/GND/GAT) + renderer     | (tbd)            | —   | ⬜ |
| 8  | Effect engine (CEffect / .str)        | `0x5c2630`       | `effect-ids-from-exe.md` | 🟨 |
| 9  | Network / packet dispatch             | (tbd)            | —   | ⬜ |
| 10 | UI windows / widgets                  | (tbd)            | —   | ⬜ |
| 11 | Audio (Miles/WINMM) + Bink video      | binkw32          | —   | ⬜ |
| 12 | Lua / lub script VM                   | (tbd)            | —   | ⬜ |
| 13 | Game-state / session / login flow     | (tbd)            | —   | ⬜ |

## Journal

- **2026-08-09** — Set up Ghidra on owner's box. Ghidra 11.3 dropped bundled Jython →
  `.py` scripts need PyGhidra + system Python (absent). Switched to Java GhidraScript
  `DumpDecomp.java` (no Python). Import settled: PE / `x86:LE:32:default:windows`.
  Awaiting `uaRO_decomp.c` from full-program decompile. Log scaffold created.
- **2026-08-09** — Received `uaRO_decomp.c` (14.9 MB, 548,684 lines, **11,379 functions**),
  stored at `winEXE/decomp/` (git-ignored). ~7,444 auto-named `FUN_`, rest are import/CRT
  thunks; navigation is by API calls + string xrefs (no RTTI class names recovered).
  **Subsystem 1 DONE** → `01-entry-winmain.md`: entry→WinMain(`0x006555f0`), cmdline/mode
  parse (g_mode `DAT_0074b560`), anti-tamper self-relaunch (`0x0067407e`), single-instance
  mutex + "Surface" checksum trip-wire, timer-res raise, COM, OS gate, resNameTable load,
  window create (`0x00654b60`, WndProc `0x006541e0`), audio (`0x004214f0`), D3D/GRF init
  (`0x00403f30`), subsystem-init batch, intro Bink (`0x0050b740`/`0x0050b390`), login.rsw
  load (`0x004f6ae0`), shutdown drain. OPEN: exact per-frame loop mechanism → Subsystem 2.
  NEXT: Subsystem 2 — full WndProc switch + frame-loop/timer model.
- **2026-08-09** — **Subsystem 2 DONE** → `02-message-loop-and-frame.md`. Resolved the
  open question: main loop is the **mode state-machine `CModeMgr::Run` @ `0x0059fda0`**
  (`while(running && !DAT_007735e0){ enter pending state via vtbl[0x18]; Process via
  vtbl[0x10]; frame++ }`), quit flag `DAT_007735e0` set by WM_CLOSE. Frame clock 60 Hz
  via `0x00421e60` (`DAT_0070ec10`=16 ms). Per-frame keep-alive `0x0059fe10` sends
  char-server ping (0x187) every 12 s. Full WndProc `0x006541e0` message map documented
  (IME first-chance filter `DAT_0074aea0`, WM_CLOSE→quit, focus→Bink pause, syscmd
  screensaver block). Per-mode `Process`/`OnEnter` vtables deferred to Subsystem 13.
  NEXT: Subsystem 3 — DirectDraw/D3D backend (init `0x00403f30`, device `DAT_006ee3d8`).
- **2026-08-09** — **Subsystem 3 DONE** → `03-directdraw-render-backend.md`. Backend =
  **DirectDraw7 + Direct3D7 immediate mode** (`DirectDrawCreateEx`, `IID_IDirectDraw7`
  `DAT_006a20dc`). CRenderDevice `DAT_006ee3d8`: `[0xf]`=D3D7 device, `[0x24/0x28]`=
  viewport, `[0x75]`=fullscreen, `[0x77]`=IDirectDraw7. Create path
  `0x00403f30`→`0x00404260`→ fullscreen `0x00404570` / windowed clipper `0x00404450` /
  surfaces+device `0x004049f0` (primary+back flip chain + Z-buffer). Default FF pipeline:
  2 texture stages, stage0 MODULATE(tex×diffuse) bilinear, stage1 lightmap, TFACTOR.
  DrawPrimitive submission deferred to sprite(6)/map(7) subsystems.
  NEXT: Subsystem 4 — DirectInput (keyboard/mouse), then 5 — GRF VFS.
- **2026-08-09** — **Subsystem 4 DONE** → `04-directinput.md`. Mouse = **DirectInput7**
  (CMouse `0x00419b50`: DirectInputCreateA v0x700, CreateDevice GUID_SysMouse
  `DAT_006a109c`, c_dfDIMouse `DAT_0069fe84`, coop FOREGROUND|NONEXCLUSIVE, Acquire;
  virtual cursor `this[5/6]` clamped, swap-button `SM_SWAPBUTTON`; global `DAT_006ee6f4`).
  Focus capture toggle `0x00419e20` (Acquire/Unacquire on WM_ACTIVATEAPP). Keyboard =
  Win32 WM_KEY*/CHAR via IME/input mgr `DAT_0074aea0` (no DI keyboard device).
  NEXT: Subsystem 5 — GRF virtual filesystem (resNameTable `0x00512b70`, archive mount).
