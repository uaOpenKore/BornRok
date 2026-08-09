# Hunter / Blacksmith / Sage / Monk

> Per-skill effect dossiers reversed from `winEXE/uaRO.exe`. See [`README.md`](README.md) for the shared conventions and field meanings.

## Hunter/Blacksmith/Sage/Monk — skill effect dossiers

Convention notes (shared): `param_1+0x110` = animation tick/timeline cursor (phase gate). `param_1+0x114` = effect lifespan. `param_1+0xf4` = owner actor (position copied into effect each frame). `FUN_004396b0(name, dx,0,dz, 0xfa,0x28,0x3f800000)` = positional 3D sound at owner-minus-camera offset, params (250, 40, 1.0) = max-dist/min-dist/volume. Particle factory `FUN_005c3670(type,x,y,z)`: type selects a preset emitter class; common fields: `+0x198`=particle lifespan (frames), `+0x17c`=render/billboard flag bits, `+0xfc`=texture-pointer array (allocated `+0x1b8`<<2), `+0x1b8`=texture count, `+0x290`=rotation/yaw, `+0x35c`=Z/height, `+0x1a4`=fade-start frame, `+0x2d8/+0x2e4`=scale. `FUN_005c3870(0,-80f,0)` = torso-anchored one-shot billboard (the -80px screen/torso anchor from CEffect). `FUN_0040c590(name,0)` = load texture.

---

### Blitz Beat — `FUN_005d2350` (Hunter/Falcon)
- **Sound:** `effect\hunter_blitzbeat_1st.wav` at tick 5; `effect\hunter_blitzbeat.wav` at tick 0x1e(30). Both positional at owner.
- **Anchor:** Owner/caster — every frame copies owner pos from `+0xf4` into effect origin (follows the falcon/hunter).
- **Phase 1 (tick 0):** Loop 10× spawn particle type `0x10` (falling debris/impact motes). Flags `+0x17c |= 0x200` (billboard bit), lifespan `+0x198`=0x14 (20). Randomised velocity via `_rand` + trig (`FUN_0040ae60/aea0` sin/cos), gravity `+0x35c`=-7.0 (0xc0e00000), Z-launch `+0x358`. Scale ramp `+0x2d8`=rand(20..60)·k. Texture: **`effect\ac_center2.tga`** (single). Fade-start = life−2.
- **Phase 2 (tick ≥ 0x24 every 3rd frame, capped by `+0x10c` hit-count via counter `DAT_007710b0`):** spawn particle type `0xd` (ground splash), lifespan 0xc(12), yaw from atan of velocity, random horizontal scatter. Texture: **`effect\magic_green.tga`**. Alpha `+0x240`=0.4 fading. This is the per-hit green ground burst, one per Blitz hit.
- **Blend:** additive billboards; one-shot per invocation, re-triggered by server per hit.

### Shockwave Trap — `FUN_005d0f50(param_1, param_2)` (Hunter)
- **Sound:** `effect\hunter_shockwavetrap.wav` at tick 0.
- **Anchor:** Ground at trap cell (uses `param_1+0x13c/+0x144` = trap world XY to compute a ground-aligned tilt/pitch).
- **Two ground emitters, type `0xf`:**
  1. Swirl disc, life 0x14(20). Flags `+400 |= 0x40|0x400` and `+0x17c |= 2` (ground-quad/flat orientation). Scale `+0x2d8/+0x2e4`=3.5 (0x40600000). Pitched to lie flat (`FUN_00544da0/d20` derive ground normal from trap XY). Texture: **`effect\thunder_center.bmp`** if `param_2==0`, else **`effect\twirl_soft.bmp`** (soft variant).
  2. Ball/spark ring, life 0x14. `+0x208`=1 (multi-frame anim), scale 4.5 (0x40900000), 6-texture animation array (`+0x1b8`=6). Normal set: **`thunder_ball_a…f.bmp`** (a,b,c,d,e,f). Soft set (`param_2!=0`): **`twirl_soft, thunder_ball_b, twirl_soft, thunder_ball_d, twirl_soft, thunder_ball_f`**.
- **Blend:** additive, flat-on-ground; one-shot.

### Flasher — `FUN_005d1cb0` (Hunter)
- **Sound:** `effect\hunter_flasher.wav` at tick 0. Positional.
- **Anchor:** Ground; projects owner→screen via `FUN_00412cb0` (stores screen X/Y into `+0x130/+0x134`) — used to place a screen-flash relative to the trap.
- **Emitter A (type `0xf`):** central flash, life 0x46(70). `+400`=0x41 (flags), `+0x17c|=2` (flat), gravity `+0x35c`=-10, scale 20 (0x41a00000). Keyframe scale/colour curves pushed via `FUN_00427180` on channels `+0x2fc/+0x30c/+0x31c` (multi-keyframe scale animation: values 40→120→200 then 100→0→80). Fade-start=life−10. Texture: **`effect\thunder_center.bmp`**.
- **Emitter B (loop 0x14=20×, type `3`):** radial spark streaks. Life 0x46, gravity −40 (0xc2200000), random yaw `%0x168`(360°), random speed rand(10..70)·k with decel, random spin, random scale rand(20..60). Texture: **`EFFECT\alpha_center.tga`**. Fade at 2/3 life.
- **Blend:** additive; one-shot flash + spark burst.

### Remove Trap — `FUN_005d2160` (Hunter)
- **Sound:** `effect\hunter_removetrap.wav` at tick 0.
- **Anchor:** Ground at trap.
- **Emitter (loop 12×, type `7`):** rising sparkle/dust, triggered at tick 0 **and** tick 7. Life 0x32(50), drift up `+0x270`=-2.0, random rise speed, random yaw `%360`, gravity/scale small. Position offset from ground normal (`+0x358..0x360`). Fade-start 0x23. Uses `FUN_005481c0(&DAT_006dbf2c)`/`FUN_00548230(&DAT_006dbf14)` + `FUN_006113a0` (assigns a shared texture/material handle rather than a named tga). Rising dissipation puff as the trap is picked up.
- **Blend:** additive; one-shot ×2 sub-bursts.

### Trap-set effects (Blastmine / Claymore / Freezing / Spring) — `FUN_005d9040` / `…90b0` / `…9120` / `…9190`
All four are **identical in structure** — a sound cue plus a single torso/ground one-shot billboard:
- **`FUN_005d9040` Blastmine:** `effect\hunter_blastmine.wav` at tick 0x14(20).
- **`FUN_005d90b0` Claymore:** `effect\hunter_claymoretrap.wav` at tick 0.
- **`FUN_005d9120` Freezing:** `effect\hunter_freezingtrap.wav` at tick 0.
- **`FUN_005d9190` Spring:** `effect\hunter_springtrap.wav` at tick 0.
- **Geometry/anchor (all):** single `FUN_005c3870(0, -80f, 0)` — torso-anchored (−80px) one-shot billboard, no procedural particles, no named texture in this fn (the sprite is the trap-place stock effect). Returns when the billboard finishes.

### Skid Trap — `FUN_005d7df0` (Hunter)
- **Sound:** `effect\hunter_skidtrap.wav` at tick 0.
- **Geometry/anchor:** same skeleton as the other traps — single `FUN_005c3870(0, -80f, 0)` torso billboard one-shot. No particles.

### Detecting — `FUN_005d35e0` (Hunter)
- **Sound:** `effect\hunter_detecting.wav` at tick 0.
- **Anchor:** Ground (flat expanding ring at caster cell).
- **Emitter (type `0xf`):** life 0x39(57). Scale `+0x2dc/+0x2e8`=1.5 expanding (rate via `_DAT_0069facc`). Gravity `+0x35c` set −7 then overwritten −5 (0xc0a00000). Tilt `+0x338`=-0.5, radius `+0x298`=90 (0x42b40000). Anim-frame count `+0x1b4`=0x11(17). Fade-start `+0x1a4`=0x28(40). Texture: **`effect\fashasha.tga`** — the expanding detect-ripple ring, flat on ground.
- **Blend:** additive; one-shot expanding disc.

### Maximize Power — `FUN_005d8ee0` (Blacksmith)
- **Sound only + torso billboard.** Multiple timed cues:
  - `effect\black_maximize_power_circle.wav` (`…circ`) at tick 0.
  - `effect\black_maximize_power_sword.wav` (`…swor` @6dcaa8) at tick 0x28(40) and tick 0x2f(47).
  - `effect\black_maximize_power_sword` (@6dca7c variant) at tick 0x41(65).
- **Geometry/anchor:** single `FUN_005c3870(0,-80f,0)` torso one-shot billboard. No procedural particles.

### Adrenaline Rush — `FUN_005d1860` (Blacksmith)
- **Sound:** `effect\black_adrenalinerush_a.wav` at tick 0; `effect\black_adrenalinerush_b.wav` at tick 0x1e(30). Sets lifespan `+0x114`=300.
- **Anchor:** Caster; `FUN_00412cb0` projects owner→screen (`+0x130/+0x134`).
- **Emitter A (loop 0x14=20×, type `3`):** upward spark spray. Life 0x50(80), gravity −40, random yaw `%360`, random speed rand(10..70) decelerating, random spin, upward bias `+0x2e8`, fade over life. Texture: **`EFFECT\alpha_center.tga`**.
- **Emitter B (loop over yaw 0→360 step 0x5a=90°, i.e. 4 arms, type `7`):** ring of 4 rising jets. Life 100, drift up `+0x270`=-0.1, speed grows with tick, yaw = arm angle, fade at life−life/10. Uses shared material via `FUN_005481c0/548230/6113a0` (no named tga).
- **Blend:** additive; one-shot; long-lived (300).

### Weapon Perfection — `FUN_005d8e70` (Blacksmith)
- **Sound:** `effect\black_weapon_perfection.wav` at tick 0.
- **Geometry/anchor:** single `FUN_005c3870(0,-80f,0)` torso one-shot billboard. No particles.

### Weapon Repair — `FUN_005d8d20` (Blacksmith)
- **Sound:** `effect\black_weapon_repair_a.wav` at tick 0x14(20) **and** tick 0x37(55) (same wav, two hammer strikes).
- **Geometry/anchor:** single `FUN_005c3870(0,-80f,0)` torso one-shot billboard. No particles.

### Over Thrust — `FUN_005d4850` (Blacksmith)
- **Sounds:** `effect\black_overthrust.wav` at tick 0; **also** `effect\EF_StoneCurse.wav` at tick 0 (reused as the second layer). Sets lifespan `+0x114`=9999 (persists).
- **Anchor:** Caster; owner→screen projection stored `+0x130/+0x134`.
- **Emitter (type `4`, tick 0):** single ground/aura element. Life 0x28(40), gravity `+0x35c`=-20 (0xc1a00000), initial vel `+0x20c`=10, spin `+0x278`=10, scale-rate `+0x2d0`=7 fading over (life+40). Fade-start `+0x1a4`=0. Texture: **`effect\alpha_center.tga`**.
- **Blend:** additive; one-shot but effect object lingers (9999) as the buff aura.

### Refine Success — `FUN_005d9730` (Blacksmith, forge result)
- **Sound:** `effect\bs_refinesuccess.wav` at tick 0xf(15).
- **Geometry/anchor:** single `FUN_005c3870(0,-80f,0)` torso one-shot billboard.

### Refine Failed — `FUN_005d97a0` (Blacksmith, forge result)
- **Sound:** `effect\bs_refinefailed.wav` at tick 5.
- **Geometry/anchor:** single `FUN_005c3870(0,-80f,0)` torso one-shot billboard.

### Magic Rod — `FUN_0060f330` (Sage)
- **Sound:** `effect\sage_magic_rod.wav` at tick 0.
- **Geometry:** none beyond sound. At tick 0 it only re-syncs effect origin to owner (`+0xf4`→`+4/+8/+0xc`) and plays the wav. No particles/textures — the visual is the stock cast/absorb sprite driven elsewhere; this fn is purely the audio + position lock.

### Asura Strike — `FUN_005db010(param_1, param_2)` (Monk) — render `0x005db1b3`
- **Sound:** `effect\EF_BeginSpell.wav` at tick 0 (charge-up). Lifespan forced ≥ 0x15e(350). Also spawns two ground cast-rings via `FUN_005fb880(0x2d, effect\ring_white.tga, 2)` and `FUN_005fb880(0x19, …ring_white.tga, 2)` (large + smaller white cast circles).
- **Anchor:** Caster torso/body; origin locked to owner every frame.
- **Two shockwave sheets (type `0x44`), `param_2` = left/right (mirror) flag:**
  - **Tick 1:** first sheet. Flags `+0x17c`=1|5 (upright, additive). Life=`+0x114`. 3-texture set: `param_2==0` → **`effect\asura3.tga, asura2.tga, asura1.tga`**; else → **`asura13.tga, asura12.tga, asura11.tga`**. Then builds a strip of sub-elements (stride 0xb8, ~7 segments) at offsets scanning `local_c` from −0x28 step +0x14, height `local_14` from −6 step −0xc; per-segment scale 0x41900000(18) normal / 0x41c00000(24) mirrored; type field `+0xf778`=0 or 3.
  - **Tick 0x15(21):** second sheet, same type `0x44`. Textures `param_2==0` → **`asura4, asura5, asura6.tga`**; else → **`asura14, asura15, asura16.tga`**. Segment strip offsets `iVar4` from −0x28 step −0x14, `local_c` 8 step +0x10, `local_10` 6 step +0xc.
- **Geometry:** custom multi-segment billboard "sheets" (16-tile Asura wall), camera-billboard, upright. Rendered by dedicated `0x005db1b3` path. One-shot, ~350-frame lifespan.
- **Blend:** additive (flag bit 5).

### Soul Breaker (Soul Destroyer) — `FUN_005db750` (Monk/Assassin-cross render path)
- **Sound:** none in this fn (audio driven by caller).
- **Anchor:** Caster body; origin locked to owner.
- **Two lettered-glyph sheets (type `0x44`), one per phase:**
  - **Tick 1:** `+0x17c`=5 (upright additive), life=`+0x114`, 4-texture set: **`effect\soul_l.tga, soul_u.tga, soul_o.tga, soul_s.tga`** (spells "soul"/"s-o-u-l"). Segment strip (stride 0xb8): `local_8` from −0x3c step +0x14, `local_c` −4 step −6, per-segment scale 0x40a00000(5), type field `+0xf778`=2.
  - **Tick 0x15(21):** second set: **`soul_k.tga, soul_n.tga, soul_i.tga, soul_l.tga`**. Segment strip: `iVar3` from −0x78 step +0x14, `local_c` 0x16 step −6.
- **Geometry:** multi-segment upright billboard strip of glyph textures streaking toward target, camera-billboard. One-shot per phase.
- **Blend:** additive (flag 5).

---

**Cross-cutting observations**
- Traps-set, Maximize, Weapon Perfection/Repair, Skid, and both refine results are **audio + stock torso billboard only** (`FUN_005c3870(0,-80f,0)`) — no procedural particle geometry in-fn.
- Procedural particle skills (Blitz, Shockwave, Flasher, Remove Trap, Detecting, Adrenaline, Over Thrust) all use `FUN_005c3670(type,…)` with the named `.tga`/`.bmp` textures listed above; ground-flat ones set `+0x17c |= 2`, billboards `|= 0x200`.
- Asura and Soul Breaker are the only two here using the type-`0x44` multi-segment "sheet" emitter with per-tile strip tables and a `param_2` mirror flag, rendered by the dedicated `0x005db1b3`/`FUN_005db750` renderers.
