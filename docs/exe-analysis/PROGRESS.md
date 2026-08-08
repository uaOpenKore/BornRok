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
| 5  | File / GRF virtual filesystem         | mount `0x0050ccb0`; entry `0x005118b0` | `05-grf-vfs.md` | ✅ |
| 6  | Sprite (.spr/.act) load + animation   | spr `0x004385f0`; act `0x00422aa0` | `06-sprite-spr-act.md` | ✅ |
| 7  | Map load (RSW/GND/GAT) + renderer     | scene `0x004f6ae0`; world `0x0059f9c0` | `07-map-rsw-gnd-gat.md` | ✅ |
| 8  | Effect engine (CEffect / .str)        | factory `0x005c2630`; table `DAT_006d9100` | `08-effect-engine.md` | ✅ |
| 9  | Network / packet dispatch             | dispatcher `0x00579900` (306 cases) | `09-network-packets.md` | ✅ |
| 10 | UI windows / widgets                  | CButton `0x004405f0`; root `DAT_0074aea0` | `10-ui-windows-widgets.md` | ✅ |
| 11 | Audio (Miles/WINMM) + Bink video      | init `0x004214f0`; AIL/mss32 | `11-audio-bink.md` | ✅ |
| 12 | Lua / lub script VM                   | AI vm `0x005a6ac0`; script `0x005a6b60` | `12-lua-vm.md` | ✅ |
| 13 | Game-state / session / login flow     | modes `0x0059fda0`; `0x005a49f0` | `13-game-state-session.md` | ✅ |

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
- **2026-08-09** — **Subsystem 5 DONE** → `05-grf-vfs.md`. Layered VFS: `DATA.INI [Data]`
  keys '9'..'0' enumerated by `0x0069422f` → each GRF mounted via `0x0050ccb0` (provider
  chain; GRF vtbl `PTR_FUN_0069c988`, loose-dir override vtbl `PTR_FUN_0069c97c`). GRF
  table load `0x00510fd0`; entry read `0x005118b0` = read→DES `0x005120b0`→zlib uncompress.
  resNameTable remap singleton `0x00512b70`/`DAT_0074bad8` (+alias `0x00513e80`). Paths
  cp949. NEXT: Subsystem 6 — .spr/.act sprite load + animation.
- **2026-08-09** — **Subsystem 6 DONE** → `06-sprite-spr-act.md`. .spr parse `0x004385f0`
  (magic 'SP' 0x5053; 256-RGBA palette at tail-0x400 → `this+0x110`; indexed frames, RLE
  `0x00439260` for v≥0x201, truecolor RGBA for v≥0x200; index→texture `0x004390c0`; HD
  downscale `0x004392f0`/`DAT_007735f4/f8`). .act parse `0x00422aa0` (magic 'AC' 0x4341;
  actions×dir → frames → layers{spr,offset,mirror,tint,scale,rot} + delay + anchor CP pts
  + sound idx). Anim: action=dir*N+motion, delay scaled by ASPD, billboard quads via D3D,
  dye via palette swap. Stream `0x0050d570`/seek `0x0050d620`.
  NEXT: Subsystem 7 — map RSW/GND/GAT load + world renderer (`0x004f6ae0`).
- **2026-08-09** — **Subsystem 7 DONE** → `07-map-rsw-gnd-gat.md`. Scene switch
  `0x004f6ae0` (destroy/construct scene; world ctor `0x0059f9c0` vtbl `PTR_FUN_0069f4dc`,
  alt `0x005582d0`). **Async** map request `0x004f6c40` (name `this+0x30`, flag
  `this+0x5c`) → explains loading screen. Server map-change `0x005a49f0` builds `"%s.rsw"`
  (map global `DAT_00771730`) + PingLog. RSW(world/light/objects)+GND(mesh/lightmap)+
  GAT(collision¼)+RSM(models) render order documented. NOTE: low-level byte parsers are
  world-scene vtable methods → enumerate in Subsystem 13; formats already in port
  (Rsw/Gnd/Gat/Rsm). NEXT: Subsystem 8 — effect engine (CEffect `0x5c2630` / .str).
- **2026-08-09** — **Subsystem 8 DONE** → `08-effect-engine.md`. CEffect factory
  `0x005c2630`(this,owner,effectId,x,y,z): stores id@`this+0x100`, per-id param table
  `DAT_006d9100[id]`@`this+0x114`, giant switch effectId→.str filename (defense/JobLvUP/
  angel/devil/melt/cart/sword/asum/ramadan…) else procedural fall-through. .str = STRM
  v0x94 layered keyframe billboards (pos/uv/rgba/angle/scale/blend), one-shot additive,
  body-anchored. 4 invocation channels (0x1F3/0x19B/skillId/0x8A). Cross-refs
  effect-ids-from-exe.md, ragexe-effect-map.md. NEXT: Subsystem 9 — network/packets.
- **2026-08-09** — **Subsystem 9 DONE** → `09-network-packets.md`. Winsock init
  `0x00418db0` (WSAStartup + **dynamic send/recv ptrs** `DAT_006ee6e4`/`DAT_006ee66c`
  from ws2_32, "Module Hooking Error" fallback stubs — anti-IAT-hook). Conn singleton
  `0x00419480`=`DAT_006ee670`, non-blocking TCP connect `0x00418af0`; 3 conns login→char→
  map. recv→frame(len table / 2-byte var)→dispatch. **Master ZC dispatcher `0x00579900`
  = 306-case switch** (called from `0x00559b00` loop). Pre-game dispatchers `0x00645650`
  (~108)/`0x006445f0`. Outgoing build `0x004192f0`/`0x004191b0`, ping 0x187. Optional
  packet obfuscation (off). Cross-ref uOK210/packet-audit. NEXT: Subsystem 10 — UI.
- **2026-08-09** — **Subsystem 10 DONE** → `10-ui-windows-widgets.md`. UI = code-built
  windows; CButton ctor `0x004405f0` (0xA4 obj, 3-bmp skin normal/_a/_b from 유저인터페이스
  via VFS). Widget set CEdit/CText/CList/CScroll/CWindow. Builders: login/char-create
  (arw_str/agi/vit/int), in-game windows (port already reimpl #39-#98/#136). UI root
  `DAT_0074aea0` = first-chance msg dispatch; Z-order top captures click; titlebar drag;
  modal blocks world. Strings via `0x00504fb0`. NEXT: Subsystem 11 — audio + Bink.
- **2026-08-09** — **Subsystem 11 DONE** → `11-audio-bink.md`. Backend = **Miles Sound
  System (AIL_*/mss32)** + mp3dec.asi. Init `0x004214f0`: AIL_startup, open digital driver
  (rate 22050/11025/8000, 16/8-bit, mono/stereo from settings `DAT_007731a8/b0/b8`), voice
  pool 48/32/16 by quality `DAT_007731b4`, 3D provider (positional SFX). SFX .wav→sample
  handle w/ 3D pos (ACT frame sound idx / effect wav / skill). BGM mp3 stream `0x004219c0`.
  Bink intro via BinkOpenDirectSound. Port replaces w/ SDL3+dr_libs / XAudio2.
  NEXT: Subsystem 12 — Lua/lub VM.
- **2026-08-09** — **Subsystem 12 DONE** → `12-lua-vm.md`. Embedded **Lua 5.0/5.1** (static;
  lua_getinfo/error/trace/LUA_PATH). Uses: (1) luafiles/.lub data tables (itemInfo,
  MonsterSkillInfo, etc.) into VM at startup; (2) **homun/merc AI**: create `0x005a6ac0`,
  load `0x005a6b60` (\AI\AI.lua / AI_M.lua, USER_AI overrides `DAT_00772ecc`/`f68`), per-tick
  `0x005a6c60` AI(id), init event vtbl+8(0x7d), errors AI_lua_error via `0x00405bf0`.
  Cross-ref homun-ai-lua-vm(#148). NEXT: Subsystem 13 — game-state/session/login flow
  (mode state machine `0x0059fda0`, per-mode vtables).
- **2026-08-09** — **Subsystem 13 DONE** → `13-game-state-session.md`. Session flow via
  mode state machine `0x0059fda0` (state `mgr[1]`, g_mode `DAT_0074b560`): intro/login.rsw
  → login (account send, char-list via `0x00645650`) → server/char select (slots as
  sprites; Make=char-create arw_* steppers) → map-login `0x72` via `0x004c9fc0` → world
  scene `0x0059f9c0` loads RSW/GND/GAT + spawn → in-game Process(input→move→dispatch
  `0x00579900`→render, ping `0x0059fe10`) → warp `0x005a49f0` → logout teardown `0x004f5790`.
  **ALL 13 SUBSYSTEMS COMPLETE.** Remaining depth (per-mode vtable bodies, per-packet
  handlers, low-level map parsers) is drill-down within these anchored subsystems.
- **2026-08-09** — **DEEP-DIVE: effects/particles/render** (S. request: understand how
  clouds/smoke/skills visualize) → `14-effects-particles-render-deep-dive.md`. Two families:
  .str (data) + procedural particles (code). Effect manager = scene+0x164 linked list
  (+0x168 count), common message method vtbl+8 (init 0x7D/stop 0x18/clear 0x19). Particle
  emitter factory `0x005c3670` → obj `operator_new(0xF9E4)`, ctor `0x00610e80` vtable
  `PTR_FUN_0069fbc8`, texture cache `0x0040c590`; **particle array at +0xF704, 0xB8 bytes
  each** (active/age/lifespan 40|30|25|∞/pos/vel/size/color). Coded dispatch switch
  `~0x005b9b88`→emit fns (cloud `0x005e08b0`, waterfall `0x005e0bc0`, rings, smoke).
  **Billboard blend** via device SetRenderState SRCBLEND 0x13/DESTBLEND 0x14: alpha (5,6)
  clouds/smoke, additive (2,2) glows/magic; Z-test on write-off; MODULATE stage0; camera-
  facing, owner-anchored one-shot. Clouds/smoke = cloud1/2/4/11.tga + smoke.tga emitters +
  RSW EF_ ids (44 smoke/45 firefly/165 sparkle). Named skills GrandCross `0x005ba4e2`/
  Asura `0x005db1b3`/SoulBreaker `0x005db80e`. Added as doc 14.
