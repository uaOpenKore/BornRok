# Merchant / Alchemist / Bard / Dancer

> Per-skill effect dossiers reversed from `winEXE/uaRO.exe`. See [`README.md`](README.md) for the shared conventions and field meanings.

## Merchant / Alchemist / Bard / Dancer — skill effect dossiers

### Architecture (how these classes are wired in uaRO.exe)

Every server-driven effect id routes through **one master procedural dispatcher `FUN_005bd3d0` (0x5bd3d0, ~2700 lines)**, a `switch(effectId)`. Three outcomes exist:

1. **Dedicated procedural handler** — its own `FUN_` that spawns particles via the factory `FUN_005c3670(type,x,y,z)` and plays sounds via `FUN_004396b0(name,x,y,z,0xfa,0x28,1.0f)`.
2. **Shared `.str` case** (`switchD_005bd4cf_caseD_17`, tail at 0x5c1... line 365767): ids fall through to `FUN_005c3870(0,-80f,0)` (torso anchor) and render a `.str` whose filename is chosen by effect-id in the StrEffect ctor **`FUN_005c2630` (0x5c2630)**; lifespan = `DAT_006d9100[effectId]` (stored at obj+0x114).
3. **`default:`** → `"Invalid Effect Id: %d"` log = **no client visual** (skill is purely data-driven server-side; nothing exists in this exe).

Common emitter fields seen below: +0x17c=5 → flat ground-quad marker; +0x198 lifespan; +0x1a4 fade-start; +0xfc texture-ptr array (+0x1b8 count); +0x290/+0x294 yaw; +0x2d0/+0x2d4 size+growth; +0x358.. velocity; +0x29c=0 → alpha-blend (non-zero/omitted → additive). "Fires at frame N" = guard `obj+0x110 == N` (animation frame counter).

---

### Merchant

**Mammonite — EF_COIN (id 10 / 0x0a) — `FUN_005c9ff0` (0x5c9ff0)**
- **Sound:** `EF_Coin_d_{1,2,3}.wav` (random), played *per coin* by the coin update `FUN_00613770` (0x613770) at the moment a coin bounces/lands (not on cast frame).
- **Textures:** `coin_a.bmp` (bound to the particle via `FUN_005481c0(&DAT_006dc23c)` / `FUN_00548230`), preloaded ×3 in `FUN_005ba930`.
- **Geometry:** 5 spawned billboard coin particles, factory type **6**; each is camera-billboard with gravity + bounce (accel at +0x244, bounce restitution +0x274, spin +0x270).
- **Anchor:** caster torso — `FUN_005c3870(0,-80f,0)`; coins scatter outward with random yaw (+0x290 = rand%12 · 30°) and outward velocity.
- **Sequence/timing:** one-shot, fires when frame `obj+0x110 == 0x14` (frame 20). Per-coin lifespan +0x198 = rand%30+40 (≈40–70 frames), fade at ~2/3 of life (+0x1a4). Loops the 5-coin emit block once.
- **Blend/colour:** coin sprite, alpha; +0x1a8=2 (blend mode 2).

**Cart Revolution — EF_CARTREVOLUTION (id 170 / 0xaa) — `FUN_005d9810` (0x5d9810)**
- **Sound:** `EF_MagnumBreak.wav` at caster (frames 7 and 20).
- **Textures:** `ring_yellow.tga` (ring A) + `&DAT_006db30c` (second ring/shock bmp). Note: a `CartRevolution.str` name also exists in the StrEffect table but this id is intercepted by the procedural handler, so the rings drive the visual.
- **Geometry:** two expanding **ring** emitters — factory type **0xc** (flag +0x17c |= 0x200 = upright/billboard ring, start size +0x2d0=0.5 growing) and type **0xe** (second ring, size 1.35 growing). Both flat-ish expanding discs.
- **Anchor:** caster torso `FUN_005c3870(0,-80f,0)`.
- **Sequence/timing:** one-shot at frames 7 & 20; each ring lifespan 20 frames, fade at life−15 (+0x1a4). Bails when frame ≥ 300.
- **Blend/colour:** yellow additive expanding shock rings.

**Cart Boost — EF_CARTBOOST (id 391 / 0x187) — dispatcher case 0x187 → shared `.str` path**
- **Sound:** `EF_IncAgility.wav` at caster (frame 0).
- **Textures/geometry:** `cart.str` (StrEffect ctor `FUN_005c2630` case 0x187 → `s_cart_str`); torso-anchored one-shot billboard sequence; lifespan `DAT_006d9100[0x187]`.
- **Anchor:** caster torso.

**Meltdown — EF_MELTDOWN (id 390 / 0x186)** (Blacksmith tree, adjacent) — case 0x186: `black_overthrust.wav` + `melt.str`.

**Pushcart / Change Cart** — no EF id, no dispatcher case, no `.str`. **Data-driven / none** (pure sprite/option-bit change; no effect object created).

---

### Alchemist

**Pharmacy / Prepare Potion result — EF_PHARMACY_OK (305 / 0x131) & EF_PHARMACY_FAIL (306 / 0x132) — shared `.str` path**
- **Textures:** `p_success.str` / `p_failed.str` (StrEffect ctor cases 0x131/0x132).
- **Geometry/anchor:** torso-anchored one-shot billboard sequence; lifespan `DAT_006d9100[id]`. No dedicated sound in the dispatcher (the `p_success_wav`/`p_failed_wav` strings exist but aren't keyed here → sound is data/UI-driven).

**Potion Pitcher / Aid Potion / Slim Potion Pitcher — EF_THROWITEM (298 / 0x12a) & EF_THROWITEM2 (299 / 0x12b) — `FUN_005e2950` (0x5e2950)**
- **Textures:** thrown-bottle bmp passed as arg (`&DAT_006dafdc` for 298, `&DAT_006dafb8` for 299).
- **Geometry:** single **ballistic projectile** quad, factory type **0x35**, ground-quad marker +0x17c=5. Parabolic arc computed from caster→target distance (fields +0xf708 dist, +0xf770/+0xf71c arc-height selected by the count arg param_3=2/3/other).
- **Anchor:** flies from **caster** origin (+0x13c/0x140/0x144) to **target** cell (+0x4/0x8/0xc); lands at target.
- **Sequence/timing:** one-shot; spawns when frame `obj+0x110 == 5` (298) or `== 10` (299); lifespan `DAT_006d9100[id]` (+0x114). Alpha blend (+0x29c=0), opacity 0xff.

**Demonstration / Bomb — EF_DEMONSTRATION (302 / 0x12e)** — **not in dispatcher switch, not in `.str` table** → hits `default:` "Invalid Effect Id". **Data-driven / none in this exe.**

**Acid Terror / Acid Demonstration — EF_ACIDDEMON (537 / 0x219)** — not in dispatcher, not in `.str` table. **Data-driven / none.**

**Slim Potion buff — EF_SLIM (497–499 / 0x1f1–0x1f3)** — not in dispatcher, not in `.str` table. **Data-driven / none** (the *throw* uses EF_THROWITEM above; the buff icon has no coded effect).

**Sphere Mine, Summon Flora, Summon Marine Sphere, Cart Boost(=Merchant, above)** — no EF effect id; these are summon sprites / server AoEs. **Data-driven / none** (no CEffect constructed).

---

### Bard / Dancer

All songs & dances are **persistent ground "bottom" effects**, handled procedurally in the dispatcher by two shared flat-ground-quad spawners:
- **`FUN_005fd800` (0x5fd800):** factory type **0x2f**, +0x17c=5, lifespan +0x198 = `DAT_006d9100[id]` (+0x114); single or random-variant texture (arg selects: 2→melody_a/b random, 5/8→random spell_/mist variants). Guard `obj+0x110==0` → spawns once.
- **`FUN_005fd430` (0x5fd430):** factory type **0x30**, +0x17c=5; scatters child note/sparkle sub-particles across the AoE with random yaw (rand%360°) and ±20 px offset; alpha blend (+0x29c=0).

Both are **ground/cast-cell anchored** (not caster/torso), flat quads laid on the floor, alpha-blended, persisting for the song's server-supplied lifespan. Per song:

| Skill (common alt name) | EF id | Spawner | Texture |
|---|---|---|---|
| Dissonance (BA_DISSONANCE) | 277 / 0x115 | `FUN_005fd430` | `ring_blue.tga` |
| Lullaby (ensemble) | 278 / 0x116 | `FUN_005fd800` | `zz.bmp` (sleep "Z") |
| Mr. Kim/RichmanKim | 279 / 0x117 | `FUN_005fd800` | `pocket.bmp` |
| Eternal Chaos | 280 / 0x118 | `FUN_005fd250` | `twirl.bmp` |
| Drum on Battlefield | 281 / 0x119 | `FUN_005fd800` | (default `DAT_0070ef24`) |
| Ring of Nibelungen | 282 / 0x11a | `FUN_005fd800` | `twirl.bmp` |
| Loki's Veil (RokisWeil) | 283 / 0x11b | `FUN_005fe760` | `safeline.bmp` |
| Into the Abyss | 284 / 0x11c | `FUN_005fd800` | (default) |
| Siegfried | 285 / 0x11d | `FUN_005fd250` | `twirl.bmp` |
| **Whistle** | 286 / 0x11e | `FUN_005fd800` | `melody_b.bmp` |
| **Assassin Cross of Sunset** | 287 / 0x11f | `FUN_005fd430` | `ring_red.tga` |
| **A Poem of Bragi** | 288 / 0x120 | `FUN_005fd800` | (default) |
| **Apple of Idun** | 289 / 0x121 | `FUN_005fd800` | `idun_apple.bmp` (preloaded ×6) |
| **Ugly Dance** | 290 / 0x122 | `FUN_005fd430` | `ring_red.tga` |
| **Humming** | 291 / 0x123 | `FUN_005fd800` | `melody_a.bmp` |
| **Please Don't Forget Me (aka Slow Grace)** | 292 / 0x124 | `FUN_005fd430` | `magic_green.tga` |
| **Fortune's Kiss (aka Lady Luck)** | 293 / 0x125 | `FUN_005fd800` | `kiss.bmp` |
| **Service For You** | 294 / 0x126 | `FUN_005fd430` | `safeline.bmp` |

**Frost Joke — EF_TALK_FROSTJOKE (295 / 0x127)** and **Scream — EF_TALK_SCREAM (296 / 0x128)** — dispatcher cases call `FUN_0060f910(1)` / `FUN_0060f910(0)`: a caster-anchored **speech/talk bubble** effect (not a particle burst).

**Arrow Vulcan — EF_VULCANWAV (776 / 0x308)** — beyond the dispatcher's switch range (max ~0x275); no case. **Data-driven / none** (arrow/hit visuals only).

**Musical Strike / Throw Arrow** — no dedicated EF id; reuse the generic **ArrowShot** effect (id 64 / 0x40 → `ArrowShot.str` in the StrEffect table). **Data-driven / `.str` (ArrowShot).**

**Marionette Control** — CG_MARIONETTE (Clown/Gypsy transcendent); no EF id present in this exe. **Data-driven / none.**

**Focus Ball** — no matching EF id / no coded effect. **Data-driven / none.**

---

### Not found (data-driven / `.str`-only / no coded effect in uaRO.exe)

- **Merchant:** Pushcart, Change Cart (sprite/option only — no effect object).
- **Alchemist:** Demonstration/Bomb (302), Acid Terror/AcidDemon (537), Sphere Mine, Summon Flora, Summon Marine Sphere, Slim Potion buff (497–499), Chemical Protection (300/303) — all hit `default:` "Invalid Effect Id" or have no id.
- **Bard/Dancer:** Arrow Vulcan (776), Musical Strike, Throw Arrow (fall back to `ArrowShot.str` id 0x40), Marionette Control, Focus Ball — no dedicated coded effect.
- Cart Boost (0x187) and Pharmacy OK/Fail (0x131/0x132) are **`.str`-driven** via `FUN_005c2630` (`cart.str`, `p_success.str`, `p_failed.str`), each with only a one-shot torso-anchored billboard sequence.

Key source locations (all `/root/BornRok/winEXE/decomp/uaRO_decomp.c`): master dispatcher `FUN_005bd3d0`; StrEffect id→filename ctor `FUN_005c2630`; Mammonite coin `FUN_005c9ff0` + coin update `FUN_00613770`; Cart Revolution `FUN_005d9810`; Potion-Pitcher throw `FUN_005e2950`; song ground-quad spawners `FUN_005fd800` / `FUN_005fd430`; EF_ id names from `/root/uAthena/db/const.txt`.
