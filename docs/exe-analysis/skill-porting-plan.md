# Skill motorics — porting gap analysis & preparation plan

Baseline checkpoint: **`v0.0.25.0-skillport-baseline`** (VERSION 0.0.25.0). This maps the
reverse-engineered original (docs 14/15/16 + dossiers + decoded data) against the **current
BornRok client** and lists exactly what to finish to port **all** skills' motorics/visuals.

## Where the client already is (strong)

| Piece | State | Client anchor |
|-------|-------|---------------|
| `.str` layered keyframe player (parse + interp + blend) | **Done** (v0x94) | `formats/Str.cpp`, `game/StrEffect.cpp` |
| effectId → `.str` map | **Done, exceeds exe** (891 + 8 seq) | `GameScene.cpp:785` `effectFxStr`, `:1704` `kFxSeqs` |
| skillId → `.str` (cast channel) | **Done** (80) | `GameScene.cpp:412` `skillFxDef` |
| CPU particle engine | **Done** | `game/Particles.hpp` |
| coded skill bursts | **Done (~25 skills)** + element fallback | `GameScene.cpp:7511` `emitSkillBurst`, `:7475` `emitElementBurst` |
| 5-channel dispatch (0x1de/0x11a/0x115/0x13e/0x1f3) | **Done** | `GameScene.cpp:4411…6621` |
| ground persistent units | **Partial (7 UNT)** | `GameScene.cpp:387` `groundUnitSkillId`, render `:12294` |
| per-skill cast sound | **Done (74)** + begin-chime | `GameScene.cpp:518` `skillSfx` |
| after-cast delay | **Done (169)** | `net/SkillCastDelay.cpp` |
| status aura | **Partial (3 EFST, 0x196 only)** | `GameScene.cpp:649` `statusAuraColor` |
| GRF Lua tables | **Partial** (efstids/stateicon/skilltree) | `resource/GrfData.cpp` |

## Gaps to close (prioritised) — each with the RE source to drive it

### P1 — Data-driven binding (structural; unlocks "all skills")
Today every mapping is a hand-maintained C++ switch keyed by skillId. To cover **all** skills
without endless manual cases, load the decoded data as tables:
- **Modern skills** → load `skilleffectinfolist` (29 Ninja/Kagerou/Oboro/Rebellion/Eclage)
  from `data/lua files/skilleffectinfo/skilleffectinfolist.lub` via the existing
  `GrfData` Lua VM (it already runs `.lub`). Fields → `beginEffectID`/`beginMotionType`
  (cast), `effectID`/`groundEffectID`/`targetEffectID`, `waveFileName`. Data reference:
  [`skills/lua-skilleffectinfolist.md`](skills/lua-skilleffectinfolist.md).
- **effectid.lub** → replace the hard-coded EF numbers with the loaded name→id map
  ([`data/effectid-map.tsv`](data/effectid-map.tsv)) so `beginEffectID=EF_X` resolves.
- Keep the C++ switches as override/fallback for classic skills (which are exe-hardcoded,
  no lua) — those are already covered by `skillFxDef`/`emitSkillBurst`.
- **Deliverable now:** the decoded tables are committed under `docs/exe-analysis/data/`;
  wiring is a `GrfData` loader addition + a `skillEffectInfo_[skid]` lookup consulted first
  in the 0x1de/0x11a/0x115 handlers.

### P2 — Ground units for ALL UNT types (traps / songs / dances)
`groundUnitSkillId()` maps only 7 UNT ids; traps/songs/dances draw nothing. Extend it using
the reversed ground-effect table (doc 15 §3, `FUN_00557540`: skillId→ground effectId, e.g.
Skid Trap 0x45, Venom Dust 0x7C, songs 0x115–0x126) + `skilleffectinfolist.groundEffectID`.
Bard/Dancer song ground quads: doc `skills/merchant-alchemist-bard-dancer.md` (each song →
`FUN_005fd800/430` + texture). Render loop already loops ground-unit `.str` at the cell.

### P3 — Cast bar widget + generic ground cast-circle
- **Cast gauge**: `castBars_[src]` already tracks start/end; add the on-screen progress bar
  widget (UI subsystem) driven by it.
- **Ground cast-circle** under the caster during cast: reversed as `FUN_00644ed0`
  (element→ring id `0x36`–`0x3B`; generic `0x10`; wizard `0x1B9`) + the **20-node ring
  geometry** (exact (x,z) in `skills/status-generic-rings.md`). Spawn on `0x13e`, remove on
  cast end/`0x1b9`. Textures: `ring_white/red/blue/purple/yellow.tga`.

### P4 — Status auras: `0x983` + full EFST set
- Add the **`0x983`** (ZC_EFST_SET_ENTER, EFST + duration) handler beside `0x196`.
- Broaden `statusAuraColor()` from 3 to the real EFST set using the decoded
  [`data/lua-tables/efstids.EFST_IDs.tsv`](data/lua-tables/efstids.EFST_IDs.tsv) (823) +
  `stateiconinfo.StateIconList` (416, has colours + descript) — the client already loads
  stateiconinfo for tooltips, so the colour data is in hand.

### P5 — Effect-duration table (coded-effect lifespans)
Client derives lifespans from the asset/packet; the exe additionally has a per-effectId
duration table. Ship [`data/effect-timing-table.tsv`](data/effect-timing-table.tsv) (700
effectId→ms) as a client lookup for coded effects (`emitSkillBurst`) so persistent auras
(∞) vs one-shots (e.g. Grand Cross 300 ms) match the original.

### P6 — Coded-effect coverage (expand ~25 → full)
Add the remaining classic coded effects from the catalogue (doc 14 §5, effectId 300–373:
clouds/rings/waterfalls/foot/etc.) and the per-skill dossiers (10 class files). Each dossier
gives textures-in-sequence + geometry + timing to reproduce faithfully. `.str`-bound skills
already resolve via `effectFxStr`; the per-`.str` layer/texture content is inventoried in
[`str-effects-inventory.md`](str-effects-inventory.md).

### P7 — Anchor parity (optional polish)
`StrEffect` uses roBrowser's `1/35` ratio + `−0.5` lift; the exe anchors effects at the
owner **torso (−80 px)** (doc 14 §3). If placements read too low/high vs official, switch to
the −80px body anchor.

### P8 — Weapon-hit sounds (optional, was removed by design)
Per-swing weapon-hit sound was intentionally removed. If desired for melee skills, re-add
using the decoded weapon-type→hit-wav table
([`data/lua-tables/_join.weapontype-sprite-hitwav.tsv`](data/lua-tables/_join.weapontype-sprite-hitwav.tsv))
+ the exe selector (`FUN_006379e0`, doc 15 §6.3).

## Preparation done in this checkpoint
- Version bumped **0.0.25.0**; tag `v0.0.25.0-skillport-baseline`.
- Full RE reference committed under `docs/exe-analysis/` (engine, dispatch, per-skill
  dossiers, master table) + machine-readable data (`data/`): effect-timing (700),
  skill-server (650), skill-master (103), skilleffectinfolist, effectid/actorstate maps,
  weapon/headgear/status-icon lua tables, `.str` inventory (2308), effecttool (226).
- These are the exact inputs each gap task consumes.

## Beyond the client code — other requirements for full skill/fx porting

These are **not** C++ gaps but are required for skills to actually appear correctly.

### A. Content / assets — **present, with a webp caveat**
- In `texture_x4.zip`: **2308 `.str`**, **~21 382 `.webp`** effect textures (incl. all
  classic coded-fx: `cross_old/asura1/ring_white/cloud11/alpha_center/explosive_1_128/
  star02/magic_target/endure/guardK`…), **88 `.ezv`**. Effect sounds: **759** in `wav.zip`.
  So the assets exist — nothing to re-pack.
- **Caveat 1 — bmp/tga → webp resolution:** the coded-fx code references `*.bmp/*.tga`;
  content ships them as `*.webp`. The client's texture loader must resolve the extension
  (it largely does — `readPreferPng`/`decodeImage`). Any coded effect still referencing a
  raw `.bmp` via a Bmp-only path needs the webp-preferring loader (memory
  `format-override-must-gate-on-decode`).
- **Caveat 2 — webp alpha:** several HD webp re-exports are flattened (no alpha / baked
  black or colour bg) → square artefacts (memory `combat-visuals-and-webp-gotchas`,
  `onTop-sprite-no-writez-particle-bleed`). **Content QA**: verify effect webp keep alpha,
  or the client bg-keys them (`keyBlackBackground`/`applyCornerKey`).
- Confirm `downloads.list` mounts `texture_x4.zip` + `wav.zip`.

### B. Server-side parity — **critical, outside the client**
Skills only animate if the **server sends the trigger packets** with the right ids:
- `0x1DE`/`0x11A` (damage/no-damage skill), `0x115` (ground), `0x11F`/`0x120` (skill unit
  enter/leave), `0x13E` (cast ack + castTime), `0x1F3`/`0x19B` (effect), `0x196`/`0x983`
  (status), `0x1D3` (sound). Verify the uAthena map-server emits these for the target
  packetver (memory `openkore-uok210-protocol-reference`).
- **Skill-id parity:** the client tables key on numeric skillId; server must use matching
  ids. Non-standard/renamed ids exist (memory `skill-inf-and-nonstd-ids`, e.g. Asura 271) →
  validate a skillId map server↔client.
- **Skill-unit → skillId** map (for P2 ground units): confirm the UNT ids the server sends.

### C. Cast/skill animation motions (sprite side)
`beginMotionType` (ACTOR_STATE ST_*) must map to a real **ACT action index** on each job
sprite. Verify every job sprite has the cast/skill motions the skills request (else the
caster just stands). Reference: `data/lua-tables/actorstate-map.tsv`.

### D. Codegen / single source of truth
The effect wiring is hand-maintained C++ switches. To keep them in sync (and to add the
data-driven P1 path), generate `skillFxDef`/`effectFxStr`/`skillSfx`/`SkillCastDelay` from
the committed TSVs (`data/*.tsv`) at build time rather than editing by hand.

### E. QA harness
A way to trigger + visually verify **every** skill: the existing `--view2d` effect inspector
+ `logSkillCastFx()` gap logger; add a per-skill cast test (server `@useskill`) and a visual
checklist keyed to `16-skill-master-table.md`. This is how 1:1 parity is validated.

### F. Blend / render parity
`.str` uses **21** src/dst combos (up to `5/9`); effecttool emitters use up to **`11`
(SRCALPHASAT)** and `10/x`. Confirm `StrEffect.cpp`'s D3DBLEND table covers `11` and every
dst factor used, else some effects blend wrong.

### G. Positional audio
Effect/cast sounds should play **3D** (distance/pan) like the exe (`FUN_004396b0`). Verify
the SDL3 audio backend does positional SFX; cp949 wav paths resolve via the `Cp949` table.

## Recommended order
P1 (data-driven binding) → P2 (ground units) → P4 (status auras) → P3 (cast bar + circle)
→ P5 (durations) → P6 (coded coverage) → P7/P8 (polish). P1 is the force-multiplier: it turns
"port each skill by hand" into "load the table", after which P2/P4/P6 mostly become data.
