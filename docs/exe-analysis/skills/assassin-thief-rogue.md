# Assassin / Thief / Rogue

> Per-skill effect dossiers reversed from `winEXE/uaRO.exe`. See [`README.md`](README.md) for the shared conventions and field meanings.

## Assassin/Thief/Rogue — skill effect dossiers

All positional sounds go through `FUN_004396b0(name, dx, 0, dz, 0xfa, 0x28, 0x3f800000)` where `dx/dz = caster_world − camera_target` (i.e. caster-anchored, panned by 250/40 fall-off, volume 1.0). All particle emitters come from `FUN_005c3670(type,0,0,0)` and the returned emitter is stored in the global `DAT_007710a8`. Frame counter is `*(param+0x110)`; `param+0x114` is the caller-supplied lifespan; `+0x44`/`param_1[0x44]` in the shared updater is the same frame counter. Emitter fields referenced repeatedly: `+0x198` = particle count / lifespan, `+0x1a4` = fade-in cutoff frame, `+0x17c=5` = ground-quad marker, `+0xfc` = texture-pointer array, `+0x240/0x244` = alpha & alpha-delta, `+0x29c` = particle size, `+0x290/0x294` = spawn angles, `+0x358..0x360` = velocity vector.

---

### Sonic Blow — `FUN_005d3a10` (per-actor updater, no effectId; runs while `+0x110==0`)
- **Sound:** `effect\assasin\sonicblow.wav` — once, on frame 0 only.
- **Textures:** `effect\magic_red.tga` (single texture, loaded into the emitter's texture array).
- **Geometry:** one particle emitter of `FUN_005c3670` **type 0xd**; billboard/quad particle system (12 particles — `+0x198 = 0xc`). Not a ground quad.
- **Anchor:** caster (world pos read from `param+4/+0xc`); torso-height offset applied via the velocity/gravity block.
- **Sequence & timing:** one-shot burst on frame 0. `_rand` seeds a random yaw `local_8` (0–359°, `+0x290`) and a fixed pitch `-90°` (`+0x294 = 0xc2b40000`). Velocity (`+0x334..0x33c`) computed from orientation matrix minus 2× a basis vector (an outward-thrust vector), plus random per-axis scatter (`+0x358` = 1–2, `+0x35c` ≈ −11…−1). Alpha base `+0x240 = 0.4` (`0x3ecccccd`) with per-frame delta scaled by count; size params `+0x278=4.0`, `+0x27c=7.0`, `+0x2e4=3.5`. Fade cutoff `+0x1a4 = count − count/2`.
- **Blend/colour:** red additive streak field (magic_red.tga, red slash motes) — the classic Sonic Blow red-slash sparks.

### Venom Splasher — `FUN_005d9480` (effect drives via torso helper)
- **Sound:** `effect\assasin\venomsplasher.wav` — once, fired when frame `+0x110 == 10` (delayed 10 frames, i.e. on the "burst" beat, not cast start).
- **Textures/Geometry:** no particle system built here; the visible body is a `.str` effect (`VenomSplasher.str`, index 0x81 in the .str dispatcher). This routine only handles sound + anchoring.
- **Anchor:** torso — returns `FUN_005c3870(0, 0xc2a00000, 0)` i.e. anchored −80.0f up the torso (`0xc2a00000 = −80.0`).
- **Timing:** one-shot; helper returns bool for lifetime.

### Enchant Poison — `FUN_005d43b0` (self-buff variant) & `FUN_005d4610` (repeat/apply variant)
- **Sound:** both play `effect\assasin\enchantpoison.wav`. `FUN_005d43b0` *also* plays `effect\EF_PoisonAttack.wav` at frame 0 (double sound). `FUN_005d4610` re-triggers the enchantpoison.wav at frames 0, 5, 11, 18, 26, 40 (`0/5/0xb/0x12/0x1a/0x28`).
- **Textures:** set via `FUN_005481c0(&DAT_006dc950)` / `FUN_00548230(&DAT_006dc938)` — the poison-dust particle sprite pair (green venom motes).
- **Geometry:** `FUN_005c3670` **type 5** (soft additive mote), 0x28 = 40 particles per emit.
- **Anchor:** caster/torso.
- **Sequence & timing:** looping spawner. `FUN_005d43b0` emits every 5th frame (`%5==0`); `FUN_005d4610` emits every 3rd frame (`%3==0`) — denser. Each mote: random yaw 0–359° (`+0x358..0x360` = velocity = `(rand%6+2)` × basis + origin), fixed pitch 90° (`0x5a`). Alpha `+0x240 = (rand%30+30)` (43b0) or `(rand%50+30)` (4610) scaled; size `+0x29c = (rand%50+40)` (43b0) / `(rand%20+20)` (4610); `+0x2b8=3.0`, size-delta `+0x2ac`. Fade cutoff `+0x1a4 = count − count/5`.
- **Blend/colour:** green poison particles, additive rising cloud around the caster/weapon.

### Cloaking — `FUN_005d37d0`
- **Sound:** `effect\assasin\cloaking.wav` — once on frame 0.
- **Textures/Geometry:** none — no particle emitter. Calls `FUN_00544f20(0x32, 0xffffffff,0xffffffff,0xffffffff)` (a 50-frame full-white screen/actor fade helper) and clears `*(param+0x90)=0` (a render/visibility flag on the actor).
- **Anchor:** caster (self actor).
- **Timing:** one-shot state toggle; the white flash marks the cloak transition.
- **Blend/colour:** white fade, no sprite.

### Poison React — `FUN_005d9330` & `FUN_005d9410` (identical bodies)
- **Sound:** `effect\assasin\poisonreact.wav` — once on frame 0 (both functions).
- **Textures/Geometry:** none built here; visible body is `PoisonReact.str` / `PoisonReact_1st.str` (.str dispatcher cases 0x7f / 0x7e). These routines only do sound + torso anchor.
- **Anchor:** torso — `FUN_005c3870(0, 0xc2a00000, 0)` (−80.0f).
- **Timing:** one-shot; bool return for lifetime. (Two entry points = the two React stages, both wired to the same sound + anchor.)

### Envenom — `FUN_005d7cc0`
- **Sound:** `effect\thief\invenom.wav` — once on frame 0.
- **Textures/Geometry:** none here; visible body is a `.str` effect. Routine = sound + torso anchor only.
- **Anchor:** torso `FUN_005c3870(0, 0xc2a00000, 0)` (−80.0f).
- **Timing:** one-shot; bool lifetime.

### Steal — `FUN_005c8400`
- **Sound:** `effect\EF_Steal.wav` — once on frame 0.
- **Textures:** `FUN_005481c0(&DAT_006dc110)` / `FUN_00548230(&DAT_006dc0f8)` — the steal sparkle sprite pair (yellow sparkle motes).
- **Geometry:** `FUN_005c3670` **type 6**; a burst of **10 particles** (fixed `do{…}while(iVar4)` loop, iVar4=10). Billboard motes.
- **Anchor:** caster/torso.
- **Sequence & timing:** one-shot 10-particle spray on frame 0. Per particle: random yaw (`+0x290 = rand%360`), random pitch (`+0x294 = rand%100 − 50`), velocity magnitude `rand&3 +4` along basis (`+0x364..0x36c`) with a `−_DAT_00696b70` downward bias on the y-component; lifespan `+0x198` = caller lifespan; alpha `+0x240 = (rand%50+50)`, alpha-delta positive; a second alpha channel `+0x270 = (rand%90+60)`; size `+0x29c = (rand%100+100)` with **negative** size-delta (`+0x2a0`, shrinking). Fade cutoff `+0x1a4 = count − count/3`.
- **Blend/colour:** yellow/gold sparkle motes flung outward then shrinking — the "gotcha" glitter.

### Detoxify — `FUN_005c8710`
- **Sound:** `effect\EF_Detoxication.wav` — once on frame 0.
- **Textures:** `FUN_005481c0(&DAT_006dc060)` / `FUN_00548230(&DAT_006dc048)` — detox particle sprite pair (green cure motes).
- **Geometry:** `FUN_005c3670` **type 5**, 40 particles (`+0x198=0x28`).
- **Anchor:** caster/torso.
- **Sequence & timing:** looping spawner with **two cadences gated by `DAT_00772d2c`**: normal → emit every 5th frame; alt (min-effect mode) → emit every 15th frame (`%0xf`). Per mote identical to Enchant-Poison motes: random yaw, pitch 90°, velocity `(rand%6+2)`×basis, alpha `(rand%30+30)`, size `(rand%50+40)`, `+0x2b8=3.0`, fade cutoff `count−count/5`.
- **Blend/colour:** green rising cure cloud (same particle family as Enchant Poison, different sprite pair).

### Stone Curse — `FUN_005d3840` (cast-on-target) & `FUN_005d4850` (petrify-apply)
- **Sound:** `effect\EF_StoneCurse.wav` — once on frame 0 (both). `FUN_005d4850` additionally plays `effect\black_overthrust.wav` on frame 0 and forces lifespan `param+0x114 = 9999` (long-lived petrified state).
- **Textures:** `effect\alpha_center.tga` (single texture into emitter array).
- **Geometry:** both build a color-transform (`FUN_00412cb0` blends the target's colour toward the emitter tint via a 12-float matrix copied from the camera/context at `+0xc8+0x94`, writing result to `param+0x130/0x134`). Emitter `FUN_005c3670` **type 4** (ground/impact quad). One quad.
- **Anchor:** target/ground (colour matrix tints the target sprite grey; the alpha_center quad marks the spot). `+0x35c = −20.0` (`0xc1a00000`) raises it; scale `+0x20c=10.0`, `+0x278=10.0`, `+0x2d0=7.0`.
- **Sequence & timing:** one-shot. `+0x1a4 = 0` (no fade-in delay). Local colour set to black (`local_14/10/c = 0`) → target desaturates to stone-grey. `FUN_005d3840` uses caller lifespan; `FUN_005d4850` pins it to 9999.
- **Blend/colour:** target sprite tinted toward black/grey (petrify), plus a central alpha-blend flash quad.

### Poison Attack — `FUN_005d4a70`
- **Sound:** `effect\EF_PoisonAttack.wav` — once on frame 0.
- **Textures:** `FUN_005481c0(&DAT_006dc950)` / `FUN_00548230(&DAT_006dc938)` — same green venom sprite pair as Enchant Poison.
- **Geometry:** `FUN_005c3670` **type 5**, 40 particles (`+0x198=0x28`).
- **Anchor:** target/torso; forced `+0x35c = −20.0` (`0xc1a00000`) after velocity setup, biasing motes upward from a low anchor.
- **Sequence & timing:** emits every 5th frame (`%5==0`). Per mote: random yaw, pitch 90°, velocity `(rand%6+2)`×basis; alpha `+0x240 = (rand%30+30)` scaled by `_DAT_0069fa20`; size `+0x29c = (rand%50+40)`, `+0x2b8=3.0`, fade cutoff `count−count/5`.
- **Blend/colour:** green additive poison spray — same family as Envenom/Enchant Poison but target-anchored and low.

---

### Rogue branches inside the shared updater `FUN_005bd3d0`
This is the giant per-effect dispatcher (`switch(param_1[0x40])` = effectId; `param_1[0x44]` = frame counter). The three Rogue effects:

**Intimidate — case `0xe3` (effectId 227).**
- **Sounds:** `effect\EF_Bash.wav` at frame 0 (the snatch/hit), then `effect\rog_intimidate.wav` at frame `0x23` (35) — the teleport-grab beat.
- **Sub-emitter:** loops `FUN_005ff300()` **20 times** (0x14) per frame-0 build.
  - `FUN_005ff300`: `FUN_005c3670` **type 0x2b**, ground-marked (`+0x17c=5`), texture `effect\white01.bmp`. Fills a 640-slot particle array (stride 0xb8, up to 0x2e0) of upward white streaks: per particle vertical velocity `+0xf70c = −70 − rand%30` (strong upward), gravity `+0xf770 = 12.0`, random yaw/pitch angles (`rand%360`), spawn offset from caster feet (`param+0x13c`), lowered by `_DAT_0069fad0`. Flag byte `+0xf774 = 0` (blend mode 0).
- **Anchor:** caster feet/ground (`param+4/8/c` ← target node `+0xf4`, y lowered).
- **Blend/colour:** white ground-burst column (white01.bmp), one-shot.

**Steal Coin — case `0x112` (effectId 274).**
- **Sound:** `effect\rog_steal_coin.wav` — once on frame 0.
- **Sub-emitter:** loops `FUN_005ff4f0(effect\coin_a.bmp)` **30 times** (0x1e).
  - `FUN_005ff4f0` is the same ground-particle builder as `FUN_005ff300` but texture is caller-supplied (`coin_a.bmp`), a gentler upward velocity `+0xf70c = −18 − rand%30`, lowered by `_DAT_0069f70c`, and blend flag `+0xf774 = 1` (alpha-blend, not additive). Type 0x2b, ground-marked.
- **Anchor:** caster feet/ground.
- **Blend/colour:** alpha-blended coin sprites (`coin_a.bmp`) fountaining up from the caster — the coin-steal shower. (Also has a `.str` counterpart `steal_coin.str`, dispatcher case 0x10c.)

**Back Stab — case `0x113` (effectId 275).**
- **Sound:** `effect\rog_back_stap.wav` — fired at frame `0x14` (20), not frame 0.
- **Sub-emitter:** loops `FUN_00600f60()` **20 times** (0x14).
  - `FUN_00600f60`: `FUN_005c3670` **type 0x2d**, ground-marked (`+0x17c=5`), texture `effect\thunder_center.bmp`. Anchored to caster (`+0x34c..0x354` and `+4/8/c` ← target node). Size `+0x29c = 0`. Per particle: vertical velocity `+0xf70c = rand%10 − 40` (upward burst), horizontal spread `+0xf770` = `(rand&0xf)`×`_DAT_0069f178` + base, random rotation/scale via `_DAT_00695778` scalers, blend fields `+0xf774=0,+0xf775=0,+0xf776=2`. Spawns around caster feet with a small `−_DAT_0069f82c` y-drop.
- **Anchor:** caster/ground.
- **Blend/colour:** a sharp "thunder_center" white-flash impact burst at the victim's back — the stab flash. One-shot.

---

### Grimtooth / Venom Dust
There is **no dedicated Grimtooth builder function** in the exe. The Grimtooth/Venom-Dust visual is a pure `.str` effect resolved in the `.str` dispatcher (`FUN_005c2c8e` region): **case `0x7c` → `effect\VenomDust.str`** (`s_VenomDust_str_006dbc18`). That routine just hands the filename to the generic `.str` player, so:
- **Geometry:** `.str` layered-billboard animation (multi-layer timeline baked into VenomDust.str), rendered as a screen-facing quad sequence — the spreading green poison-dust cloud carpet on the ground cells.
- **Anchor:** ground / cast cell (standard `.str` placement).
- **Sound:** none emitted by the dispatcher case (VenomDust.str carries no coded .wav here; Grimtooth's poison motes reuse the same green venom particle family as Envenom/Poison-Attack via `&DAT_006dc950/938` when spawned by the poison updaters).
- **Blend/colour:** additive green poison dust. (Related poison `.str` in the same table: `PoisonReact.str` 0x7f, `PoisonReact_1st.str` 0x7e, `VenomSplasher.str` 0x81, `enc_fire/ice/wind/earth.str` 0xff–0x102 for enchant elements, `steal_coin.str` 0x10c, `strip_weapon/shield/armor/helm.str` 0x10d–0x110.)

---

**Key shared primitives observed:** `FUN_005c3670(type,…)` particle-emitter factory (types seen: 4=impact/ground quad, 5=soft green mote, 6=sparkle, 0xd=red slash mote, 0x2b/0x2d=ground streak arrays); `FUN_005c3870(0,0xc2a00000,0)` = torso anchor at −80.0f returning a bool lifetime; `FUN_0040c590(name,0)` = texture load; `FUN_005481c0`/`FUN_00548230` = set the emitter's two-sprite texture pair; `FUN_004396b0` = 3D positional sound; `+0x17c=5` = ground-quad flag; `FUN_00412cb0` = colour-matrix tint (used by Stone Curse to grey the target). Source: `/root/BornRok/winEXE/decomp/uaRO_decomp.c`.
