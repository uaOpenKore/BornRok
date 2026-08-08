# Subsystem 6 — Sprite pipeline (.spr frames + .act animation)

Every animated 2D entity (characters, monsters, NPCs, items, effects, cursor) is a
**`.spr` (frames/pixels) + `.act` (animation/layout)** pair, loaded through the VFS
(Subsystem 5) and drawn as textured quads through the D3D7 device (Subsystem 3).

## .spr parser — `FUN_004385f0` @ `0x004385f0`

Reads a 6-byte header via the CFile stream helper `FUN_0050d570(buf,n)` (n-byte read);
`FUN_0050d620` seeks.

1. **Magic** `local_38 == 0x5053` (`'SP'`); **version** `local_36`.
2. **Palette** (version > 0x100): seek to **tail − 0x400**, read the **256-colour RGBA
   palette** (0x400 bytes) into a buffer, seek back, store into the sprite at
   `this+0x110` (`FUN_00404e60`). This is the shared palette used by all indexed frames
   (and the target of dye/`.pal` recolouring).
3. **Indexed frame count** at `this+0x530`; **RGBA frame count** read (2 bytes) when
   version ≥ 0x1FF.
4. **Indexed frames** (loop `this+0x530` times): read `width,height` (`local_1c`),
   allocate a 0x10-byte frame record `{w,h,flagX,flagY,…,texptr@+6}`:
   - version < 0x201 → read raw `w*h` palette indices;
   - version ≥ 0x201 → read a 2-byte packed length then **RLE-decompress**
     (`FUN_00439260`) the indices.
   Then `FUN_004390c0` converts indices+palette into a **DirectDraw texture** (frame’s
   `+6` slot).
5. **RGBA (truecolor) frames** (version ≥ 0x200) follow, stored the same way.
6. **Optional HD downscale**: globals `DAT_007735f4` / `DAT_007735f8` (X/Y reduction) →
   `FUN_004392f0` resamples the frame and halves the logical size (`flagX/flagY` mark
   the 2× frames). Frames are pushed into the sprite’s frame vector at `this+0x518`
   (end `this+0x51c`).

Version map: **0x100/0x101** paletted, **0x200** adds truecolor RGBA frames, **0x201/0x202**
RLE-compress the indexed frames.

## .act parser — `FUN_00422aa0` @ `0x00422aa0`

Reads a 0x10-byte header via `FUN_0050d570(&hdr,0x10)`; **magic `0x4341` (`'AC'`)** else
`"Illegal file format"`. The body is the classic ACT structure:

- **Actions[]** — one per (direction × motion). Each action = **Frames[]**.
- **Frame** = **Layers[]** + per-frame **delay** (animation speed) + **event/sound index**
  + **anchor points**.
- **Layer** = referenced `.spr` frame index, `x/y` offset, **mirror** flag, **tint RGBA**,
  **scale x/y**, **rotation**, and sprite-type (indexed vs RGBA).
- **Anchor / attach points** ("CP" points) — used to attach child sprites (head, hair,
  headgear, weapon, shield, effects) to the body at the correct pixel.

## Animation & compositing

- The active action index = `direction * (actions_per_direction) + motionState`
  (idle/walk/attack/hurt/sit/dead…). The frame cursor advances by the frame **delay**,
  **scaled by the server-driven attack/walk speed** (so attack motion matches ASPD).
- Each visible layer is drawn as a **billboarded textured quad** through the D3D device,
  applying the layer tint × the entity’s environment light (day/night + status tint),
  depth-biased for correct draw order. Child sprites are positioned via the parent’s
  current-frame anchor points.
- The paletted texture can be re-generated with a substituted palette for **dye**
  (hair/cloth colour) — the palette pointer at `this+0x110` is the hook.

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| .spr parse | `0x004385f0` | header/palette/frames |
| .act parse | `0x00422aa0` | actions/frames/layers/anchors |
| RLE decompress | `0x00439260` | v≥0x201 indexed data |
| index+palette→texture | `0x004390c0` | build DDraw texture |
| HD resample | `0x004392f0` | DAT_007735f4/f8 downscale |
| stream read | `0x0050d570` | CFile read N bytes |
| stream seek | `0x0050d620` | CFile seek |
| palette store | `0x00404e60` | → sprite `+0x110` |
| frame count | `this+0x530` | indexed frames |
| frame vector | `this+0x518/0x51c` | frame list |
