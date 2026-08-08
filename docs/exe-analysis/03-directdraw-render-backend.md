# Subsystem 3 — DirectDraw / Direct3D render backend

The exe renders through **DirectDraw7 + Direct3D7 immediate mode** (imports `DDRAW.DLL`;
`DirectDrawCreateEx` with `IID_IDirectDraw7` GUID at `DAT_006a20dc`). There is no D3D8/9;
all 2D sprites and the 3D map/models are drawn as textured triangles via the D3D7 device,
and Bink video is blitted straight to the DirectDraw primary surface.

## The render-device object (`CRenderDevice`, `this = param_1`)

Created/owned by `FUN_00403f30` @ `0x00403f30`; fetched globally by
`FUN_00404ed0` → **`DAT_006ee3d8`**. Field layout in use:

| offset | meaning |
|--------|---------|
| `[0x0]` | validity/first flag |
| `[0xf]` | **Direct3D7 device** (vtable object; `SetRenderState`=`vtbl+0x50`, `SetTextureStageState`=`vtbl+0x94`) |
| `[0x24]`, `[0x28]` | back-buffer **width / height** (viewport) |
| `[0x4d]` | video-mem/caps flag (`0x4000` HW / `0x800` fallback) |
| `[0x75]` | fullscreen flag |
| `[0x77]` | the `IDirectDraw7` object |

## Device creation — `FUN_00403f30` → `FUN_00404260` @ `0x00404260`

1. Pick caps into `[0x4d]` (`0x4000` = hardware / `0x800` = software/emulated) from a
   caps table (`DAT_006a1d7c` / `DAT_006a1d3c`).
2. Fullscreen (`flags&1`): `SetWindowLongA(GWL_STYLE, WS_POPUP)` + `ShowWindow`.
3. **`DirectDrawCreateEx(guid, &ddraw, IID_IDirectDraw7, 0)`** → `[0x77]`.
4. Mode setup:
   - fullscreen → `FUN_00404570(w,h,flags)`: `SetCooperativeLevel(EXCLUSIVE|FULLSCREEN)`
     + `SetDisplayMode`.
   - windowed → `FUN_00404450`: normal coop level + create an `IDirectDrawClipper`
     bound to the HWND.
5. **`FUN_004049f0`** creates the surface chain — primary + back-buffer (flip chain) +
   an attached **Z-buffer**, then creates the **Direct3D7 device** on the back-buffer
   → stored in `[0xf]`.

## Default pipeline state (set once in `FUN_00403f30`)

`SetRenderState` (`vtbl+0x50`) programs the fixed-function pipeline: cull mode, shade
mode, Z-enable (`0x13→5`, `0x14→6` = Z test/write), alpha test/blend (`0x18→0xF0`),
fog, dither, `TFACTOR`. `SetTextureStageState` (`vtbl+0x94`) sets up **two texture
stages**:
- **stage 0**: `COLOROP=MODULATE (4)` of texture × diffuse, `ALPHAOP`, address `WRAP`,
  **bilinear** min/mag filter (`MINFILTER/MAGFILTER = LINEAR = 2`), `TEXCOORDINDEX=0`.
- **stage 1**: second texture for lightmaps/blending (`COLOROP`, `ALPHAARG`, filter,
  `TEXCOORDINDEX=1`), constant `TFACTOR = 0xFF000000`.

This is the classic RO look: sprites and terrain are `MODULATE(texture, vertexcolor)`
with the lightmap applied through the second stage.

## Frame present / surface loss

- `FUN_00404bc0(1)` (from `WM_MOVE`) recomputes the windowed clip rectangle so blits
  land in the right client area.
- Surface-lost / focus loss is handled via the WndProc focus messages (Subsystem 2);
  the device is restored/`Reset`-equivalent on `WM_SETFOCUS`.
- Actual triangle submission (`DrawPrimitive`/`DrawIndexedPrimitive` through the
  `[0xf]` device) is invoked by the sprite renderer (Subsystem 6) and the map/model
  renderer (Subsystem 7); this file covers device + state only.

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| Render-device init | `0x00403f30` | create device + default states |
| DDraw/D3D creation | `0x00404260` | `DirectDrawCreateEx`, surfaces |
| Fullscreen mode set | `0x00404570` | coop level + display mode |
| Windowed clipper | `0x00404450` | clipper on HWND |
| Surface + D3D device | `0x004049f0` | primary/back/Z + D3D7 device |
| Device accessor | `0x00404ed0` | → `DAT_006ee3d8` |
| Clip recompute | `0x00404bc0` | on WM_MOVE |
| IID_IDirectDraw7 | `DAT_006a20dc` | GUID |
| Global device | `DAT_006ee3d8` | CRenderDevice |
