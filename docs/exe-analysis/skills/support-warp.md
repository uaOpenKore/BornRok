# Support / buffs / warp

> Per-skill effect dossiers reversed from `winEXE/uaRO.exe`. See [`README.md`](README.md) for the shared conventions and field meanings.

## Support/buffs & warp — skill effect dossiers

All handlers below are per-frame effect updaters. They receive `param_1` = the effect-instance struct; key fields are `+0x110` = elapsed-frame counter (0 on the spawn frame), `+0x114` = total lifespan, `+0xf4` = owner actor, `+4/+8/+0xc` = world x/y/z (re-copied from the owner every frame → the effect **tracks the caster/target actor**). Sub-particles are spawned through the factory `FUN_005c3670(type,0,0,0)` into `DAT_007710a8`; the returned emitter is then hand-configured. Recurring emitter fields: `+0x198`=particle life (frames), `+0x1a4`=fade-out start frame, `+0x17c`=flags (`=5` → **ground-plane quad**; `|0x200` → screen/billboard-lock), `+0x190`(=`400`) flag word, `+0xfc`=texture handle array (`+0x1b8`<<2 alloc), `+0x2a8`=blend (`0`=additive), `+0x358/35c/360`=velocity, `+0x130..0x144`=camera right/up basis (screen-space emission), `+0x27c/0x278`=start scale, `+0x2e4`=end scale, `+0x240/0x244`=alpha & alpha-delta, `+0x290`=spin angle. Colour bytes `+0x1f0/0x1f4/0x1f8` = RGB(0x20,0xB0,0xE8) = pale cyan-blue.

The camera-relative listener position (`FUN_004f6ad0()→+0xc4→+0x3c`, offsets +4/+0xc) is subtracted from the effect world pos before every `FUN_004396b0` call, i.e. **positional 3D audio anchored at the effect**, gain 0xfa(250)/0x28(40)/1.0.

---

### Heal (skill→effect dispatcher) — `FUN_0056b540` @0x56b540
Not a visual per se — this is the **skill-packet → effectId router** that also fires the one-shot cast SFX and then calls the generic spawner `FUN_00549180(effectId,...)`. Giant `switch(param_2=skillId)`. Heal-family cases (`0x2f5a–0x2f5c`, `0x2fa3–0x2fa6`, and the `0x2fcd/0x2f63` group) → **`_heal_effect.wav`** (one-shot, position 0,0,0 = at caster) + effectId `0x1eb`; other branches emit bolt/monster-skill ids (0xcc–0xd3, 0x242–0x245, 0x251–0x256 elemental, 0x25b `evil_cloud_hermit_attack.wav`, 0x264, etc.). Purges any live copy of the effectId (`FUN_00549ae0` loop) before respawn. **The actual Heal green-sparkle visual is `FUN_005c7360`/`FUN_005c5d30` below.**

### Heal (green sparkle burst) — `FUN_005c7360` @0x5c7360
- **Sound:** `_heal_effect.wav`, one-shot on spawn frame only, positioned at caster.
- **Spawn frame (`+0x110==0`):** builds **3 concentric expanding rings**, factory type `0xd`, texture **`effect\alpha_down.tga`** (soft radial gradient, additive). The three rings share life 100 frames, colour RGB(32,176,232), and differ only in radius: start-scale 2.5→end 15, 3.5→25, 4.5→35 (`+0x27c/0x278`→`+0x2e4`); fade begins at life−20.
- **Every frame while `(life & 0x80000001)==0` (even frames):** spawns one **falling green sparkle**, factory type `7`, life 30, random azimuth (`rand%360` into `+0x290`), spin rate −1.2/life, downward drift, alpha-gravity `0.55` (`+0x29c=0x3f0ccccd`), fade at life−life/3. Uses sprite tables `DAT_006dc060`/`DAT_006dc048` via `FUN_005481c0`/`FUN_00548230`+`FUN_006113a0` (the classic 힐/heal sparkle .spr sequence).
- **Geometry:** 3 ground-facing billboard rings + a stream of camera-billboard sparkle sprites. **Anchor:** target actor (heal is cast on a target). Additive.

### Heal (single-ring variant / low-level) — `FUN_005c5d30` @0x5c5d30
Same as above but **one** ring only (start-scale 4.5→35, life 100, `alpha_down.tga`, additive) and a sparse sparkle: emits a type-7 sparkle (life 50) **once every 9 frames** (`life%9==0`), gated on `DAT_00772d2c==0`. Sprite tables `DAT_006dbf2c`/`DAT_006dbf14`. Same `_heal_effect.wav` on spawn. Anchor: target. This is the reduced/cheap Heal.

### Blessing — `FUN_005c6af0` @0x5c6af0
- **Sound:** `effect\EF_Blessing.wav`, one-shot at spawn, at caster.
- **Spawn frame:** one **expanding halo ring**, factory type `0xc`, texture **`effect\alpha_down.tga`**; flag `+0x190|1`, `+0x17c|0x200` (screen-locked billboard), radius param `+0x2cc=10.0`, `+0x294=90.0`, life=`+0x114`, colour RGB(32,176,232), end-scale 100, fade at life−30. Additive.
- **First 10 frames, every 3rd frame (`life%3==0 && life<10`):** spawns a type-`5` **rising column sprite** (`+0x19c=2`), initial upward velocity y=−25 (`+0x35c=0xc1c80000`), horizontal offset shrinking `(life*-6+200)`; sprite tables `DAT_006dbff0`/`DAT_006dbfe0`.
- **Every frame while `(life & 0x80000003)==0` (every 4th frame):** spawns a **swirling rune/spark**, type `5`, life 0x50(80), flag `+0x190|0x80`, random azimuth, radial velocity `(rand%7+2)` along camera-right minus a fixed camera-forward pull, downward-tilt (−90°), spin, alpha 0.5, gravity. Sprite tables `DAT_006dbfc8`/`DAT_006dbfb0`. The classic blue Blessing sparkle column + orbiting motes. **Anchor:** target actor. Additive.

### Increase AGI (full) — `FUN_005cced0` @0x5cced0
- Computes a **screen-space projected anchor** for the actor via `FUN_00412cb0` (world→screen, stored to `+0x130/0x134`) so emitters ride the sprite.
- **Sound:** `effect\EF_IncAgility.wav`, one-shot, at caster.
- **Spawn frame:** rising **glow plume**, factory type `0` (generic billboard), texture **`effect\agi_up.bmp`**, life=`+0x114`, upward growth `+0x240=1.5`, size 40→height 20, end-scale 200, fade at life−15; `+0xf704=99` (loop/hold flag). Additive.
- **Odd frames (`life&1==0`):** spawns an **upward-streaking spark**, type `0x10`(16), life 50, texture **`effect\ac_center2.tga`**, random azimuth+elevation, outward+upward velocity `(rand%7+2)`, sizes randomised (`rand%50+20`, `rand%60+30`), end-scale 200, `+0xf704=99`. Additive. **Anchor:** target actor (self-buff → on the recipient), body/feet-tracked in screen space.

### Increase AGI (spark-only variant) — `FUN_005dad30` @0x5dad30
Same projection + `EF_IncAgility.wav`, but **no** central `agi_up.bmp` plume — only the odd-frame `ac_center2.tga` type-16 upward sparks (life 50, randomised azimuth/size, end-scale 60→`0x42700000`, extra random tint jitter on `+0x2e4`). Lighter AGI-up used when the plume isn't wanted.

### Decrease AGI — `FUN_005cd2b0` @0x5cd2b0
- **Sound:** `effect\EF_DecAgility.wav`, one-shot at caster.
- **Spawn frame:** a **sinking/descending glow**, type `0`, texture **`effect\slow.bmp`**; note the *downward* setup — velocity y lowered (`+0x35c -= const`), `+0x290=180.0`, `+0x244` is **negative** (shrinks: `-(1.0/life)`), start at 40, `+0x2e4=10`, end-scale 200, fade life−15, `+0xf704=0` (one-shot, no loop). Additive.
- **Odd frames:** downward-drifting sparks, type `0x10`, life 50, `ac_center2.tga`, velocity biased down (`+0x35c -= const`, tiny `+0x244=0.015`), sizes `rand%60+30`. Anchor: target. The inverse motion signature of Inc AGI.

### AGI/DEX Up (Improve Concentration) — `FUN_005c6f80` @0x5c6f80
Structurally identical to Inc AGI full (`FUN_005cced0`) but:
- **Sound:** `effect\EF_IncAgiDex.wav`.
- Central plume texture is **`effect\dex_agi_up.bmp`** (`+0xf704=0`, one-shot).
- Odd-frame sparks reuse `effect\ac_center2.tga`, type 16, life 50, upward, end-scale 200. Anchor: target actor, screen-projected.

### Ruwach — `FUN_005d96c0` @0x5d96c0
- **Sound:** `effect\ac_concentration.wav`, one-shot at caster.
- **Visual:** just `FUN_005c3870(0, -80.0, 0)` — renders the caster's **built-in cast/AoE reveal sprite animation at torso height (−80px)**, no procedural particles. Returns "still-playing". Minimal effect (the detect-hidden ring is the actor's own .spr). Anchor: caster torso.

### Concentration (Ruwach-class self, with detox ring) — `FUN_005d9b40` @0x5d9b40
- **Sound:** `effect\ac_concentration.wav`, one-shot, position **0,0,0 (dead-centre on caster)**.
- On spawn calls helper **`FUN_005d74d0(2.0, 50)`**: one expanding ring, factory type `0xd`, texture **`effect\alpha_down.tga`**, life 50, scale 4.5→10, `+0x2e8=2.0`, end-scale 90, fade life−10 (additive blue ground ring).
- Then `FUN_005c3870(0,−80.0,0)` for the caster torso sprite. Anchor: caster.

### Endure — `FUN_005ca2b0` @0x5ca2b0
- **Sound:** `effect\EF_Endure.wav`, one-shot at caster; screen-projected anchor (`FUN_00412cb0`).
- **Spawn frame:** a **rising oval "shield" glow**, type `0`, texture **`effect\endure.tga`**, flag `+0x190|2`, initial y-vel −20, sizes `+0x2d8=150`, `+0x2e4=150`, x/z shear `+0x2dc/2e8=−5.0` (with per-frame decay), fade at life−(life/3). One-shot (`+0xf704=0`). Additive.
- **Every frame while `life<0x32` (first 50 frames):** spawns **falling motes**, type `0`, life 40, `effect\alpha_down.tga`, random azimuth, negative growth, velocity computed from `sin/cos` of the random angle (`FUN_0040ae60/0aea0` × `(rand%40+100)` speed) so they scatter outward-and-down, plus a downward bias (`+0x35c -= const`). The blue converging/settling motes of Endure. Anchor: caster (self-buff).

### Aqua Benedicta — `FUN_005cd6a0` @0x5cd6a0
- **Sound:** `effect\EF_Aqua.wav`, one-shot at caster.
- **Spawn frame only (pure one-shot):** a single type-`5` **water-splash sprite sequence**, life=`+0x114`, initial upward y-vel −20 (`+0x35c=0xc1a00000`), `+0x19c=2`, `+0x208=0`; sprite tables `DAT_006dc4e0`/`DAT_006dc4cc` via `FUN_005481c0`/`FUN_00548230`+`FUN_006113a0`. No per-frame emission. Anchor: caster.

### Cure — `FUN_005d7c50` @0x5d7c50
- **Sound:** `effect\Acolyte_cure.wav`, one-shot at target.
- **Visual:** `FUN_005c3870(0,−80.0,0)` only — the target actor's built-in status-clear sprite at torso height. No procedural particles. Anchor: target torso. (Same skeletal pattern as Ruwach `FUN_005d96c0`.)

---

### Teleport — flash ring (`FUN_005cc830` @0x5cc830)
- **Sound:** `effect\EF_Teleportation.wav`, one-shot at caster.
- **Spawn frame only:** one **screen-locked blue ring flash**, factory type `0xd`, texture **`effect\ring_blue.tga`**, life 0x3c(60), flag `+0x190|1`, scale 4.0×4.0 (`+0x27c/0x278`), shear `+0x2e8=5.0`/`+0x2ec=0.25`, `+0x2b4=2.0`, fade from frame 0 (`+0x1a4=0`, i.e. fades whole life). Additive, one-shot. Anchor: caster.

### Teleport — column pillar (`FUN_00600a00` @0x600a00)
- **Sound:** `EF_Teleportation.wav` at caster.
- **Spawn frame only:** a **ground-anchored multi-column light pillar**, factory type `0x39`(57), `+0x17c=5` (**ground quad**), texture passed in as `param_2` (caller supplies `ring_blue.tga`/beam texture), gravity `+0x29c=4.0`. Fills the large `+0xf704…+0xf9b8` block = **5 stacked keyframed vertical segments** (each a 0xb8-stride record: enable byte, angle 0x168, height, radius, colour-ramp arrays zeroed then seeded). `param_3` rotates each segment's azimuth (`param_3*60`) — used for a spinning multi-band teleport beam. One-shot. Anchor: caster ground cell.

### Teleport (arrive/other variant) — `FUN_00603750` @0x603750
Same family: factory type `0x3c`(60), `+0x17c=5` ground quad, texture=`param_2`, gravity 4.0. Configures **4 keyframed segments** (blocks at `0xf704/0xf7bc/0xf874/0xf92c`) with fixed heights (20/45/70/90 range via floats `0x41a00000…0x428c0000`) and per-segment velocities — a static (non-random) rising blue column set. One-shot at spawn. Anchor: caster ground cell.

### Warp Portal — opening cast (`FUN_005cc990` @0x5cc990)
- **Sound:** `effect\EF_ReadyPortal.wav`, retriggered **every 14 frames** (`life%0xe==0`) at caster.
- **On each 14-frame beat:** spawns an **expanding blue ring**, factory type `0xc`, texture **`effect\ring_blue.tga`**, life 0x1e(30), y-vel −1.0, radius `+0x278=8.0`, `+0x294=90`, spin `+0x2d4` from `+0x2d0=1.0`/life, fade at life−life/2. Additive, looping while the portal is being drawn. Anchor: caster.

### Warp Portal — opening cast (ground-column variant) — `FUN_00605980` @0x605980
- **Sound:** `EF_ReadyPortal.wav` every 14 frames.
- **Spawn frame only:** a **ground portal disc column**, factory type `0x3f`(63), `+0x17c=5` (ground quad), texture **`effect\ring_blue.tga`**, gravity `+0x29c=5.0`. Fills 4 segment blocks with small radii (2/3/4-cell) and offset heights (`0x40c00831`≈6.0), colour-ramp seed byte `1`. The flat rotating portal on the ground. Anchor: caster ground cell.

### Warp Portal — enter/step-through — `FUN_006070f0` @0x6070f0
- **Sound:** `effect\EF_Portal.wav`, one-shot, at **z=−150** (`0xc3160000`, below feet) — the whoosh as the character drops in.
- **Spawn frame only:** a tall **swirling multi-column vortex**, factory type `0x3c`(60), `+0x17c=5` ground quad, texture **`effect\ring_blue.tga`**, gravity 4.0. **5 segment blocks**, each with **randomised start azimuth** (`rand%360` into the angle fields) and long durations (0x578=1400) — the columns spin independently. Seed byte `2`. Heights from 4.0 up to 180.0 (`0x43340000`). One-shot. Anchor: portal cell.

### Safety Wall — `FUN_005c6730` @0x5c6730
- **Sound:** `effect\EF_GlassWall.wav`, one-shot at caster.
- **Spawn frame:** loop `local_c = 1; local_c -= 2` down to ≥−2 → **two passes** (offsets +1 and −1 across the cell), each pass spawning **two** billboard panels ⇒ builds the wall as paired quads at symmetric offsets along the camera basis (`local_44/40/3c` and `local_5c/58/54` direction vectors × `local_c`). Each panel: factory type `0xf`(15), texture **`effect\ring_blue.tga`**, life=`+0x114`, flags `+0x190=0x28`, `+0x180=0x32`, spin `+0x290` (base angle `rand`, second panel offset +90°), sizes `+0x2d8` 3.0/2.6, `+0x2e4=40`, `+0x2e8=0.25`, end-scale 180, fade at life−6. Additive.
- Also renders `FUN_005c3870(0,−80.0,0)` (caster torso sprite). **Return** compares `life != +0x114` → keeps the wall alive until lifespan hit. Anchor: caster's targeted cell (ground).

### Safety Wall — glass shard variant — `FUN_00608610` @0x608610
- **Sound:** `EF_GlassWall.wav` one-shot at caster.
- **Spawn frame only:** ground-column build, factory type `0x3c`(60), `+0x17c=5` (ground quad), texture=`param_2` (caller supplies the glass/`ring_blue` texture), gravity `+0x29c=10.0`. **4 segment blocks**, each with **random azimuth** (`rand%360`), tall durations 0xc08(3080), heights 34/37/40/45 (`0x42080000…0x42340000`). Then `FUN_005c3870(0,−80.0,0)` caster sprite; return keeps it alive until `life==+0x114`. This is the volumetric glass-pane version of Safety Wall. Anchor: caster's targeted cell.

---

**Cross-cutting notes**
- **Texture library used:** `effect\alpha_down.tga` (soft radial, heal/blessing/endure/concentration rings & motes), `effect\ring_blue.tga` (all teleport/warp/safety-wall structural quads + teleport flash), `effect\agi_up.bmp` / `effect\dex_agi_up.bmp` / `effect\slow.bmp` (self-buff central plumes), `effect\ac_center2.tga` (buff sparks), `effect\endure.tga` (Endure shield). Heal-sparkle & Blessing-column & Aqua use `.spr`/`.act` sequences via the `DAT_006dbXXX` sprite tables + `FUN_005481c0/548230/6113a0`, not `.str`.
- **Blend:** every emitter sets `+0x2a8=0` = additive; colours are the pale-cyan RGB(32,176,232) for the Acolyte support set.
- **Anchor rule:** buffs copy owner pos every frame → follow the recipient actor; teleport/warp/safety-wall set `+0x17c=5` → flat on the ground cell; the `FUN_005c3870(…,−80,…)` calls draw the actor's own torso-height cast sprite.
- **One-shot vs looping:** Heal/Blessing/AGI-family emit per-frame sub-particles for their whole lifespan; Teleport-flash, Aqua, Cure, Ruwach, and all warp/safety ground-column builders configure everything on the spawn frame only. Warp-open handlers retrigger `EF_ReadyPortal.wav` every 14 frames.
- `FUN_0056b540` is the **id router**, not a renderer — it selects the effectId and fires the initial cast SFX, then `FUN_00549180` instantiates whichever of the above handlers matches.
