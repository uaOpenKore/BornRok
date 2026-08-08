# Subsystem 4 — Input (DirectInput mouse + Win32 keyboard)

The client splits input by device: the **mouse** is read through **DirectInput7**
(relative motion, buffered), while the **keyboard** is handled through the Win32 window
messages routed by the WndProc into the IME/input manager (`DAT_0074aea0`). There is no
DirectInput keyboard device (no `GUID_SysKeyboard`/`c_dfDIKeyboard` in the binary).

## Mouse init — `FUN_00419b50` @ `0x00419b50` (CMouse)

`this = param_1`:
1. `DirectInputCreateA(hInstance, 0x0700, &dinput, 0)` — DirectInput **v7**. Failure →
   `"DirectInput not available"`. Object stored at `this[0]`.
2. `CreateDevice(GUID_SysMouse = &DAT_006a109c, &device, 0)` (`vtbl+0xC`) → `this[1]`.
   Failure → `"ErrorMsg :: CreateDevice(SysMouse)"`.
3. `SetDataFormat(c_dfDIMouse = &DAT_0069fe84)` (`vtbl+0x2C`).
4. `SetCooperativeLevel(hwnd, DISCL_FOREGROUND|DISCL_NONEXCLUSIVE = 5)` (`vtbl+0x34`).
   Failure → `"ErrorMsg :: SetCooperativeLevel"`.
5. `Acquire()` (`vtbl+0x1C`).
6. Seed state: `this[5]=0x140 (320)`, `this[6]=0xF0 (240)` = initial cursor X/Y;
   `this[8..0xB]` = button states; `this[0xE]=500` = double-click window (ms);
   `this[0xF] = GetSystemMetrics(SM_SWAPBUTTON)` = left/right swap flag.

The global mouse-device pointer is **`DAT_006ee6f4`**.

## Per-frame polling

Each frame the mouse object reads **relative** deltas from the DI device (`GetDeviceState`
/ buffered `GetDeviceData`), accumulates them into the virtual cursor `this[5]/this[6]`,
and **clamps to the screen resolution**; button transitions (down/up, with swap applied)
and the wheel are latched for that frame. UI hit-testing and world picking consume this
cursor (Subsystems 7/10). On device loss the next poll re-`Acquire`s.

## Focus / capture — `FUN_00419e20` @ `0x00419e20`

Called from `WM_ACTIVATEAPP` (Subsystem 2). `arg != 0` → `Acquire` (`vtbl+0x1C`) the
mouse device (regained foreground); `arg == 0` → `Unacquire` (`vtbl+0x20`) so the OS
cursor is released when the game loses focus. Guarded on `DAT_006ee6f4`.

## Keyboard

Keyboard input arrives as Win32 `WM_KEYDOWN` / `WM_KEYUP` / `WM_CHAR` in the WndProc,
which first offers them to the **IME/input manager `DAT_0074aea0`** (`vtbl+0x10` filter)
for Korean composition; unconsumed keys drive hotkeys / chat / movement in the active
mode's `Process`. The IME `WM_KEYDOWN wParam==0xE5` case (Subsystem 1/2) converts the
composed virtual key via `ImmGetVirtualKey`.

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| Mouse init (CMouse) | `0x00419b50` | DI7 SysMouse device |
| Capture toggle | `0x00419e20` | Acquire/Unacquire on focus |
| Warn helper | `0x004f6cb0` | on-screen/log warning printf |
| GUID_SysMouse | `DAT_006a109c` | device GUID |
| c_dfDIMouse | `DAT_0069fe84` | data format |
| Global mouse dev | `DAT_006ee6f4` | CMouse instance |
| IME/input mgr | `DAT_0074aea0` | keyboard/IME first-chance |
