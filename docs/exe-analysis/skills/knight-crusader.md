# Swordman / Knight / Crusader

> Per-skill effect dossiers reversed from `winEXE/uaRO.exe`. See [`README.md`](README.md) for the shared conventions and field meanings.

## Swordman/Knight/Crusader — skill effect dossiers

Notation: emitter spawns use `FUN_005c3670(type,x,y,z)`; texture load `FUN_0040c590(name,0)` stored at `+0xfc`; sound `FUN_004396b0(wav, dx,dy,dz, 0xfa,0x28, 1.0)` (position is caster-minus-listener, i.e. localized at caster). Torso anchor `FUN_005c3870(0,-80.0,0)`. `FUN_006113a0()` = ground-orient/additive helper. Key emitter fields: `+0x198` lifespan(frames), `+0x1a4` fade-start, `+0x17c` render-flags (`|0x200` additive/ground, `=5` ground-quad), `+0x290/0x298` angle(rand%360), `+0x2b4/0x2d0` velocity/radius, `+0x2e4/0x2e8` size, `+0x35c` z/height (`0xc2200000`=-40px). `0xc2a00000`=-80px torso.

---

### Bash — `FUN_005c7c70` @0x5c7c70 (effectId 0x10) and `FUN_005cffa0` @0x5cffa0 (effectId 0x52)
- **Sound:** `effect\EF_Bash.wav` (`s_effect_EF_Bash_wav`@6db1e0), fired once at cast start (`+0x110`==0), localized on caster. In `cffa0` it re-fires at sub-phase `+0x110`==0x25.
- **Textures (in sequence):** `effect\alpha_down.tga` (single downward impact streak) → loop of `EFFECT\alpha_center.tga` spark motes.
- **Geometry / count:** 1× type-4 emitter (billboard impact flash, `+0x400 |1`) then a loop of type-3 spark emitters — **20 sparks** normally, **10** if `DAT_00772d2c` set (`c7c70`); **5–20** in `cffa0`. `cffa0` additionally, at `+0x110`==0x32, spawns 1× type-0xf ring using `effect\ring_liner.tga` (expanding ground ring, size 1.5, `+0x35c`=-7.0).
- **Anchor:** caster torso — colour/position seeded from actor matrix via `FUN_00412cb0` into `+0x130`; sparks emitted at local origin over the body. `cffa0` sparks bind to caster world pos (`+0x334/+0x338` from `+0xf4+4/+0xc`).
- **Sequence/timing:** one-shot. Impact flash lifespan 0x28 (40f) [`c7c70`] / 0x14 (20f) [`cffa0`], fade at life-10. Each spark: lifespan 0x28/0x14, random angle (rand%360 at `+0x290`), random outward speed (rand%60+10 at `+0x2b4`, radial vel derived at `+0x2c0`), random size (`+0x2e4`,`+0x2e8`), fade at life−life/3. `cffa0` cast-variant gives sparks upward bias (angle 0x15e+, speed 0x96+).
- **Blend/colour:** additive sparks; colour 0xff000000 base pulled from actor lighting matrix.

### Magnum Break — `FUN_005c8040` @0x5c8040 (effectId 0x11) and `FUN_005d9810` @0x5d9810 (effectId 0xaa)
- **Sound:** `effect\EF_MagnumBreak.wav` (@6dc0dc), once at start.
- **Textures:** `effect\ring_yellow.tga` + `effect\대폭발.tga` ("big explosion", DAT@6db30c). `c8040` = **3 emitters**: type-0xc ring_yellow (fast) → type-0xe 대폭발 explosion → type-0xc ring_yellow (2nd, life 0x1e). `d9810` = **2 emitters**: type-0xc ring_yellow + type-0xe 대폭발.
- **Geometry:** expanding flat ground shockwave rings (type 0xc/0xe, additive `+0x17c |0x200`, `FUN_006113a0` ground-orient), radius growth `+0x2d0` (1.75 / 0.29 fire) scaled by lifespan; explosion billboard 대폭발.
- **Anchor:** ground under caster (rings), caster torso for gate. `d9810` guards effectId 7/0x14; if id>299 returns 0, else calls `FUN_005c3870(0,-40.0,0)` (torso anchor at -40) returns 1.
- **Timing:** one-shot; lifespan taken from `+0x114` (server-passed duration) for `c8040` first/second ring, fixed 0x14/0x1e for `d9810`; fade at life−15.
- **Blend:** additive rings + explosive billboard, yellow.

### Provoke — `FUN_005d7d30` @0x5d7d30 (effectId 0x43)
- **Sound:** `effect\swordman\provoke.wav` (@6db320), fired at BOTH sub-phases `+0x110`==5 and ==0x1e (two shouts).
- **Textures/geometry:** none — no particles.
- **Anchor:** caster torso via `FUN_005c3870(0,-80.0,0)`.
- **Timing:** one-shot sound only; returns anchor result. **Sound-only skill.**

### Endure — `FUN_005ca2b0` @0x5ca2b0 (effectId 0xb)
- **Sound:** `effect\EF_Endure.wav` (@6dc260), once at `+0x110`==0.
- **Textures:** `effect\endure.tga` (aura ring) + `effect\alpha_down.tga` (rising motes).
- **Geometry / count:** at start, 1× type-0 aura emitter (`+0x400 |2`, size 150 `+0x2d8/+0x2e4`, shrink, life=`+0x114`). Then every tick while `+0x110`<0x32, 1× type-0 rising mote using alpha_down: random angle (rand%360), upward velocity (`+0x240`=-4.0 gravity-style), random radius (rand%100+60), sin/cos-placed on a circle around caster (`FUN_0040ae60/0040aea0` = sin/cos into `+0x358/+0x35c`).
- **Anchor:** caster — colour from actor matrix (`+0x130`); aura centered on torso, motes on surrounding ring, height `+0x35c` randomized minus `_DAT_0069f174`.
- **Timing:** persistent/looping buff — aura one-shot at cast, motes emitted continuously for duration (`+0x110`<50). Fade at life−life/3.
- **Blend:** blue-ish additive aura + drifting motes.

### Two-Hand Quicken — `FUN_005d9640` @0x5d9640
- **Sound:** `effect\knight\twohandquicken.wav` (@6da12c), once at `+0x110`==0. Also sets `+0x114`=9999 (effectively infinite duration marker).
- **Textures/geometry:** none.
- **Anchor:** torso `FUN_005c3870(0,-80.0,0)`.
- **Sound-only** self-buff.

### Counter Attack (Auto Counter) — `FUN_005d93a0` @0x5d93a0
- **Sound:** `effect\knight\autocounter.wav` (@6dacd8), once at start.
- **Textures/geometry:** none.
- **Anchor:** torso `FUN_005c3870(0,-80.0,0)`.
- **Sound-only** stance skill.

### Grand Cross — full chain `FUN_005feeb0` @0x5feeb0 (effectId 0xe2 white `param_3=0`; 0x1c2 black `param_3=1`)
- **Sound:** `effect\cru\grand_cross.wav` (@6dd204), once at start; caster localized.
- **Primary emitter:** 1× `FUN_005c3670(0x22,0,0,0)` — the cross-array carrier. Flags `+0x17c` cleared then `|1 |5` (ground-quad + additive). Lifespan from `+0x114`. Texture: `effect\ring_white.tga` (@6da2e8) for holy variant, `effect\ring_black.tga` (@6da738) for the demon/black variant; base scale `+0x29c`=1.0 (white) / 12.0 (black).
- **Cross layout:** a `do{…}while(iVar4<0x2e0)` loop initializes a **per-cross sub-element table** at `+0xf704` stride 0xb8 (5 crosses): each entry active-flag=1, angle base 0x5a(90°) at `+0xf706`, height `0x42f00000`=120.0 at `+0xf708`, radius `0x419f3333`=19.9 at `+0xf770`, plus `param_2`/`param_3` passed through. Then hand-tuned per-cross overrides at `+0xf710/…/+0xf99c`: angles {0xb4, 0x5a, 0, 0x10e}=180/90/0/270°, and RGB pairs {0x17,0x17}, {0x17,0xe9}, {0xe9,0xe9}, {0xe9,0x17} — i.e. 4 crosses radiating N/E/S/W with alternating white↔red(0xe9) tint.
- **Sub-emitters (per context — the 3 classic layers):** the type-0x22 carrier drives the known Grand Cross render chain — `FUN_005e20a0`(0x57, `cross_old.bmp`) the descending cross beams, `FUN_005ec140`(0x58, `explosive_1_128.bmp`) the ground explosion burst, `FUN_005e2300`(0x59, `alpha_center.tga`) the central flash — composited by the render path at **0x005ba4e2**.
- **Anchor:** ground at caster cell (`+0x4/+0x8/+0xc` copied from `+0xf4`); crosses billboard around that centre, camera-facing.
- **Timing:** one-shot volley, lifespan server-driven (`+0x114`); crosses staggered by their per-entry angle/height.
- **Blend:** additive; white (holy) or black+red (demon/undead-target) tint.

### Shield Boomerang — `FUN_005e1b60` @0x5e1b60 (effectId 3 `토마.bmp`, 0xf9 `shield_boomerang.bmp`) and `FUN_005e1e00` @0x5e1e00 (effectId 0x1d)
- **Sound:** `effect\cru\shield_boomerang.wav` (@6dd07c). `e1b60`: once at start. `e1e00`: fired at phases `+0x110` ∈ {0,3,6,9,0xc} (repeated per bounce).
- **Textures:** shield sprite passed as `param_2` — `effect\shield_boomerang.bmp` (@6da4b0) or `effect\토마.bmp` (@6da4cc, variant).
- **Geometry — `e1b60` (thrown shield):** 1× type-0x2a emitter, ground flag `+0x17c`=5. It's a **travelling projectile**: target world pos stored `+0x34c/+0x350/+0x354` from `+0x13c/+0x140/+0x144`; travel distance = `sqrt(dx²+dz²)` via `FUN_00408df0` into `+0xf708`; spin rate `+0xf770`=7.0; start pos = caster `+0x4/+0xc`, height `+0x790` = caster_y − `_0069f82c`; vertical arc term `+0xf720` interpolates target height. Single spinning shield flying caster→target.
- **Geometry — `e1e00` (scatter shards):** 1× type-0x74 emitter, flag=5; spin `+0xf770`=5.0; **random launch angle** (rand%360 at `+0xf710`) offset from caster by `&DAT_006ecbe4[angle]`/`&DAT_006ec09c[angle]` (sin/cos lookup tables) × range `+0xf71c`=2.5 — i.e. shields fanning out in a random direction each bounce.
- **Anchor:** caster origin → target (e1b60) / random radial (e1e00); z = ground.
- **Timing:** one-shot per throw; lifespan from `+0x114`. e1e00 re-triggers on bounce phases.
- **Blend:** opaque shield sprite (`+0x29c`=0, no extra scale).

### Kyrie Eleison / Guard shield wall — `FUN_00603b30` @0x603b30 (effectId 0x24 `guardK.tga param_3=0`; 0xb8 `param_3=2`)
- **Sound:** `effect\kyrie_guard.wav` (@6db484), once at start.
- **Textures (2, layered):** `param_2` (passed `effect\guardK.tga` @6da254) as slot 0, and `effect\guardK2.tga` (@6dd344) as slot 1 — emitter frame count `+0x1b8`=2 (two-texture blend). 
- **Geometry:** 1× type-0x3d emitter, ground/additive flag `+0x17c`=5, scale `+0x29c`=11.0, radius `+0xf708`=7.8, spin `+0xf770`=1.0 — a **shield/barrier disc** billboard hovering in front of/around the target. `param_3`==2 sets sub-mode `+0xf774`=2 (the 0xb8 recast variant).
- **Anchor:** target actor cell (`+0x4/+0x8/+0xc` from `+0xf4`); disc faces camera at torso height.
- **Timing:** one-shot spawn, lifespan from `+0x114` (duration of the ward).
- **Blend:** additive translucent shield, single active sub-element (`+0xf7bc/+0xf874/+0xf92c` cleared).

### Guard/particle update–render dispatcher — `FUN_006510b0` @0x6510b0
Not a Kyrie-specific spawner — this is the **per-particle-type update/animation state machine** (switch on `+0x160` particle subtype) that advances every emitter created above. Relevant swordman cases: it re-plays `effect\kyrie_guard.wav` at case 0x18 first frame; cases 0xe/0xf/0x10/0x15 implement the ballistic/arc motion (gravity `_0069fa28`, spin, 4-way directional sprite selection via jump tables `PTR_LAB_00651ea4/…ec4/…ee4/…ee4`) used by thrown-shield and cross elements. Handles fade (`+0x3c` alpha ramp) and lifespan expiry (returns 0 to free).

---

### Pierce / Spear / Brandish / Spiral / Bowling Bash — no dedicated client effect
A full scan of the effect string/texture pool (`s_effect_*` names and cp949 texture names) found **no** spear/pierce/brandish/bowling/spiral effect textures, sounds, or emitter functions. These physical spear skills have no client-side particle/`.str` layer — they render purely as the character's weapon attack sprite motion (server damage packet only). The only spawner-bearing Knight/Crusader entries in the effect dispatch (`FUN_005bd3d0`@0x5bd3d0) are the ones above plus Holy Cross (case 0xf5 → `effect\cru\holy_cross.wav` + common spawn label 0x5bf7a6).

**Dispatch reference:** effectId→function mapping lives in the big switch `FUN_005bd3d0`@0x5bd3d0. Confirmed cases: Endure 0xb, Bash 0x10 & 0x52, Magnum 0x11 & 0xaa, Provoke 0x43, GrandCross 0xe2/0x1c2, ShieldBoomerang 3/0x1d/0xf9, Kyrie/Guard 0x24/0xb8. Two-Hand Quicken (0x5d9640) and Auto-Counter (0x5d93a0) are invoked from the per-actor state path, not this table (sound-only, torso-anchored).
