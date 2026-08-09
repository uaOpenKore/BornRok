# Wizard

> Per-skill effect dossiers reversed from `winEXE/uaRO.exe`. See [`README.md`](README.md) for the shared conventions and field meanings.

## Wizard — skill effect dossiers

### Conventions used below (address-anchored)
- Age/tick counter = `*(emitter+0x110)`; per-skill param = `+0x114`. All phase gates test `+0x110`.
- Sound helper `FUN_004396b0(name, x, 0, z, 0xfa, 0x28, 0x3f800000)` = vol 250, pan/range 40, pitch 1.0; position is `effectpos − listenerpos` (`FUN_004f6ad0()→+0xc4→+0x3c` is the listener/camera object at +4/+0xc).
- `FUN_005c3670(type,x,y,z)` = particle-emitter factory; texture list stored at `+0xfc` (`operator_new(count<<2)`, count at `+0x1b8`), lifetime `+0x198`, fade-start `+0x1a4`, anim frame count `+0x188`, size `+0x29c/+0x2e4`, velocity `+0x358/+0x35c/+0x360`, blend/render flags word at `+0x190` (shown as `+400`), geometry flags at `+0x17c` (`|=2` upright/oriented quad; ground-quad marker `=5` / `|0x200`).
- `.spr/.act` particles set via `FUN_005481c0(spr)` + `FUN_00548230(act)` + `FUN_006113a0()` (real sprite, not a texture quad).
- `FUN_005c3870(0,0xc2a00000,0)` = torso-anchored billboard update at y=−80.0, one-shot; returns alive flag. The one-line wrappers do nothing but sound + this call.
- Scene call `(**(*FUN_004f6ad0()+0x14))(0x87,…)` = screen shake/flash trigger fired at impact ticks.

---

### Storm Gust — `FUN_005d82e0`
- **Sound:** `effect\wizard\stormgust.wav` once at tick 0 (`+0x110==0`), L377002.
- **Geometry/particles:** on every tick where `+0x110 % 0x1d==0 && 0x1e<tick<0x97` (every 29 frames, ~frames 30–150), spawns **2** particles (`local_c=2` loop) of factory **type 9**, texture `effect\ice.tga` (single, L377051). Blend flag `+0x190 |= 1`. Lifetime `+0x198 = 0xd7−tick` (215−tick), fade `= life−10`. Anim frames `+0x188=3`.
- **Motion:** random angle (`_rand()%0x168` = 0–360°) drives velocity `+0x358 = cos*r`, `+0x360 = −sin*r` with radius `r = _rand()%0x19+3` (3–27); vertical vel `+0x35c=0x41a00000` (20.0); size `+0x29c=(rand%100+400)*scale`, `+0x2e4=20.0`; rotation `+0x290=rand%360`, spin `+0x294=100.0`.
- **Anchor:** ground/world, torso billboard update L377057.
- **Sequence:** continuous swirling ice-shard blizzard, one shard-pair per 29 ticks; **one-shot per tick**, no impact sub-emitter. Gated off if `DAT_00772d2c!=0` (low-effects setting).
- **Blend/colour:** ice.tga additive/alpha shards, `+0x35c` upward drift.

### Meteor Storm — cast marker `FUN_005d8bb0`
- **Sound:** `effect\wizard\meteor.wav` at tick 0 (L377290).
- **Impact hook:** at tick `0x28` (40), scene `0x87` screen-shake (L377295).
- **Geometry:** none itself — torso billboard update only (L377297). Pure sound/shake shell; the visible meteor uses the impact fn below.

### Meteor Storm — impact `FUN_005d8090`
- **Sounds (switch on `+0x110`):** tick 0 → `effect\wizard\meteo.wav` **and** `effect\hunter\blastmine.wav` (L376937/376941); tick 10 → `blastmine.wav`; ticks 0x14,0x32,0x50,0x64,0x82,0x8c,0xb4,0xc8 (multiples of ~20/50) → `effect\wizard\meteo.wav` (L376964). Final unconditional `FUN_004396b0(pcVar4,…)` L376966 plays the selected clip.
- **Impact hook:** when `0x31<tick<300 && tick%0x32==0` (every 50 frames) → scene `0x87` shake (L376969).
- **Geometry:** no particle spawns here (drives sound/shake cadence for the falling-meteor rocks); torso billboard update L376973.

### Water Ball — splash ring `FUN_005d2ff0`
- **Sound:** `effect\wizard\waterball_chulung.wav` at tick 0 (L374521).
- **Geometry:** one factory **type 0xf** emitter (L374525). Flags `+0x190=0x40 |=0x100`; **ground-quad**: `+0x17c |= 2`; `+0x208=1`. Lifetime `+0x198 = param(+0x114)`. Sizes `+0x2d8=+0x2e4=3.5`. Vertical drift `+0x338 = −(const/life)`, `+0x35c=−5.0`. Anim `+0x19c=5`, **3** textures: `effect\water_out_a.bmp`, `_b.bmp`, `_c.bmp` (L374539-374544) — sequential splash animation.
- **Anchor:** ground/caster cell.
- **Blend:** water sprite, upward-fading.

### Water Ball — projectile `FUN_005d31e0`
- **Sound:** `waterball_chulung.wav` at tick 0 (L374566).
- **Geometry:** factory **type 8** projectile (L374569), lifetime `+0x198=0x28` (40). Velocity `+0x35c=−10.0`; travels from source toward target — direction from `FUN_00544d20/00544da0(param+0x13c,+0x144)` (target vector), stored heading `+0x2cc`, per-frame position toward target `+0x364/0x368/0x36c = pos+delta`. Angular params `+0x270=3.5`, `+0x2b8=−2.0`. Uses `.spr/.act` **`DAT_006dc880`/`DAT_006dc868`** (L374601-374602), `+0x1a8=0xc`. `+0x29c=0.75` size.
- **Anchor:** flies caster→target.
- **Blend:** sprite-based water bolt.

### Ice Wall — `FUN_005cf710`
- **Sound:** `effect\wizard\icewall.wav` at tick 0 (L373083).
- **Geometry:** sets `+0x114=9999` (near-permanent). Loop `local_8=−2..3 step 2` → **3** particles, factory **type 9**, texture `effect\ice.tga` each (L373128). Blend `+0x190 |= 1`. Lifetime `+0x198=9999`, fade `life−10`. Anim frames `+0x188=0x14` (20). Velocity: `+0x35c=30.0` (up), `+0x360=−(local_8*sin(rand angle))`; size `+0x29c≈base+rand`, `+0x2e4=(rand%100+300)*scale`; `+0x240=3.0`, growth `+0x244`; `+0x2a8=200.0`.
- **Anchor:** ground wall cells, persistent (no billboard call — long-lived static shards).
- **Blend:** stacked ice.tga shards forming the wall.

### Heaven's Drive / Earth Spike (stone geyser) — `FUN_005cf9f0(param_1, kind)`
- **Sounds by `kind`(param_2):** kind 0 → `effect\wizard\earthspike.wav`, texture `effect\stone.bmp` (L373171/373174); kind 2 → `EF_IceArrow_d.wav` (random 1–3 variant via `FUN_006713a8`), texture `effect\ice.tga` (L373178/373183).
- **Impact:** scene `0x87` with arg 1 (L373186); also sets a light/colour vector via scene `0x94` (RGB `1.0, 0.24, 0.20`, L373188-373190) — reddish flash.
- **Geometry — center burst:** one factory **type 9** (L373194), `+0x198=0xf0` (240) life, flags `+0x190=1 |=2`, up-vel `+0x35c=10.0`, size `+0x29c=(rand%50+300)*scale`, `+0x2e4=18.0`, `+0x240=1.0` grow `+0x244=0.01`, `+0x2a8=250.0`, anim `+0x188=0xf`, gravity `+0x220=−1.2`; texture = selected `param_2` bitmap.
- **Ring of spikes:** `do` loop `iVar3=0x3c..<0x1a4 step 0x3c` (**~10 more** emitters, type 9 each, L373258) fanning outward at angles `local_8` (0–360 clamp), radial velocity from random angle, `+0x188=2` anim frames, same stone/ice texture. Total ≈ 1 center + 10 radial.
- **Anchor:** ground AoE around cast cell.
- **Blend:** stone/ice debris kicked upward + outward, red light flash.

### Earth Spike (grid) — `FUN_005d5010`
- **Sound:** `effect\wizard\earthspike.wav` tick 0 (L375396).
- **Impact:** scene `0x87` at tick 0 (L375400); adds `+0x114 += 0x32`.
- **Geometry:** nested loop `local_c,-10..<0xf step 5` × `local_8,-10..<0xf step 5` → **5×5 = 25** emitters, factory **type 9** (L375406), texture `effect\stone.bmp` (L375451). Grid offset placed via velocity `+0x358=local_8`, `+0x360=local_c`, `+0x35c=10.0`. Life `+0x198=param+(-10)+rand%0x1e` (jittered). `+0x188=0xe` anim, `+0x1b4=0xb`, tilt `+0x22c=−1.0`, `+0x238=−0.01`, gravity `+0x220=−1.2`. Blend `+0x190=1 |=2` (upright quad).
- **Anchor:** ground grid of stone spikes.
- **Blend:** 25 upthrust stone quads.

### Sightrasher — `FUN_005ce790`
- **Sound:** `effect\wizard\sightrasher.wav` at tick 0 (L372690); also fires scene cmd `(vtbl+8)(0,0x6d,0x16,…)` (light) L372687.
- **Geometry — radial shards ×2 fans:** at `tick%5==0 && tick<0x14` (ticks 0,5,10,15), two `do`-loops of **8** each = 16 `.spr/.act` particles, factory **type 5** (L372708/372761). First fan uses spr/act `DAT_006dc2a4`/`DAT_006dc290` (L372745-372746); second fan uses **`Shadow.spr`/`Shadow.act`** (L372787-372788). Render flag `+0x190 |= 0x400`. Lifetime `+0x198=0x55−tick`. Radial velocity from `cos/sin(rand)` scaled by `(tick/5)+const`; per-particle spin `+0x19c`; fade `+0x1a4=0x41−tick`, `+0x1b4=0x23−tick`.
- **Ground ring:** at tick 10, one factory **type 0xc** (ring) emitter (L372798), `+0x198=0x1e` (30), expanding `+0x2d0=2.5`, `+0x278=−10.0`, `+0x2a8=150.0`, texture `effect\ring_yellow.tga` (L372810).
- **Anchor:** caster-centered burst + ground ring.
- **Blend:** yellow additive shards radiating out + expanding ground ring.

### Quagmire — `FUN_005d8b30`
- **Sound:** `effect\wizard\quagmire.wav`, only when `_rand()%0xc==0` at tick 0, at y=−100.0 (below ground, L377267-377269).
- **Geometry:** none of its own — torso billboard update only (L377272). Visual is the persistent ground `.str`/sprite; this fn just does the occasional bubble sound.

### Fire Pillar — set/arm `FUN_005d8c40`
- **Sound:** `effect\wizard\fire_pillar_a.wav` at tick `0x4b` (75) (L377314). Billboard update L377318. Arm-phase shell (no particles).

### Fire Pillar — trigger `FUN_005d8cb0`
- **Sound:** `effect\wizard\fire_pillar_b.wav` at tick 0 (L377335). Billboard update only.

### Fire Pillar — eruption `FUN_005d8de0`
- **Sound:** `fire_pillar_b.wav` at tick `0x1e` (30) (L377385).
- **Impact:** scene `0x87` shake at tick `0x23` (35) (L377391). Billboard update only; no particle spawns (the flame column is a `.str`).

### Fire Ivy — `FUN_005d3460`
- **Sound:** `effect\wizard\fire_ivy.wav` at tick 0 (L374625).
- **Geometry:** one factory **type 0xf** emitter (L374629), ground-quad `+0x17c |= 2`, flags `+0x190=0x40 |=0x100`, life `+0x198=param(+0x114)`, sizes `+0x2d8=+0x2e4=3.5`, drift `+0x338=−(const/life)`, `+0x35c=−5.0`, single texture `effect\fire_ivy.bmp` (L374640).
- **Anchor:** ground cell (target).
- **Blend:** single animated fire-ivy quad rising.

### Sight (status/option apply) — `FUN_005518f0`  ⚠ not the Sight-skill graphic
This is the actor status-bits handler; it plays many status sounds and toggles sprite tints. Key facts:
- `effect\EF_Sight.wav` played when the relevant option toggles on (L272 blocks L280672 / L280678).
- Status sounds, each with `param_5!=0` gate: `_stone_explosion.wav` (state1), `_frozen_explosion.wav` (state2, also sets frozen tint 0xff/0xff/0xff), `_stonecurse.wav`, `_stun.wav`, `_poison.wav` (green tint + status icon `FUN_00549180(0x14f)`), `_curse.wav` (tint 200/0x32/0x32), `_silence.wav`, `_confusion.wav`, `_blind.wav` (icon `0x14e`). (L280904-281093)
- Drives sprite colour bytes at `+0x85/0x86/0x21` and calls actor vtbl `+8` with EF ids to attach/detach status sprites. **No .str/particle spawn** — status feedback only.

---

### Jupitel-Thunder-family emitters (thunder textures, near the set)
Two functions build the lightning-ball visuals; both take a `param_2` variant bool that swaps the classic thunder bitmaps for `twirl_soft`/`pokjuk` variants. Sounds attached in the decompile are hunter-trap clips (effect reuse), so skill attribution is via the caller/effectId table, not the sound.

**`FUN_005d0f50(param_1, variant)` — thunder ball projectile**
- **Sound:** `effect\hunter\shockwavetrap.wav` at tick 0 (L373712) [reused clip].
- **Geometry:** two factory **type 0xf** emitters. (1) core: life `+0x198=0x14`, flags `+0x190=0x40|0x400`, `+0x17c|=2`, size `+0x2d8/+0x2e4=3.5`, aimed by target vector (`FUN_00544d20/da0` L373723/373729), texture `effect\thunder_center.bmp` (variant→`twirl_soft.bmp`) L373740-373746. (2) ball: `+0x208=1`, size 4.5, **6** textures `thunder_ball_a…f.bmp` (variant→twirl_soft mix) L373772-373797, anim `+0x19c=1`.
- **Anchor:** flies toward target (velocity `+0x240=−targetdist/life`); billboard.

**`FUN_005d1480(param_1, variant)` — thunder plazma impact**
- **Geometry:** at `tick%0x14==0` spawns a **type 0xf** flash quad, texture `thunder_pang.bmp` (variant→`pokjuk_d.bmp`) L373840-373845, life 10, `+0x17c|=2|0x200`. At tick 10, a second **type 0xf** burst, life 300, `+0x208=1`, size 7.5, **5** textures `thunder_plazma_blast_a/b.bmp` + `thunder_ball_d/e/f.bmp` (variant→twirl mix) L373862-373885.
- **Anchor:** target cell, ground-oriented (`+0x17c|=2|0x200`).

**`FUN_005d1cb0` — ground lightning field (Lord-of-Vermilion-style)**
- **Sound:** `effect\hunter\flasher.wav` at tick 0 (L374045) [reused clip].
- **Geometry:** one central **type 0xf** emitter life `+0x198=0x46` (70), keyframed size/alpha via `FUN_00427180` on channels `+0x2fc/+0x30c/+0x31c` (scale ramps 40→120→200, L374081-374091), texture `effect\thunder_center.bmp` (L374095). Then a `do`-loop of **0x14 = 20** factory **type 3** bolt particles (L374102), each at random angle `+0x290=rand%360`, random size/velocity, texture `EFFECT\alpha_center.tga` (L374130).
- **Anchor:** ground AoE, 20 random lightning strikes around center over life 70.
- **Blend:** additive thunder_center core + 20 alpha_center bolt flashes.

### ThunderStorm sound shells (Mage/LoV audio)
- `FUN_005cba20`: plays `effect\magician\thunderstorm.wav` at tick 0, billboard-only shell (L371438).
- `FUN_005cba90`: snaps to owner pos, plays `effect\EF_ThunderStorm.wav` at tick 0, then one **type 5** `.spr/.act` particle `DAT_006dc368`/`DAT_006dc354` with flags `+0x17c|=0x200`, `+0x190|=0x200`, `+0x19c=2` (L371462-371474).

**Source:** all facts from `/root/BornRok/winEXE/decomp/uaRO_decomp.c` at the cited line numbers.
