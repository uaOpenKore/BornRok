# RO effect particle engine — reverse-engineering notes (uaRO.exe)

Goal (S.): port the exe's procedural effect particle engine **1:1** so coded skills (Magnum, Bash,
Stone Curse, …) look exactly like the original — no single-quad/ring approximations.

## Object model (per effect instance)

Created by the factory `CEffect::CreateChild` @ **0x5c3670** (thiscall parent, args: type, {0,0,0}):
`operator new(0xf9e4)` (~63KB) → constructor **0x610e80**. Stored at global `0x7710a8` (last created).

Constructor 0x610e80 sets:
- vtable `[obj] = 0x69fbc8` (9 entries; mostly small accessors: [4]=0x544800 is an angle-wrap+sin/cos
  helper, not the sim).
- **Emitter array** at `[obj+0x3b8]`, stride **0x8c**, up to **0x32 (50)** slots (loop 0x610f2a,
  init fn 0x4e59b0).
- **Particle array** at `[obj+0x1f04]`, stride **0x6c**, up to **0x200 (512)** slots (loop 0x610f55,
  init fn 0x40d3c0).
- Defaults: `[obj+0x20c]=36.0f`, `[obj+0x17c]=1` (flags).

## Per-effect config (set by the skill handler AFTER CreateChild)

Magnum Break handler **0x5c8040** creates TWO children and sets, per child:
- `[0x198]` = skill level (from parent [0x114]); `[0x1a4] = [0x198] - 0xf`.
- `[0x294]=90.0f`, `[0x2d0]=1.75f` (child1) / 1.15f (child2), `[0x278]=12.0f`, `[0x2b4]=3.0f`.
- `[0x2d4] = ([0x198]/[0x2d0]) * const(0x69c470)`; `[0x2ac] = [0x258] * const(0x69c5f8)`.
- `[0x17c] |= 2` (flag).
- texture: `[0xfc]` = malloc([0x1b8]*4) array; entry 0 = CTexture for `effect\ring_yellow.tga` (child1)
  / `effect\대폭발.tga` (child2), loaded via `0x40c590` (this=0x6ee3c8 texture mgr).
- calls **0x6113a0** = **populate emitters** (see below).

Bash handler **0x5c7cf0**: type 4, `[0x198]=0x28`, textures `alpha_center.tga`/`alpha_down.tga`/`lens1.tga`.

## Emitter-spawn 0x6113a0 (config template → emitter slots)

Loops `i < [obj+0x1a8]` (emitter count), each slot = `[obj+0x3b4 + i*0x8c]`, copies a TEMPLATE from
the object into the emitter struct:
- slot[-8..0] ← [obj+4],[obj+8],[obj+0xc]  (position?)
- slot[+4..+30] ← [obj+0x37c .. 0x3a8]  (11 dwords: the emitter param template)
- slot[+34] ← [obj+0x2cc]; slot[+38] ← [obj+0x29c]; slot[+3c] ← [obj+0x290]
- slot[+40..+6c] ← rep movsd 0xc (12 dwords) from [obj+0x118]  (a 3x4 matrix / basis)
- slot[+70..+78] ← [obj+0x1f0..0x1f8] (RGB); slot[+7c] ← [obj+0x2a8]
- slot[+80] = packed ARGB: (A<<24)|(R<<16)|(G<<8)|B where A = sin([obj+0x2a8])&0xff (0x671970 = sin)

## Core simulation region: 0x615000–0x61a000 (~20KB, ~21 functions)

The per-frame particle math lives here — enumerated function starts (float-op count = math density):

| addr | floats | calls | notes |
|---|---|---|---|
| 0x615070 | 68 | 8 | |
| 0x615290 | 74 | 7 | |
| 0x6154f0 | 85 | 7 | |
| 0x615790 | 59 | 10 | |
| 0x6159e0 | 55 | 6 | touches particle array |
| 0x615c80 | 81 | 5 | particle array |
| 0x615f80 / 0x616100 | 26 | 9 | paired (twin) |
| 0x6163d0 | 43 | 2 | particle array |
| 0x6168d0 | 51 | 6 | |
| 0x616bc0 / 0x6170f0 | 90 | 0 | pure-math twins |
| 0x617620 | 112 | 0 | densest math (matrix/transform?) |
| 0x617d70 | 87 | 0 | |
| 0x618590 | 85 | 2 | |
| 0x618d00 | 104 | 2 | |
| 0x619450 | 0 | 3 | dispatch/wrapper (no floats) |
| 0x619890 | 67 | 14 | many calls -> orchestrator (update loop?) |

Best update-loop candidate: **0x619890** (67 floats, 14 calls, touches particle array) — likely the
per-frame orchestrator calling the per-particle math + render. Render likely in the 0x5e3xxx–0x5e4xxx
cluster (the `test byte [ebx+0x1f04]` / `lea [ebx+ecx*4+0x1f04]` particle-iteration + draw).

**Reality/scope:** a 1:1 port = reversing + reimplementing ~21 float-dense functions (thousands of
lines of exact float math) + the render, verified only on S.'s build (no local GPU/bgfx). This is a
multi-session marathon — reverse one function at a time, port, keep this doc as the map.

## Update loop 0x619890 (cracked)

Per-frame particle update (thiscall, this=ebx=effect obj):
- Time factors from `[obj+0x20c]` (=36.0f = effect LIFETIME in frames): angular rates
  `const/[0x20c]` (rotation progresses over the lifetime).
- Loops particles by index `esi`: base `eax = obj + esi*0x6c` (confirms **particle stride 0x6c**),
  particle fields at `[eax+0x1f04 .. +0x1f70]` — ~13 dword fields addressed (0x1f04, 1f14, 1f1c,
  1f20, 1f24, 1f34, 1f3c, 1f40, 1f44, 1f54, 1f5c, 1f60, 1f64) = pos.xyz / vel.xyz / color / size /
  life / angle etc.
- `sin/cos` via **0x671970** of `[obj+0x290] + offset` (emitter base angle) → **angular/ring
  emission** (this is the ring-of-flames geometry, done procedurally, not a texture ring).
- Constants: 0x6959f0, 0x695850, 0x69572c, 0x695718, 0x695764, 0x69fbf0 (rates/thresholds — decode
  as floats when porting).

## Particle positioning = 3D rotation matrices (0x619a00+)

Each particle's world position is built with **3D rotation matrices**, not a flat 2D ring:
- helper `0x671970` = sin/cos; `0x40ae20` = float post-op (fabs/neg?); `0x408e50` = build/load a
  rotation matrix (local at [ebp-0x120]/[ebp-0x150]/[ebp-0x1b0]); `0x409060` = transform a vector /
  matrix-multiply.
- pattern `fld [obj+0x2cc]; fld matrix.comp; fmul; fadd center.comp; fstp particle.pos.comp` =
  `particle.pos = rotMatrix * radius([0x2cc]) + center` per xyz.
- angles from `[obj+0x290]` (base) + time (`[0x20c]` lifetime) → the ring spins in 3D over its life.

So magnum's flames sit on a 3D circle (radius [0x2cc]) that rotates over 36 frames — a genuine 3D
particle emitter. Port needs: 0x408e50 (matrix build), 0x409060 (transform), 0x40ae20, plus the
size/color/life curves + render. Marathon continues.

## Particle init 0x5e40a8 (fields confirmed)

Per-particle "create/init" writes the 0x6c-byte struct (base = obj + idx*0x6c):
- +0x1f34 = param, +0x1f3c/+0x1f40 = floats (initial pos/size?), +0x1f44..+0x1f60 = 8-dword block
  (rep movsd) = the particle's transform matrix, +0x1f54 = param, +0x1f5c/+0x1f60 = floats,
  **+0x1f64 = texture handle from `[obj+0xfc][texIdx*4]`** (the ring_yellow/대폭발 CTexture), base
  color alpha 0xff000000, flags `[obj+0x17c]`.

So each particle carries: transform matrix, texture handle, color, size — driven by the update loop's
3D-rotation math. Structure is now FULLY mapped (object → emitters → 3D ring positioning → particle
fields → texture). Remaining for a byte-exact 1:1: the size/color/life CURVES (the ~20 float fns'
exact coefficients) + the GPU draw + the matrix primitives 0x408e50/0x409060.

## STILL TO REVERSE

- [ ] per-frame **update** (integrate particle pos/vel/gravity/life/size/rotation) — the main sim loop.
- [ ] particle **render** (quad build, blend, texture `[0xfc]` frame select).
- [ ] the emitter template fields (0x37c…, 0x118 matrix, 0x290/0x29c/0x2cc, velocity/spread/rate).
- [ ] how `type` (12/4) selects the base template (the config before 0x6113a0).
- [ ] Bash (type 4) full config.

## Tooling

`/tmp/scrape.py` + ad-hoc capstone scripts in scratchpad; `re/uaRO.exe` (PE32 x86, unpacked).
Recipe: string `effect\EF_<Skill>.wav` → push xref = handler → after `call 0x5c3670` reads the config.
