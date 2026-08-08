# Deep-dive — Effects, particles & how they are rendered

Focused reverse-engineering of the visual-effect subsystem: how **clouds, smoke, skill
effects and every particle** are created, animated and drawn. Extends Subsystem 8.

There are **two effect families**, both drawn as camera-facing textured billboards through
the Direct3D7 device (Subsystem 3):

- **`.str` effects** — data-driven layered keyframe animations (artist authored). Factory
  `CEffect::Init 0x005c2630` maps `effectId → *.str`. See Subsystem 8.
- **Procedural particle effects** — code-driven emitters (clouds, smoke, rings, bolts,
  Grand Cross, Asura…). This document.

Both are owned by the scene's **effect manager list** and share the billboard renderer.

---

## 1. Effect manager (per scene)

The world scene holds a **doubly-linked list of active effects** at `scene+0x164`
(node = `{next, prev, effectObj}`) with a count at `scene+0x168`. Every effect object
exposes a common **message method `vtbl+8`** `(effect, sender, msgId, a, b)`:
- `0x7D` init, `0x18` stop, `0x19` clear-all, plus positional updates (`0x0E` set
  pos/target). The manager broadcasts these by walking the list (e.g. clear-all sends
  `0x18` to every node then frees them — `~0x005994xx`).

Each frame the manager **updates** every effect (advance state) and **renders** every
effect (draw its billboards), then reaps finished ones (`count--`, unlink, free).

---

## 2. Particle emitter object (`CParticleEffect`)

### Creation

`FUN_005c3670(typeId, …)` @ `0x005c3670`:
- `operator_new(0xF9E4)` (~64 KB) — the emitter + its embedded particle array,
- ctor `FUN_00610e80` @ `0x00610e80` sets **vtable `PTR_FUN_0069fbc8`**,
- init `FUN_0061a650`, then **links it into `scene+0x164`** (`scene+0x168++`).
`typeId` selects the emitter class (e.g. `0x42` clouds, `0x4F` waterfall/stream).

### Layout

| offset | field |
|--------|-------|
| `+0x04/08/0C` | world position (copied from owner each frame) |
| `+0xF4` | owner actor pointer |
| `+0xFC` | texture-handle array (`FUN_0040c590(name,0)` loads/caches a texture → handle) |
| `+0x114/0x198` | per-effect **timing** record = `DAT_006d9100[effectId]` (lifespan/∞) |
| `+0x1B8` | particle count (array length) |
| `+0xF704 …` | **particle array** — each particle = **0xB8 (184) bytes** |

### Particle struct (0xB8 bytes) — fields observed in the cloud emitter

| offset | meaning |
|--------|---------|
| `+0x00` | active flag |
| `+0x08` | age/timer |
| `+0x0E` | **lifespan** in frames — variant-dependent: `0x28`(40)/`0x1E`(30)/`0x19`(25)/`0`(∞) |
| `+0x6C` | position/velocity block (seeded from float consts `_DAT_0069568x…0069572x`) |
| `+0x30/0x34` | size / scale (`0x40800000`=4.0, `0x40000000`=2.0, `0x3F800000`=1.0) |
| — | colour/alpha + rotation (faded over life) |

So the emitter fills its particle array with **initial position, velocity, size, lifespan
and colour**; the shared update advances each particle (`pos += vel`, `age++`, alpha
ramps down, killed when `age ≥ lifespan`), and dead emitters unlink themselves.

---

## 3. Coded-effect dispatch (which particles for which effect)

A large `switch` (base `~0x005b9b88`) maps a **sub-effect id → an emit function**; each
emit function spawns an emitter with a **specific `effect\*.tga` texture + motion**.
Catalogue (representative):

| emit fn | texture(s) | effect |
|---------|-----------|--------|
| `0x005e08b0` | `effect\cloud11.tga` (variants 0/1/2/10/11) | **clouds** (drift + fade) |
| `0x005e0bc0` (type `0x4F`) | `effect\waterfall11/12/31/32.tga` | waterfall/stream |
| `0x00602f20` | `effect\alpha_down.tga` | falling glow |
| `0x00604c50` | `effect\ring_white.tga` | expanding ring |
| `0x00603750` | `effect\ring_purple.tga` | ring |
| `0x005fcca0` | `effect\ring_red.tga` / `alpha_down.tga` | ring/pulse |
| `0x00607500` | `effect\ring_blue.tga` | ring |
| `0x005fed20` | `effect\foot_l_b.tga` | footprints |
| — | `effect\smoke.tga` | **smoke** |

Named coded **skills** already reversed (same mechanism, see memory
`uaro-exe-disassembly`): **Grand Cross** render `0x005ba4e2`
(`cross_old.bmp`+`explosive_1_128.bmp`+`alpha_center.tga`), **Asura** `0x005db1b3`
(`asura1..16.tga` kanji), **Soul Breaker** `0x005db80e` (`soul_s/o/u/l.tga`).
These have **no `.str`** — the visual *is* the texture set + the emit/motion code.

---

## 4. Billboard render & blend modes (the actual pixels)

Each particle/`.str` layer is drawn as a **camera-facing quad** through the render device
(the device wrapper reached via `param+0x7C`, i.e. the D3D7 device `dev+0xF`). The render
functions program the blend before submitting:

`SetRenderState` (`vtbl+0x50`) with `D3DRS_SRCBLEND=0x13`, `D3DRS_DESTBLEND=0x14`
(D3DBLEND values: `1`=ZERO, `2`=ONE, `5`=SRCALPHA, `6`=INVSRCALPHA):

| mode | src,dst | used for |
|------|---------|----------|
| **alpha** | `(5,6)` SRCALPHA / INVSRCALPHA | clouds, smoke, soft sprites |
| **additive** | `(2,2)` ONE / ONE | glows, light, magic, bolts, auras |
| **premul/glow** | `(2,5)`, `(5,2)` | mixed light passes |

- **Z**: test on, **write off** for transparent particles so overlapping billboards blend
  instead of occluding (this is the exact reason the port had to add/omit `WRITE_Z`
  carefully — memory `onTop-sprite-no-writez-particle-bleed`).
- **Texture stage 0** = `MODULATE(texture, particle colour)`, bilinear filter (Subsystem 3).
- **Billboarding**: the quad's corners are built from the camera's right/up vectors so it
  always faces the viewer; positioned at the particle's world position. Skill effects are
  **body-anchored** to the owner (~torso, ≈ −80 px screen), **one-shot**, no scale
  multiplier (memory `str-effect-orientation-scale`, `EXE .str construction model`).

---

## 5. Clouds & smoke specifically

- **Map ambient** clouds/smoke/fireflies come from **RSW map effects** — the RSW object
  list carries `EF_` ids (smoke `44`, firefly `45`, sparkle `165`, …) that the scene turns
  into these same emitters at the placed positions (see `rsw-map-effects-id44.md`).
- **Cloud billboards**: `effect\cloud1/2/4/11.tga` drift with a slow velocity and fade via
  the alpha blend; the airship/sky maps add extra drifting cloud billboards (port already
  reproduces this from roBrowser `Sky.js` — memory `airship-sky-clouds`).
- **Smoke**: `effect\smoke.tga` emitter with upward velocity + expanding scale + alpha
  fade (alpha blend `(5,6)`).

---

## 6. Skill effects end-to-end (server → pixels)

1. Server sends the trigger (0x1F3 `EF_` id / 0x19B level-up / skill packet / 0x8A act).
2. `CEffect::Init 0x005c2630` resolves the id: **has `.str`** → play the layered keyframe
   animation; **no `.str`** → the coded dispatch spawns the right particle emitter(s).
3. The emitter fills its particle array (texture + motion + lifespan from `0x6D9100`).
4. Each frame: update particles, then render them as blended billboards anchored to the
   caster/target, until the timing expires and the effect unlinks itself.

---

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| `.str`/coded factory | `0x005c2630` | effectId → .str or coded |
| Per-effect timing table | `DAT_006d9100` | `[effectId]` lifespan |
| Particle emitter factory | `0x005c3670` | new emitter (typeId) |
| Emitter ctor (vtable) | `0x00610e80` | `PTR_FUN_0069fbc8` |
| Emitter init | `0x0061a650` | setup |
| Texture load/cache | `0x0040c590` | name → texture handle |
| Cloud emitter | `0x005e08b0` | `cloud11.tga` |
| Waterfall/stream emitter | `0x005e0bc0` | `waterfall*.tga` (type 0x4F) |
| Coded emit switch | `~0x005b9b88` | sub-effect id → emit fn |
| Effect list (scene) | `scene+0x164` / `+0x168` | active effects + count |
| Effect message method | `vtbl+8` | init 0x7D / stop 0x18 / clear 0x19 |
| Grand Cross render | `0x005ba4e2` | cross+burst+glow |
| Asura render | `0x005db1b3` | asura1..16 kanji |
| Soul Breaker render | `0x005db80e` | SOUL letters |

Related: `08-effect-engine.md`, `effect-ids-from-exe.md`, `ragexe-effect-map.md`,
`particle-engine-re.md`, `coded-skill-effects.md`, `rsw-map-effects-id44.md`,
memories `airship-sky-clouds`, `onTop-sprite-no-writez-particle-bleed`,
`str-effect-orientation-scale`, `never-invent-skill-effects`.
