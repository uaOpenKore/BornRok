# Per-skill visual dossiers

Detailed reverse of the client's per-skill effect classes: for each skill — the **sounds**
played (and when), the **textures/sprites** used and in what **sequence**, which **object**
each is anchored to, the **geometry** (camera billboard vs flat ground quad vs ribbon vs
`.spr`), and the **timing** of every phase. Companion to
[`../15-skills-effects-sounds-timings.md`](../15-skills-effects-sounds-timings.md) (the
dispatch channels + timing tables) and
[`../14-effects-particles-render-deep-dive.md`](../14-effects-particles-render-deep-dive.md)
(the render/particle engine).

Shared conventions used throughout (from the reversed engine):
- `effect+0x110` = animation tick/timeline cursor (phase gate); `effect+0x114` = lifespan;
  `effect+0xf4` = owner actor (position copied into the effect each frame).
- `FUN_004396b0(name, dx,0,dz, 0xfa,0x28,0x3f800000)` — positional 3D sound at
  owner−camera; params 250 (max-dist), 40 (min-dist), 1.0 (volume).
- `FUN_005c3670(type,x,y,z)` — spawn a particle emitter of a preset `type`; common emitter
  fields: `+0x198` particle lifespan (frames), `+0x17c` render/billboard flags
  (`|=2` = **flat-on-ground quad**, `|=0x200` = billboard), `+0xfc` texture-pointer array,
  `+0x1b8` texture count, `+0x290` yaw, `+0x35c` gravity/Z, `+0x1a4` fade-start frame,
  `+0x2d8/+0x2e4` scale.
- `FUN_005c3870(0,-80f,0)` — torso-anchored (−80 px) one-shot stock billboard.
- `FUN_0040c590(name,0)` — load a texture. Blend: additive = ONE/ONE, alpha =
  SRCALPHA/INVSRCALPHA.

## Class files
- [Mage — offensive](mage.md)
- [Wizard](wizard.md)
- [Priest](priest.md)
- [Support / buffs / warp](support-warp.md)
- [Assassin / Thief / Rogue](assassin-thief-rogue.md)
- [Swordman / Knight / Crusader](knight-crusader.md)
- [Hunter / Blacksmith / Sage / Monk](hunter-blacksmith-sage-monk.md)
- [Merchant / Alchemist / Bard / Dancer](merchant-alchemist-bard-dancer.md)
- [Transcendent & expanded classes](transcendent-expanded.md) — incl. the key finding that
  the exe's hardcoded effect engine covers only classic 1st/2nd-job skills; advanced skills
  are data-driven, with only status-icon overlays hardcoded
- [Statuses / generic hits / level-up / cast-circle geometry](status-generic-rings.md)

**Consolidated index:** [`../16-skill-master-table.md`](../16-skill-master-table.md) — one
row per skill (effect fn + textures + sound + geometry + client & server timings).
