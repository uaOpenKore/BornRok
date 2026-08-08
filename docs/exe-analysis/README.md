# uaRO.exe — Algorithmic Reverse-Engineering (English)

A subsystem-by-subsystem algorithmic description of the original client
`winEXE/uaRO.exe` (PE32 x86, DirectDraw7 + Direct3D7, Miles audio, Bink intro),
produced from a full Ghidra decompile (`uaRO_decomp.c`, 11,379 functions).

All exe virtual addresses are quoted so any finding can be re-opened in Ghidra.
The running journal and coverage map live in [`PROGRESS.md`](PROGRESS.md).

## Subsystems

1. [Entry, WinMain, window & main loop](01-entry-winmain.md) — `WinMain 0x006555f0`
2. [WndProc, frame loop & mode state-machine](02-message-loop-and-frame.md) — `0x0059fda0`
3. [DirectDraw7/Direct3D7 render backend](03-directdraw-render-backend.md) — `0x00403f30`
4. [Input: DirectInput mouse + Win32 keyboard](04-directinput.md) — `0x00419b50`
5. [GRF virtual filesystem](05-grf-vfs.md) — mount `0x0050ccb0`, entry `0x005118b0`
6. [Sprite pipeline (.spr/.act)](06-sprite-spr-act.md) — `0x004385f0` / `0x00422aa0`
7. [World/map (RSW/GND/GAT/RSM)](07-map-rsw-gnd-gat.md) — scene `0x004f6ae0`
8. [Effect engine (CEffect + .str)](08-effect-engine.md) — factory `0x005c2630`
9. [Network & packet dispatch](09-network-packets.md) — dispatcher `0x00579900` (306 cases)
10. [UI windows & widgets](10-ui-windows-widgets.md) — CButton `0x004405f0`
11. [Audio (Miles) & Bink video](11-audio-bink.md) — `0x004214f0`
12. [Lua / lub VM (+ homun AI)](12-lua-vm.md) — AI vm `0x005a6ac0`
13. [Game states, session & login flow](13-game-state-session.md) — `0x0059fda0`

## High-level architecture

```
CRT → WinMain(0x006555f0)
  ├─ parse cmdline → g_mode (DAT_0074b560)
  ├─ anti-tamper, single-instance, timers, COM
  ├─ resNameTable + GRF VFS mount (DATA.INI)
  ├─ create window (WndProc 0x006541e0)
  ├─ init: audio(Miles), DirectDraw7/D3D7, DirectInput mouse
  ├─ intro Bink (openning.bik), load login.rsw
  └─ CModeMgr::Run (0x0059fda0)  ── the main loop ──
        while running && !quit:
          OnEnter(pending state)         // build mode object (login/char/world)
          Process()  = input → network(0x00579900) → update → render(D3D7) → present
     modes: intro → login → server/char select → char-create → in-game(map)
```

Cross-references to existing repo docs: `effect-ids-from-exe.md`, `ragexe-effect-map.md`,
`packet-audit.md`, `grf-lub-files-survey.md`, `rsw-map-effects-id44.md`, and the
port memories (audio backend, homun AI, cp949, etc.).
