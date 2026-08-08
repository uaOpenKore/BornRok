# Subsystem 1 — Entry, WinMain, Window & Main Loop

Scope: process entry, command-line/mode parsing, anti-tamper, one-time subsystem
initialisation order, window class + `WndProc`, intro video, and shutdown.
All addresses are the exe's virtual addresses (from the Ghidra decompile).

## Entry point

Standard MSVC C-runtime startup. The CRT stub sets up the heap/locale/exception
machinery and calls **`WinMain` @ `0x006555f0`** (`FUN_006555f0`) with the usual
`(hInstance, hPrevInstance, lpCmdLine, nCmdShow)`.

## WinMain @ 0x006555f0 — algorithm

1. **Install crash handler** — `SetUnhandledExceptionFilter(0x004e8890)`.

2. **Parse command line (`lpCmdLine`) for the launch "mode".** `strstr` scans for the
   classic Gravity mode tokens, each wrapped in `0x01` bytes:
   `\x01rag\x01`, `\x01sak\x01`, `\x01gld\x01`, `\x01eve\x01`, `\x01pvpsak\x01`,
   `\x01pksak\x01`, `\x01sak2\x01`, `\x01rag2\x01`. The selected mode is stored in the
   global **`DAT_0074b560` (= g_mode)**, which gates most later behaviour
   (0=normal, 2, 3, 4, 6, 8, 9, 0xc=Brazil, 0xe, …).
   - Also scans for `-account-` (pre-filled account name → copied into the clientinfo
     path buffer `local_118`) and, in `pvpsak` mode, substitutes
     `psakclientinfo.xml` for the default `clientinfo`.

3. **Load clientinfo** — `FUN_004f26f0(local_118)` parses the chosen clientinfo XML
   (server list, version, langtype, etc.). If mode==3 and the path contains
   `clientinfo`, a flag `DAT_0076fd44` is cleared.

4. **Anti-tamper / self-check** — `FUN_0067407e(exePath,0)`. On mismatch it shows a
   localised message (`"%s is changed. Press ok to restart"`, Portuguese
   `"%s foi modificada…"` for mode 0xc), then **relaunches itself**
   (`CreateProcessA(self)` + `WaitForSingleObject`) and writes a registry value via
   `FUN_00654f60` (HKLM key `PTR_DAT_006bd38c`).

5. **Raise timer resolution** — `timeGetDevCaps` + `timeBeginPeriod(min)` so frame
   pacing / `Sleep` are accurate.

6. `FUN_00421e50()` then **`CoInitialize(NULL)`** (COM, used later for the shell/registry
   and Bink).

7. **Single-instance guard** (non-zero, non-3 modes): `CreateMutexA("Global\\Surface")`
   + `WaitForSingleObject`. A checksum sanity guard follows — the byte sum of the
   `"Surface"` string must equal `0x2c9`, otherwise WinMain bails; this is an
   anti-patch trip-wire.

8. `FUN_0061dd60()` — early global init.

9. **OS gate** (mode 0 only): `GetVersionExA`; if `dwPlatformId==1` (Win9x)
   → `FUN_00653940`, else `FUN_00653d30` (platform-specific setup, e.g. DEP/priority).

10. **Resource name table** — `FUN_00512b70("resNameTable.txt")` + `FUN_00513e80`
    load the GRF resource-name remap table (Korean→internal path aliasing).

11. **Create the window** — `FUN_00654b60(hInstance)` @ `0x00654b60`:
    - `RegisterClassA`: `lpfnWndProc = FUN_006541e0`, `hIcon = LoadIcon(0x77)`,
      `hCursor = IDC_ARROW`, `hbrBackground = GetStockObject(4)`, class name
      `PTR_DAT_006bd388`.
    - `AdjustWindowRect` for the configured resolution (`DAT_006e4bd0` × `DAT_006e4bd4`),
      style `0x2CA0000` (windowed caption), centred on the desktop via
      `GetSystemMetrics`.
    - `CreateWindowExA` → global HWND **`DAT_007735d8`**; on failure MessageBox
      `"Window Creation Failed"`. `ShowWindow(SW_SHOW)` + `UpdateWindow`.
    - Keyboard-layout / region checks (`GetKeyboardType`) with localised message boxes
      pulled from the string table via **`FUN_00504fb0(id)`** (string-table lookup by id).
    - `GetCurrentDirectoryA` saved to `DAT_007730b8`.

12. **Audio init** — `FUN_00421e50` earlier readied it; `FUN_004214f0()` returns false →
    `"Audio System Init Failed"` and exit. (Gated on `DAT_007731a8`.)

13. **Graphics + GRF init** — `FUN_00403f30(hwnd, &DAT_00773148, &DAT_00773138, …, mode)`.
    Returns `<0` → `"Cannot init d3d OR grf file has problem"`. This creates the
    DirectDraw/D3D device **and** mounts the GRF archives. The device/context object is
    fetched into **`DAT_006ee3d8`** by `FUN_00404ed0()`; `FUN_00418fa0()` post-init.

14. **Post-graphics setup** — `SetFocus(hwnd)`; `ShowCursor(0)` if fullscreen
    (`DAT_006ebfec`); viewport `FUN_004cc950(dev+0x24, dev+0x28)` (back-buffer W×H);
    then a batch of one-time subsystem constructors:
    `FUN_0050e470, FUN_00419b50, FUN_004cf2b0, FUN_004342a0`,
    `FUN_004342b0("model\3dmob\guildflag90_1.gr2",5,0)` (preload the guild-flag 3D
    model), `FUN_0043b500, FUN_0040ad90`.

15. **Mode → startup flags** — a switch on `g_mode` sets `DAT_006d7e0c` (0/1/2), which
    selects the initial game-state / whether login is skipped.

16. **Intro movie** — `FUN_0050b740(1)` plays `openning.bik` (Bink). The playback loop
    is frame-limited with `QueryPerformanceCounter` + `Sleep`, calling the per-frame
    Bink step **`FUN_0050b390`** (`BinkDoFrame` → `BinkCopyToBuffer` → DirectDraw `Blt`),
    and can be skipped via a pending message.

17. **Load first scene** — `FUN_004f6ae0(0, "login.rsw")` loads the login world map,
    followed by the remaining game-object initialisers:
    `FUN_004217c0, FUN_004cc990, FUN_00419480, FUN_00418c90, FUN_00419050,
    FUN_004cccc0, FUN_004d09b0, FUN_0061d890, FUN_00512b70, FUN_005130b0,
    FUN_0040c890, FUN_00404ad0(dev), FUN_00403e00, FUN_00419c20`.
    (`FUN_00421e60(0x3c)` earlier arms a 60-unit timer — candidate frame clock.)

18. **Shutdown** — after the game ends, WinMain drains pending messages
    (`PeekMessageA` loop with the IME `WM_KEYDOWN wParam==0xE5` special case →
    `ImmGetVirtualKey`), then `CoUninitialize`, `DestroyWindow(hwnd)`,
    `FUN_0050e900` (final teardown), `CoUninitialize`.

## WndProc @ 0x006541e0 (`FUN_006541e0`) — partial

- **`WM_ACTIVATEAPP`** — toggles `DAT_006e4bdc` (foreground flag). Foreground →
  `SetPriorityClass(NORMAL=0x20)` + `FUN_00419e20(1)`; background →
  `SetPriorityClass(HIGH=0x40)` + `FUN_00419e20(0)` (pauses input/mouse capture).
- **`0x281` (WM_IME_SETCONTEXT / IME)** — handled for Korean input.
- Everything else → `DefWindowProcA`.
- The full mouse/keyboard/paint message handling is dispatched from here into the
  input subsystem — detailed in Subsystem 4 (DirectInput) / Subsystem 2.

## Open question (carried to Subsystem 2)

The multi-minute **per-frame main loop** is not a plain `while` in WinMain's tail
(that tail loop only *drains* messages at shutdown). Evidence points to a
timer-driven model armed by `FUN_00421e60(0x3c)` (≈60 Hz) dispatched through the
`WndProc`, or a peek-idle render inside one of the step-16/17 initialisers. To be
pinned down next.

## Key addresses (for resume)

| Symbol | Addr | Role |
|--------|------|------|
| WinMain | `0x006555f0` | entry, init order, shutdown |
| Window init | `0x00654b60` | RegisterClass + CreateWindow |
| WndProc | `0x006541e0` | window messages |
| Msg pump (helper) | `0x00654ed0` | drain + IME |
| Registry write | `0x00654f60` | HKLM restart flag |
| Graphics+GRF init | `0x00403f30` | DirectDraw/D3D + GRF mount |
| Device fetch | `0x00404ed0` | → `DAT_006ee3d8` |
| Audio init | `0x004214f0` | audio device |
| Clientinfo parse | `0x004f26f0` | clientinfo XML |
| String table lookup | `0x00504fb0` | localised text by id |
| resNameTable load | `0x00512b70` | GRF name remap |
| Intro Bink loop | `0x0050b740` | openning.bik |
| Bink per-frame | `0x0050b390` | BinkDoFrame/Blt |
| Load map scene | `0x004f6ae0` | e.g. login.rsw |
| g_mode | `DAT_0074b560` | launch mode |
| HWND | `DAT_007735d8` | main window |
| Device ctx | `DAT_006ee3d8` | render device |
