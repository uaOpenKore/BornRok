# EffectTool emitter format (modern data-driven particle effects)

Besides the exe's hardcoded/`.str` effects, the modern client carries **228
`data/luafiles514/lua files/effecttool/*.lub`** scripts — one per placed/instance effect
(named after the map/instance, e.g. `1@4mag`, `1@20cn1`, …). Each defines a global
`<name>_emitterInfo` = an **array of particle emitters** with a full CPU-particle
configuration. This is the RO "EffectTool" emitter format (the data-driven equivalent of
the coded particle engine documented in `14-effects-particles-render-deep-dive.md`).

Decoded with the patched Lua 5.1 VM (see `lua-data-tables.md`).

## Schema — one emitter (from `1@4mag._14mag_emitterInfo[0]`)

```lua
{
  pos          = [209.5, -131, 132.6],   -- world spawn position (x,y,z)
  dir1         = [-3, -5, -3],           -- min emission direction
  dir2         = [ 5,  0,  5],           -- max emission direction (velocity randomised in [dir1,dir2])
  gravity      = [0.7, -2, 0.7],         -- per-axis acceleration
  speed        = [0.3],                  -- initial speed multiplier
  life         = [2, 2],                 -- particle lifetime range (seconds)
  maxcount     = [5],                    -- max live particles
  rate         = [1, 3],                 -- spawn rate range (per tick)
  size         = [3, 3],                 -- [start, end] size
  radius       = [0, 0, 0],              -- spawn spread radius
  rotate_angle = [0, 0, 0],              -- rotation
  color        = [160, 80, 140, 200],    -- RGBA tint
  srcmode      = [9],                    -- D3D SRCBLEND factor
  destmode     = [2],                    -- D3D DESTBLEND factor
  billboard_off= [0],                    -- 0 = camera billboard, 1 = fixed orientation
  zenable      = [1],                    -- Z-test on
  texture      = "effect\star02.bmp",    -- particle texture
}
```

Field notes:
- **`srcmode`/`destmode`** are DirectX `D3DBLEND` enum values (same as the render core,
  doc 14 §7): `2`=ONE, `5`=SRCALPHA, `9`=SRCALPHA?/BOTHSRCALPHA per build — additive when
  `src=ONE,dst=ONE`, alpha when `src=SRCALPHA,dst=INVSRCALPHA`. So each emitter picks its
  own blend, exactly like the coded/`.str` particles.
- **`pos`** is in the map's world coordinate space (the effect is placed at a fixed spot);
  `dir1..dir2` + `gravity` + `speed` drive the classic parametric motion (doc 14 §4).
- **`life`** here is in **seconds** (float), converted to frames by the engine's fps.
- **`texture`** is a `effect\*.bmp/tga` under the GRF, same texture pool as the coded
  effects (doc 14 §5 inventory).

An `_emitterInfo` array holds N emitters (e.g. `1@4mag` = 25) that together compose one
scene effect (smoke + sparks + glow layers). The client's EffectTool loader instantiates
each emitter through the same particle factory (`FUN_005c3670`-class) at the given `pos`.

## Files
228 `effecttool/*.lub`, one per instance/effect name. To decode any:
`unzip -j gro.zip "data/luafiles514/lua files/effecttool/<name>.lub" && LUB=<name>.lub
<lua5.1> dump.lua`. These are **map/instance ambient effects** (smoke, magic-room glows,
etc.), not per-skill — skill effects are exe-hardcoded (dossiers) + the 29-skill
`skilleffectinfolist` (doc `skills/lua-skilleffectinfolist.md`).

## Related tables decoded alongside (in `data/lua-tables/`)
- `skillinfolist.SKILL_INFO_LIST.tsv` — **1554** skills: `SkillName`, `MaxLv`,
  `AttackRange`/`SpAmount`/`ApAmount` per level, `_NeedSkillList` (tree prereqs),
  `bSeperateLv`. The client-side skill DB.
- `damageskinlist.DSList.tsv` — damage-number skin sets.
- `titletable.title_tbl.tsv` — 68 player titles.
- `effecthatitemtable.effectHatItemTable.tsv` — headgear item → aura effect id
  (`hateffectinfo`).
- `enumvar.EnumVAR.tsv` — 255 client enum variables.
