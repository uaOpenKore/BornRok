# Statuses / generic hits / level-up / cast-circle geometry

> Per-skill effect dossiers reversed from `winEXE/uaRO.exe`. See [`README.md`](README.md) for the shared conventions and field meanings.

## Statuses, generic hits, level-up & cast-circle geometry — dossiers

Shared conventions observed in the particle factory `FUN_005c3670(type,x,y,z)` emitter struct (all offsets from the returned emitter base, stored in global `DAT_007710a8`):

| Offset | Meaning |
|---|---|
| `+0x17c` | orientation: **`5` = flat-on-ground quad**, otherwise camera billboard |
| `+0x198` | lifespan in frames |
| `+0x188` / `+0x1a4` | fade/attack sub-timers derived from lifespan |
| `+0xfc` | pointer to texture-handle array (`operator_new(count<<2)`) |
| `+0x1b8` | texture/frame count |
| `+0x190`(=400) | flags word (`|2` = additive/self-lit blend seen on Endure; `|0x200` valentine) |
| `+0x240`/`+0x244` | alpha & per-frame alpha delta |
| `+0x290`/`+0x294`/`+0x298` | spawn angle / pitch / spin |
| `+0x358..0x360` | linear velocity vector |
| `+0x2d8`/`+0x2e4`/`+0x2dc` | size start / size cap / size delta |
| `+0x29c` | quad scale |
| `+0xf704` | per-instance billboard/loop byte (`0`=one-shot billboard) |

Sound helper `FUN_004396b0(name, dx, 0, dz, 0xfa, 0x28, 0x3f800000)` is always called with the emitter position **minus the camera/listener position** (`FUN_004f6ad0()->0xc4->0x3c`), i.e. positional 3-D SFX, gain `1.0`.

---

### 1. Generic magic hit-sparks (per-magnitude EF_hit2..hit6)

Five escalating "magic bolt landed" bursts. All anchor at **target actor world pos** (`param_1+0xf4` → copies xyz into `+4/+8/+0xc`), one-shot, and all pre-multiply a colour from the caster palette via `FUN_00412cb0(&pos, paletteCopy, &out)` into `+0x130/+0x134` (per-quad tint, alpha `0xFF000000`).

**`FUN_005c46a0` — EF_hit2 (smallest)** `@0x5c46a0`
- Sound: `effect\EF_hit2.wav` at spawn.
- Loop spawns lens sprites until an accumulator exceeds `_DAT_006959f0`; alternates texture per iteration: even → `effect\lens1.tga`, odd → `effect\lens2.tga` (billboard, `+0xf704=0`).
- Each particle: lifespan `rand%20+10`; spin `+0x298 = (base − _DAT_0069f4d4)+rand%30`; alpha `+0x240 = (rand%45+5)*_DAT_0069c5fc` fading over life; size start `+0x2d8 = rand%15+5`, size cap `+0x2e4 = rand%20+20`, size delta negative (shrinks); vel from `sin/cos(rand)*(rand%5)` (small outward scatter) with gravity `− _DAT_0069f174`.
- `param_2!=0` overrides `+0x1f0`/`+500` to `0x78` (longer variant).

**`FUN_005c4a20` — EF_hit3** `@0x5c4a20`
- Sound: `effect\EF_hit3.wav`.
- Two central **`lens2.tga`** flare quads (type `0xd`, life `0x0f`, pitch `+0x294 = -90.0`, spin set to a random-derived angle) with differing brightness ramps (`+0x27c` 1.5 vs 4.0), then a loop of **8** debris sparks (type `5`, `lens`) via `FUN_005481c0(&DAT_006dbf2c)/FUN_00548230(&DAT_006dbf14)` colour pair, `+0x1a8=3`, committed by `FUN_006113a0()`. Debris scatter ±40 around angle, pitch −50..+50, outward speed `(rand%120+80)`.

**`FUN_005c5050` — EF_hit4** `@0x5c5050` — same structure as hit3 but **5** debris sparks and brighter core (`+0x27c=4.0`, faster alpha). Sound `effect\EF_hit4.wav`.

**`FUN_005c5540` — EF_hit5** `@0x5c5540`
- Sound `effect\EF_hit5.wav`. Emits **2** `lens2.tga` quads (type `0`) at angles `rand%360` and `+90°`; life `0x11`; strong downward vel `+0x35c = -20.0`, rise-then-fall via `+700 = -5.0`; size start `rand%5+20`, cap `rand%10+30`; brightness `+0x2e8=2.5`.

**`FUN_005c57c0` — EF_hit6 (largest)** `@0x5c57c0` — byte-identical shape to hit5 (2× `lens2.tga`, life `0x11`, angle & angle+90) with smaller sizes (`rand%5+10` / `rand%5+15`) and brightness `+0x2e8≈1.7`. Sound `effect\EF_hit6.wav`.

Colour/blend: all use the tinted-lens additive flare look; the `lens1/lens2.tga` textures are the round soft-flare sprites.

---

### 2. Level-up — `FUN_00582700` `@0x582700`

This is the stat/param packet handler (`switch(*(u16*)(param_2+2))`). Two cases raise level effects:

- **case 0x0B — Base level up:**
  - Sound: `levelup.wav` (2-D, `FUN_004396b0(...,0,0,0,...)` — non-positional, at listener).
  - Picks effect id by current job (`FUN_0063b080()`): most jobs → **effectId `0x173`** (383); Novice-ish (job `0x17`/`0xfcd`) or a range → `0x152` (338); jobs `0xfce`–below → `0x246`; job `0xfd2`–`0xfd4` → `0x152`. Spawns via `FUN_00549180(id,0,0,0,0)` then `FUN_004d42d0(0x15)`. If mounted/peco (`FUN_004ebd10`) also fires the mount's level effect through `FUN_0064b310(...,6,...)`.
  - Anchor: caster (self) — the standard rising level-up pillar.
- **case 0x37 — Job level up:** no dedicated wav here; picks **effectId `0x9e`** (158) if job-level < 51 else **`0x151`** (337), spawns via `FUN_00549180`, then `FUN_004d42d0(0x31)`.
- Cases 0x18/0x19 (HP/SP fractions) only drive UI colour warnings (`FUN_004db310`), no particle.

---

### 3. Status-ailment SFX + tint/explosions — `FUN_005518f0` `@0x5518f0`

Central status-change handler. `param_1` = actor object; new opt bits in `param_2/3/4`; `param_5` = "play sound" flag. Two visual channels: (a) **sprite tint** written to actor bytes `+0x85`(R?)/`+0x86`(G?)/`+0x21`... `+0x8f`(tint-enable)/`+0x8a`(freeze), and (b) **SFX** + optional self-effect spawns.

Body-state block `switch(param_1[0x9c])` (the "body" ailment: 1=petrified-progress,2=stone,3=stun,4=sleep,6=?):
- **case 2 — Stone curse (petrified):** action `0x3a`; tint set G=`0x80`, A=`0xff`, freeze on; Sound **`_stonecurse.wav`** (if `param_5`).
- **case 3 — Stun:** action `0x47`; Sound **`_stun.wav`**.
- **case 4 — Sleep:** action `0x45`.
- **case 1 — petrify-in-progress:** grey tint (`0x40/0x40/0x40`).

Transition (old body `switch(iVar7)`) fires the *break* explosions:
- **old case 1 → Stone explosion:** Sound **`_stone_explosion.wav`**.
- **old case 2 → Frozen shatter:** actions `0x3b`+`0x3e`; Sound **`_frozen_explosion.wav`**; resets tint to full white (`0xff/0xff/0xff`), clears freeze.
- old case 3 → action `0x48`; old case 4 → action `0x46`.

Opt1/opt2 status-bit block (bitmask in `param_1[0x9e]`):
- **Poison** (`&1`, or `&0x80` = deadly poison): tint R=`0xff` G=`0x80`(or `0x32` for deadly), A=`0xff`; Sound **`_poison.wav`**; on self, spawns status icon effect `FUN_00549180(0x14f,...)` if not already present.
- **Curse** (`&2`): action `0x43`; tint R=200,G=`0x32`; Sound **`_curse.wav`**.
- **Silence** (`&4`): action `0x49`; Sound **`_silence.wav`**.
- **Blind** (`&0x10`): Sound **`_blind.wav`**; on self spawns `FUN_00549180(0x14e,...)` (screen-darken icon).
- **Confusion** (`&8`): Sound **`_confusion.wav`** (no tint).
- **Freeze/other** (`&0x20`): actions `0x4b/0x4c`.
- Sight/Ruwach detection branches at top play **`EF_Sight.wav`**.

All status SFX are positional at the actor. The tint is a persistent per-actor sprite colour multiply (not a particle), toggled by `+0x8f`.

---

### 4. Endure motes — `FUN_005ca2b0` `@0x5ca2b0`

- Anchor: **caster** (`param_1+0xf4`), tinted-quad colour pre-baked via `FUN_00412cb0` into `+0x130/+0x134`.
- On first frame (`+0x110==0`): Sound **`effect\EF_Endure.wav`**; spawns ONE big ground-anchored quad — texture **`effect\endure.tga`**, life = `param_1+0x114`, flags `|2` (additive/self-lit), size start/cap `+0x2d8=+0x2e4=150.0` (`0x43160000`) shrinking (`+0x2dc/+0x2e8 = -5.0`), downward drift `+0x35c=-20.0`, one-shot billboard.
- Every frame while `+0x110 < 0x32` (first 50 frames): spawns a **converging mote** — texture **`effect\alpha_down.tga`**, life `0x28`, random spin `rand%360`, alpha ramp `+0x240=-4.0` (fades in), size `(rand%100+60)*_DAT_006969a4` → `rand%20+30`, and an **inward** velocity `sin/cos(angle)*(rand%40+100)` with gravity — i.e. blue-ish motes streaming inward to the body. Matches the memory note "blue converging motes."

---

### 5. Ground cast-circle geometry

Three related builders. All set **`+0x17c = 5` (flat-on-ground quad)** and anchor at caster.

**`FUN_005fbd40(textureName)` `@0x5fbd40` — "BeginSpell" trigger.**
- Copies caster pos; on first frame plays Sound **`effect\EF_BeginSpell.wav`** (positional), then calls `FUN_005fb880(0x2d, textureName, 3)` and `FUN_005fb880(0x19, textureName, 3)` — i.e. two stacked spinning rings (radii selected by the level/`0x19`,`0x2d` param, element mode `3`).

**`FUN_005fb880(radiusParam, textureName, mode) `@0x5fb880` — the multi-band spinning cast ring.**
- Type `0x1e` emitter, flat ground quad, life clamped ≥ `0x38`. Quad scale `+0x29c` chosen by `mode`: 1→8.0, 2→5.0, 3→10.0, 4→11.0, 5→4.0, 6→12.0, else 5.0.
- Builds **5 concentric bands** (each a struct stride `0xb8`, band base radii `+0xf708` etc.). Per band a start angle offset and a radius: outer radii e.g. `mode 2`→25/24/23/22 (`0x41c80000…`), `mode 0x16`→15/14/13/12, default→20/19/18/17 (`0x41a00000/0x41980000/0x41900000/0x41880000`). Each band spans a full `0x168` (360°) sweep, rotation speed `+0xf770 = 4.1` (`0x40833333`), with staggered phase (`param_2 + 0x87/0x5a/0x2d/0`) and a per-band angular step `iVar7 -= 5` over `0xb8`-stride rows (21 sub-samples each, `iVar3 < 0x15`). This is the classic rotating rune-ring that scales with skill level.

**`FUN_005fbdd0(textureName, x, z) `@0x5fbdd0` — single ring-node placer (called ~20× to trace the ring).**
- Type `0x6e` emitter (the dedicated ground-ring type), flat quad `+0x17c=5`, scale `+0x29c = 10.0` (`0x41200000`), single texture. Two nested rotating sub-bands (`+0xf706..` and `+0xf7bc..`) sweeping `0x168` with radii 10.0/25.0 and rot speeds 2.3/2.0, random start angle `rand%0x168`, life tied to `param_1+0x114 − 0x19`.
- **Ring math (from caller `@0x58c3xx`, effectId 0x2b / case-block 362544-362563):** twenty `FUN_005fbdd0` calls place nodes at explicit (x,z) offsets around the caster. Decoded, they form a closed ~20-point loop, ≈18° apart, radius ≈10–17 world units (a slightly egg-shaped ring, not a perfect circle):

```
idx (x, z)        r     angle°
 0 ( 0.00,  9.99) 9.99   90
 1 (-3.51, 11.88) 12.4  106
 2 (-7.83, 12.96) 15.1  121
 3 (-12.42,11.34) 16.8  138
 4 (-15.12, 7.02) 16.7  155
 5 (-15.39, 1.62) 15.5  174
 6 (-13.77,-3.24) 14.2  193
 7 (-10.80,-7.02) 12.9  213
 8 (-6.75,-10.26) 12.3  237
 9 (-2.70,-12.69) 13.0  258
10 ( 0.00,-16.20) 16.2  270
11 ( 2.97,-12.69) 13.0  283
12 ( 7.02,-10.26) 12.4  304
13 (11.07, -7.02) 13.1  328
14 (14.04, -3.24) 14.4  347
15 (15.66,  1.62) 15.7    6
16 (15.39,  7.02) 16.9   24
17 (12.69, 11.34) 17.0   42
18 ( 8.10, 12.96) 15.3   58
19 ( 3.78, 11.88) 12.5   72
```
Each node is an independently spinning flat ring sprite; together they draw the animated ground summon-circle.

**Element ring resolution — `FUN_00644ed0` `@0x644ed0`.** Maps a skill/effect id (`param_1`) + element (`param_4`) to a cast-circle effect id (`*param_2`) and a layout/type (`*param_3`). The tail element switch is the key colour selector: when the base id is `0xc`, element `param_4` remaps to ring effect ids **1→`0x36` (red/fire), 2→`0x38` (blue/water), 3→`0x37` (green/wind), 4→`0x39` (earth), 5→`0x3b` (dark/undead), 6→`0x3a` (holy/ghost)** — i.e. the classic `0x36`–`0x3B` element cast-circle band. Other ids route to `0x156/0x157` (the animated red spell rings above), `0x1b9`, `0x1f5/0x1f6`, `0x23c–0x23f`, `-1` (none).

**Element ring textures** (loaded via `FUN_0040c590`, from the 300+ effect dispatch `switch(*(effectId)-300)` at `@~0x58c318`):
- `effect\ring_white.tga` (cases 0xd, 0x19)
- `effect\ring_purple.tga` (case 0xe)
- `effect\ring_red.tga` (cases 0x12, 0x2a→`FUN_005fbd40`, 0x2b→20-node ring)
- `effect\ring_blue.tga` (case 0x15)
- `effect\ring_yellow.tga` (case 0x27)
- `effect\ring_black.tga` (used at line 363182, dark element)
- plus `effect\alpha_down.tga` (case 0x13, the fading-alpha ground fade shared with Endure).

The `magic_violet` / `magic_green` variants belong to the same `ring_*` family selected by the element remap above; in this build the element bands 0x36–0x3B pull the coloured `ring_*.tga` set (red/blue/green/earth-yellow/dark/holy).

---

### 6. Wedding — `FUN_005e0420` `@0x5e0420`

- Anchor: caster, flat ground quad (`+0x17c=5`). Sound **`effect\wedding.wav`**.
- Type `0x4e` emitter, `+0x1b8=1` (single texture, name passed as `param_2`). Builds a **ring of falling petals/hearts**: loop stride `0xb8` up to `0x2e0` (≈8 nodes), each with `+0xf76c = sVar6*0x2d` (45° increments → 8 points), random 3-axis rotation (`rand%0x168` on `+0xf718/71c/720`), random world offset `+0xf78c…794` in ±50/−110 (downward drift), size `(rand%31)*k+base`. One-shot, camera-billboard petals raining over an 8-point ground ring.

### 7. Valentine — `FUN_00610830` `@0x610830`

- Anchor: caster. Sound **`effect\vallentine.wav`** (note double-L spelling in the asset).
- Type `5` emitter, flags `|0x200`, life `param_1+0x114`. Colour pair chosen by `param_2`:
  - `2` → `FUN_005481c0(&DAT_006ddb4c)/FUN_00548230(&DAT_006ddb34)`, mode `0x67=2`.
  - `3` → colour `&DAT_006ddb24/&DAT_006ddb14`, mode 4.
  - `4` → sets flag `+0x5f|0x200`, colour `&DAT_006ddafc/&DAT_006ddae4`, mode 4.
  - default → colour `&DAT_006ddacc/&DAT_006ddab4`, calls `(vtbl+0x1c)(param_2,0x10,1)`.
- Committed via `(*(+0x1c))(0,0x10,1)` + `FUN_006113a0()` — a rising tinted hearts burst; the colour tables are the pink/red valentine palettes.

---

Key file: `/root/BornRok/winEXE/decomp/uaRO_decomp.c`. All addresses above are the `/* ==== <addr> FUN_<addr> ==== */` anchors in that file. Float constants decoded: `0x41200000`=10.0, `0x41c80000`=25.0, `0x43160000`=150.0, `0xc0a00000`=−5.0, `0x40833333`=4.1, `0x40133333`=2.3, `0x168`=360 (angle span in degrees), `0x3f800000`=1.0 gain.
