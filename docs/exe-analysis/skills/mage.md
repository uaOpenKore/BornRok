# Mage — offensive

> Per-skill effect dossiers reversed from `winEXE/uaRO.exe`. See [`README.md`](README.md) for the shared conventions and field meanings.

## Mage offensive — skill effect dossiers

All facts anchored to `/root/BornRok/winEXE/decomp/uaRO_decomp.c`. Effect objects share one global scratch pointer `DAT_007710a8` (the just-spawned emitter from `FUN_005c3670`). Recurring field map inferred across these fns: `+0x198`=lifespan in frames; `+0x17c`=geometry/render flags (`=5` → flat ground quad; `|0x200` → billboard-planar); `+0x190`(=400)=blend/flags word; `+0x19c`=render/blend mode; `+0x358/0x35c/0x360`=spawn offset/velocity XYZ; `+0x290/0x294`=orientation angles; `+0x29c`=scale; `+0x2a8`=radius/spread; `+0x240/0x244`=angular velocity; `+0x2d8/0x2e4`=size start/end; `+0xfc`=texture-handle array, `+0x1b8`=frame count. `FUN_005481c0`/`FUN_00548230` push two colour/param blobs (`&DAT_006dc…`) then `FUN_006113a0` commits/registers the emitter. Positional sound is always emitted relative to camera focus `(pos − cam.at)` at vol `0xfa`, pan `0x28`, pitch `1.0`.

Effect-ID → function map (from the master dispatch `switchD_005bd4cf`, lines ~364360–365115, and `switchD_005ba954` ~363740):

| EF id | fn | skill |
|---|---|---|
| 0x0f | FUN_005c7920 | Soul Strike |
| 0x18 | FUN_005cab90 | Fire Ball (missile flight) |
| 0x1a | FUN_005cae20 | Cold Bolt |
| 0x1c | FUN_005cb720 | Frost Diver impact |
| 0x20 | FUN_005cc300 | Napalm Beat |
| 0x2e | FUN_005da5b0 | Fire Wall |
| 0x50 | FUN_005d05d0 | Fire Ball (2nd/monster variant) |
| 0x5d | FUN_005d0f50 | Lightning Bolt |
| 0x7b | FUN_005d3cf0 | Frost Diver cast/travel |
| 0x83 | FUN_005cba90 | Thunderstorm |
| — | FUN_005cba20 | Thunderstorm (mage sound variant, called separately) |
| 0x76 | FUN_005d3460 | (adjacent) Wizard fire_ivy |

---

### Fire Ball — `FUN_005cab90` @0x5cab90 (EF 0x18) and `FUN_005d05d0` @0x5d05d0 (EF 0x50)

Two near-identical implementations; `05cab90` keys on animation-state `*(this+0x110)` ∈ {0x14,0x18,0x1c,0x20}, `05d05d0` keys on `*(this+0x110)`≡`param_1[0x44]` ∈ {0,4,8,0xc} (the four cardinal facings).

- **Sound**: `effect\EF_FireBall.wav` (`s_effect_EF_FireBall_wav_006dc2e0`) at `0x5cab90:371064` / `0x5d05d0:373482`, fired once on the keyed frame (spawn of the fireball body).
- **Geometry**: single billboard particle from `FUN_005c3670(5,0,0,0)` (`0x5cab94`/`0x5d05e2`). No `+0x17c=5`, so camera-facing billboard (not ground quad). Blend/flags `+0x190 |= 0x120`; render mode `+0x19c=1`.
- **Textures**: no explicit `FUN_0040c590` in `05cab90` (texture comes from the two colour/param blobs `&DAT_006dc2cc` + `&DAT_006dc2b8`, `0x5cabf?`). `05d05d0` binds via the object's own vtable `(*this+0x1c)(FUN_00544880(...),1,1)` then blobs `&DAT_006dc5c0`+`&DAT_006dc5b0`.
- **Motion/anchor**: launch angle taken from `FUN_00544da0/FUN_00544d20(this+0x13c, this+0x144)` = vector from caster→target; `+0x290 = −pitch`, `+0x38` adjusted into camera space; spin `+0x240 = −len/lifespan`. Anchored on the projectile, travelling caster→target.
- **Lifespan**: `+0x198` = 0x14 (20 frames) in `05cab90`, 0x0f (15) in `05d05d0`.
- **Per-facing scale**: `05cab90` sets scale `+0x29c=0x3fa66666`(1.3), then for non-0x14 states sets `+0x1f0=0xf6, +0x1f4=199, +0x1f8=0x4c` (an RGB-ish tint 246/199/76 → orange) and radius `+0x2a8`: 0x43340000(180) / 0x43020000(130) / 0x42a00000(80) by facing. `05d05d0` scale `+0x29c=0x3f800000`(1.0), same `+0x2a8` radius ladder.
- **Blend/colour**: additive orange fireball (tint 246/199/76).

---

### Soul Strike — `FUN_005c7920` @0x5c7920 (EF 0x0f)

Fires a rising soul once every 11 frames, up to `local_8` souls where soul-count is set by cast level via `*(this+0x10c)` (case 1→1 soul, 2→2, 3→3, 4→4, 5→5; each case also sets an X-offset pair iVar4/iVar5, e.g. lvl2 = −90..+180). Gate: `(*(this+0x110)+5) % 0xb == 0 && idx ≤ count` (`0x5c79b8`).

- **Sound**: `effect\EF_SoulStrike.wav` (`s_effect_EF_SoulStrike_wav_006dc0a8`) @371049, once per soul spawned.
- **Geometry**: one billboard per soul from `FUN_005c3670(8,0,0,0)` @0x5c79f?; lifespan `+0x198=0x28` (40). Extra motion params: `+0x270=0x40600000`(3.5) & derived `+0x274`, `+0x2b8=0xc0000000`(−2.0) & `+0x2c4` — a curved/accelerating vertical path. Horizontal placement `+0x298 = idx*iVar5 + iVar4` (fans souls across level-scaled offsets). Absolute spawn locked to target cell: `+0x364/0x368/0x36c = this.pos + this(0x130..0x138)` copied into `+0x214/0x218/0x21c`.
- **Textures**: chosen by low byte of `param_2`: if 0 → blobs `&DAT_006dbf2c`+`&DAT_006dbf14`; else → `&DAT_006dc090`+`&DAT_006dc078` (two visual variants — likely player vs monster).
- **Anchor**: target cell (souls converge onto target). Scale `+0x29c=0x40300000`(2.75); `+0x1a8=0xc` (a sub-mode/frame count).
- **Blend/colour**: additive purple soul missiles (the DAT blobs carry colour). One-shot per soul, staggered every 11 frames.

---

### Napalm Beat — `FUN_005cc300` @0x5cc300 (EF 0x20)

Copies caster colour matrix (12 words from `*(gCtx+0xc8)+0x94`) and computes a projected screen point via `FUN_00412cb0` stored to `this+0x130/0x134`. Emits scattered violet motes while `*(this+0x110) < 0xf` (15 frames).

- **Sound**: `effect\EF_NapalmBeat.wav` (`s_effect_EF_NapalmBeat_wav_006db4cc`) @366246, fired only on frame 0.
- **Geometry**: particle from `FUN_005c3670(0,0,0,0)` (primitive type 0) @0x5cc3?; lifespan `+0x198=0x1e` (30). Randomised radial scatter: angle `local_c=rand()%0x168`, radius `local_8=rand()%0x32+10`, velocity `+0x358 = cos·r`, `+0x35c = −sin·r − gravity(_DAT_0069f174)`. Size `+0x2e4=+0x2d8 = rand()%0x28+0x14` (20–60). Fade start `+0x1a4 = life − life/3`. Render mode `+0x19c=2`.
- **Textures**: 8-frame animated set — `+0x1b8=8`, allocates handle array, loop `FUN_006713a8(buf,&DAT_006dc45c, i)` + `FUN_0040c590` for i=1..8 (a numbered sprite series, base template `DAT_006dc45c`).
- **Anchor**: caster/target ground point (the projected screen coord); motes burst outward with gravity.
- **Blend/colour**: additive violet; `+0x208=0` (no depth write). One-shot burst.

---

### Frost Diver — cast/travel `FUN_005d3cf0` @0x5d3cf0 (EF 0x7b) & impact `FUN_005cb720` @0x5cb720 (EF 0x1c)

**Cast/travel `FUN_005d3cf0`**: on frame 0 plays sound and computes the caster→target direction (`+0x148..0x150`) used to march the projectile (`+0x154..0x15c` decremented each tick). While `*(this+0x108)==0` (in flight), every 3rd frame emits an ice shard; arrival when distance ≤ `_DAT_0069c090` sets `+0x108=1`.
- **Sound**: `effect\EF_FrostDiver.wav` (`s_effect_EF_FrostDiver_wav_006dc91c`) @374885, once on frame 0.
- **Geometry**: shard from `FUN_005c3670(9,0,0,0)`; lifespan `+0x198=0x28`(40); flags `+0x190 |= 1`; upward velocity `+0x35c=0x41a00000`(20) plus the march vector; random spin `+0x290=rand%0x168`, `+0x294=rand%0x1e+0x4b`; scale `+0x29c=(rand%0x28+0x3c)·k` (~60–100 scaled); `+0x2e4=0x41200000`(10). Spin rate `+0x240=0x40400000`(3.0). Frame-strip `+0x188=6`. Fade `+0x1a4=life−10`.
- **Texture**: single `effect\stone.bmp` (`s_effect_stone_bmp_006dc584`) @373174 into slot 0.

**Impact `FUN_005cb720`**: on frame 0 only, plays the freeze SFX then bursts **8 ice crystals** in a loop (`local_c=8`).
- **Sound**: `effect\EF_FrostDiver2.wav` (`s_effect_EF_FrostDiver2_wav_006dc338`) @371366, once.
- **Geometry**: per-crystal `FUN_005c3670(9,0,0,0)`, lifespan `+0x198=0x28`; flags `+0x190 |=1`; radial velocity `+0x358=cosθ·f`, `+0x360=−sinθ·f` (θ=rand%0x168, f = (rand&3+1)·k), strong upward `+0x35c=0x41f00000`(30); scale `+0x29c=(rand%0xfa+100)·k`; size `+0x2e4=(rand%100+200)·k` (big); spin `+0x290=rand%0x168`, `+0x294=rand%0x14+0x50`; spin rate `+0x240=0x40400000`(3.0); frame-strip `+0x188=0x14`(20); radius `+0x2a8=0x43480000`(200); fade `+0x1a4=life−10`.
- **Texture**: single `effect\ice.tga` (`s_effect_ice_tga_006dc328`) @371? into slot 0, per crystal.
- **Anchor**: target cell (the frozen victim). One-shot 8-crystal shatter, additive/alpha ice-blue.

---

### Thunderstorm — `FUN_005cba90` @0x5cba90 (EF 0x83) + mage-sound variant `FUN_005cba20` @0x5cba20

`FUN_005cba90`: snaps effect position to owner (`this+0x4/8/0xc = *(this+0xf4)+...`), then on frame 0:
- **Sound**: `effect\EF_ThunderStorm.wav` (`s_effect_EF_ThunderStorm_wav_006dc37c`) @371462, once.
- **Geometry**: `FUN_005c3670(5,0,0,0)`; lifespan `+0x198 = *(this+0x114)` (server-supplied duration). Both `+0x17c |= 0x200` and `+0x190 |= 0x200` → planar/ground-aligned billboard column. Drop from above: `+0x35c=0xc0900000`(−4.5). Render mode `+0x19c=2` (additive). Scale `+0x29c=0x40200000`(2.5). Visual params via blobs `&DAT_006dc368`+`&DAT_006dc354`.
- **Anchor**: ground cell (targeted AoE); the storm column falls repeatedly for the server duration.

`FUN_005cba20` (mage-cast audio + anchor): frame-0 plays `effect\magician_thunderstorm.wav` (`s_effect_magician_thunderstorm_wav_006dac20`) @0x5cba? then returns `FUN_005c3870(0, 0xc2a00000, 0)` — the torso/body billboard-anchor helper (the −80px screen anchor family), `0xc2a00000` = −80.0f. No textures of its own; it's the caster-side cast cue paired with the AoE.

---

### Fire Wall — `FUN_005da5b0` @0x5da5b0 (EF 0x2e)

Called from case 0x2e with texture `effect\ring_black.tga` (`s_effect_ring_black_tga_006da738`) as the wall-panel base. Frame-0 only.
- **Sound**: `effect\EF_FireWall.wav` (`s_effect_EF_FireWall_wav_006d6684`) @378268, once.
- **Geometry**: `FUN_005c3670(0x76,0,0,0)`; **flat ground quad** `+0x17c=5`; lifespan `+0x198=*(this+0x114)` (server duration); scale `+0x29c=0x41500000`(13.0). Position snapped to owner cell.
- **Texture**: single slot 0 = the passed `ring_black.tga` (loaded via `FUN_0040c590(param_2,0)`).
- **Layered fire sub-emitters**: turns on the extended emitter block (`+0xf704=1`) with **four rising-flame sub-streams** configured at `+0xf706`, `+0xf7be`, `+0xf876`, `+0xf92c`, each seeded with child effect-id `0x13b`(315) (last one `0x168`=360), a spawn angle (`0x13b`,`0x13b`,`0xb4`,`0`... i.e. 0/90/180/315°), height (`0x41c80000`=25, `0x41b00000`=22, `0x41980000`=19, `0x41f00000`=30) and per-stream rate — building the multi-tongue fire wall along the cell row.
- **Anchor**: ground cell(s). Blend additive fire. Duration server-driven.

---

### Cold Bolt — `FUN_005cae20` @0x5cae20 (EF 0x1a)

Snaps to owner, then two phases keyed on `*(this+0x110)`:
- **Frame 0xc (12)**: plays a **randomised** ice-arrow SFX — `FUN_006713a8(buf, "effect\EF_IceArrow_d.wav" template, rand%3+1)` → `EF_IceArrow_d1/2/3.wav` (`s_effect_EF_IceArrow_d_wav_006dc30c`) @371509; also multiplies bolt count `*(this+0x10c) *= 10`.
- **Falling shards (frame>0xb, every 10th frame while idx<count)**: `FUN_005c3670(0x10,0,0,0)` (type 0x10 = falling missile); lifespan `+0x198=0x46`(70); flags `+0x190=0x14`. Spawns high above (`+0x35c=0xc2700000`=−60 downward, `+0x358=rand%10+25`, `+0x360=rand%10+15`) and computes descent pitch/yaw via `FUN_00544e00` → `+0x290`,`+0x294`. Spin `+0x240=0xc0300000`(−2.75); size `+0x2d8=0x41380000`(11.5), `+0x2e4=0x40733333`(3.8); fade `+0x1a4=life−0x14`.
  - **Texture**: single `effect\icearrow.tga` (`s_effect_icearrow_tga_006dc2f8`) @371196, slot 0.
- **Impact ring (`*(this+0x108)!=0`)**: `FUN_005c3670(0xc,0,0,0)`; lifespan `+0x198=0x1e`(30); expanding ring `+0x294=0x42b40000`(90), growth `+0x2d0=0x3f99999a`(1.2)→derived `+0x2d4`, `+0x278=0x41200000`(10); `+0x208`/depth off; fade `+0x1a4=life−10`.
  - **Texture**: `effect\ring_blue.tga` (`s_effect_ring_blue_tga_006da2a4`) @370462.
- **Anchor**: shards fall onto target cell; blue ring flashes on hit. Blend additive ice-blue.

---

### Lightning Bolt — `FUN_005d0f50` @0x5d0f50 (EF 0x5d)

`param_2` selects normal (0) vs an alt "twirl" skin (nonzero). Frame-0 only; two stacked emitters.
- **Sound**: `effect\hunter_shockwavetrap.wav` (`s_effect_hunter_shockwavetrap_wav_006dc690`) @0x5d0f6? once (note: uaRO reuses this wav string for the bolt SFX).
- **Emitter A (glow/core)**: `FUN_005c3670(0xf,0,0,0)`; lifespan `+0x198=0x14`(20); flags `+0x190 |=0x40 |0x400`, `+0x17c |=2` (planar); size `+0x2d8=+0x2e4=0x40600000`(3.5); orientation from caster→target (`FUN_00544da0/00544d20`), drop `+0x35c=0xc0a00000`(−5.0), radius `+0x2a8=0x432a0000`(170). Texture slot 0 = `effect\thunder_center.bmp` (`s_effect_thunder_center_bmp_006da1dc`) for normal, or `effect\twirl_soft.bmp` (`s_effect_twirl_soft_bmp_006dc678`) for the variant.
- **Emitter B (bolt animation)**: second `FUN_005c3670(0xf,0,0,0)`; lifespan `+0x198=0x14`; `+0x190=0x40|0x400`, `+0x17c|=2`, `+0x208=1`; size `+0x2d8=+0x2e4=0x40900000`(4.5); render mode `+0x19c=1`; **6-frame strip** `+0x1b8=6`. Normal skin loads `thunder_ball_a..f.bmp` into slots 0–5 (`s_effect_thunder_ball_a_bmp_006dc65c` … `_f_bmp_006dc5d0`, @373773–373796); the variant substitutes `twirl_soft.bmp` for frames a/c/e keeping `thunder_ball_b/d/f` for the rest.
- **Anchor**: target cell; camera-facing planar lightning column. Additive, one-shot 6-frame flicker.

---

### Fire-hit splash / generic hit dispatcher — `FUN_0060f940` @0x60f940

This is the CEffectHit multiplexer: `param_2` (0x00–0x1f) selects the hit style; each case pushes a colour/param blob pair (`&DAT_006dd…`) and commits via `FUN_006113a0`, with an orientation code passed through `(*this+0x1c)(dir,0x10,1)`. Position snapped to owner (`this+0x4/8/0xc`). Frame gate: fires when `*(this+0x110)==0` (or `==0x1e` if `param_2==0x1e`). All spawn `FUN_005c3670(5,...)` with `+0x66(=0x198*? )=*(this+0x114)` lifespan, `+0x5f|0x200` & `+100|0x200` (planar billboard), sub-mode `+0x67`.

Cases relevant to Mage/fire:
- **case 7 (`ef_firehit`)** @408556: plays `effect\ef_firehit.wav` (`s_effect_ef_firehit_wav_006dd928`), sets sub-mode `+0x67=3`, clears planar flag `+100=0`, radius `+0xaa=0x435c0000`(220); blobs `&DAT_006dd914`+`&DAT_006dd900`. This is the fire-element hit splash used by Fire Bolt/Fire Ball/Fire Wall impacts.
- **case 1 (`h_blood_lust`)** plays `effect\h_blood_lust.wav`; case 0 and 2–6, 8–0x1f are other element/skill hit skins (each just a blob pair; several — 8, 0x1c — compute an incoming-angle `sVar7` from `atan2(target−caster)` to pick a directional hit sprite via `(*this+0x1c)(code,0x10,1)`).
- **Geometry**: single planar billboard hit flash, camera-aligned, at the struck actor; blob pair carries the tint/frames; one-shot, lifespan = `*(this+0x114)` (server-supplied). Sub-mode `+0x67` (2/3/4/6) selects animation timing/style.

---

Adjacent for completeness — **Wizard fire (`fire_ivy`) `FUN_005d3460` @0x5d3460 (EF 0x76)**: frame-0 `effect\wizard_fire_ivy.wav` (`s_effect_wizard_fire_ivy_wav_006dc898`); `FUN_005c3670(0xf,...)`, `+0x17c|=2` planar, `+0x190=0x40|0x100`, drop `+0x35c=0xc0a00000`, sizes `+0x2d8/2e4=0x40600000`(3.5); single texture `effect\fire_ivy.bmp` (`s_effect_fire_ivy_bmp_006db4e8`); lifespan server-driven `*(this+0x114)`.

Note on the two Fire Ball fns: `05cab90` and `05d05d0` are the same visual with different state-encoding (player skill EF 0x18 vs the EF 0x50 path) — same wav, same `type 5` billboard, same per-facing radius ladder; only the lifespan (20 vs 15) and base scale (1.3 vs 1.0) differ.
