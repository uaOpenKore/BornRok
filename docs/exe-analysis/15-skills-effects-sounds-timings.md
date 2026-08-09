# Skills — effects, sprites, sounds & timings (complete)

How every skill is visualised and timed: the client's effect-dispatch channels, the
sprites/textures and sounds each effect uses, and the exact timings/delays — client-side
(effect lifespans, from the exe) and server-side (cast time / after-cast delay / duration,
from `skill_cast_db`). Anchored to real addresses in `winEXE/uaRO.exe`; effect visuals draw
through the engine documented in `14-effects-particles-render-deep-dive.md`.

**Per-skill dossiers** (minute detail — sounds, sprite sequence, geometry, anchor, phase
timing for each named skill's effect class) live in [`skills/`](skills/README.md), one file
per class.

Bulk data lives next to this file:
- [`data/effect-timing-table.tsv`](data/effect-timing-table.tsv) — every `effectId → lifespan(ms)` (from `DAT_006d9100`).
- [`data/skill-server-table.tsv`](data/skill-server-table.tsv) — all 650 skills: `id, SKID, name, target, maxLv, cast, actDelay, walkDelay, dur1, dur2` (from server `skill_db`+`skill_cast_db`).

---

## 0. The load-bearing fact

**The client has no single `switch(skillId) → effect` table.** Skill visuals are driven by
**five server-packet channels** (routed through the master dispatcher `FUN_00579900`
`0x00579900` → the actor message handler `FUN_0054cfc0` `0x0054cfc0`), and the *exhaustive*
per-skill on-hit effect map is **data-driven**: an array at **`actorObj+0x544`** indexed by
`skillId` (0..0x3FC), populated from **GRF Lua at load** (not a switch in the binary). On
top of that array the exe adds a layer of **hard-coded tables** (cast circles, ground
fields, status effects, special combos) — all reproduced below.

Consequence for the port: to cover *every* skill you load the same Lua table the exe reads
(`obj+0x544`); the exe's hard-coded tables here cover cast circles, ground/persistent
effects, statuses, and the bespoke multi-effect skills.

---

## 1. The five dispatch channels

| # | packet | handler | meaning | anchor |
|---|--------|---------|---------|--------|
| 1 | `0x13E` ZC_USESKILL_ACK | `FUN_0058b560` @ `0x0058b560` | **cast start** — cast bar + ground cast-circle + cast motion | caster (ground circle under caster) |
| 2 | `0x1DE` ZC_NOTIFY_SKILL2 | `FUN_0059aa50` → on-hit `FUN_005566e0` @ `0x005566e0` | **damage skill hit** — attack fx on caster + effect on target | target (+ caster attack fx) |
| 3 | `0x11A` ZC_USE_SKILL | `FUN_0058ba60` → `FUN_00557540` @ `0x00557540` | **no-damage / ground skill** — persistent ground field | ground cell (from packet) |
| 4 | `0x1F3` ZC_NOTIFY_EFFECT2 | actor msg `0x6E` in `FUN_0054cfc0` | **status / EF effect** (apply/remove) | self (looped) |
| 5 | `0x19B` ZC_NOTIFY_EFFECT | `FUN_00594c80` @ `0x00594c80` | **generic** (level-up, refine, pharmacy) | self |

Core primitives: **`FUN_00549180(effectId, x, z, y, dir)`** `0x00549180` spawns an effect
(when called inside an actor's handler it attaches to *that* actor = self anchor);
**`FUN_005c2630`** the effectId→`.str`/coded factory; **`FUN_00549ae0(id)`** the "already
live?" loop guard; **message `0x6D`** removes a looped effect; **`0x55`/`0x56`** start a
looped cast-circle under the caster; **`FUN_0055d370(&groundCell, effectId, dur, 0)`** spawns
a **map-cell-anchored persistent** effect.

---

## 2. Cast bar & cast-circle (Channel 1) — `FUN_00644ed0`

On `0x13E` the client reads `skillId` and calls **`FUN_00644ed0(skillId, &circleId, &castMotion,
element)`** `0x00644ed0`, then starts the looped ground **cast-circle** under the caster
(`0x55(circleId)` + `0x52(castDuration)`) and plays the caster's `castMotion`.

- Default: `circleId = 0x10` (generic yellow ring), `castMotion = 8`.
- `circleId = 0xC` is an **element placeholder** remapped by the skill's element:
  1(water)→`0x36`, 2(earth)→`0x38`, 3(fire)→`0x37`, 4(wind)→`0x39`, 5(holy)→`0x3B`,
  6(dark)→`0x3A`.
- `circleId = -1` → **no cast circle** (e.g. instant/self buffs).
- Wizard/Sage high-tier cast circle = `0x1B9`; special rings `0x156/0x157`, `0x1F5/0x1F6`,
  `0x23D–0x23F`; `0x3A/0x3B` for holy/dark.

Cast add-ons: `0x1EA(490)→0x21B` on target; `0x191(401)→ particle + 0x192`;
`0x105/0x10C/0x3F7→ particle FUN_00552bd0`. Cast time itself (the bar length) is **server**
(`skill_cast_db` CastingTime, sent in the packet) — see §7.

---

## 3. Persistent ground fields (Channel 3) — `FUN_00557540`

Ground-target skills spawn a **cell-anchored looped** effect via `FUN_0055d370`. This is the
Sanctuary/Storm-Gust/Fire-Wall/Warp family. Complete exe table:

| skillId | SKID | ground effectId | notes |
|---|---|---|---|
| 0x15 (21) | WZ_… Thunderstorm-set | `0x1E` | |
| 0x19 (25) | AL_PNEUMA | `0x8D` | dur 10 s |
| 0x45 (69) | PR_BENEDICTIO | `0x5B` | |
| 0x46 (70) | PR_SANCTUARY | `0x53` | + warp-style anim |
| 0x4F (79) | PR_MAGNUS | `0x71` | |
| 0x53 (83) | WZ_METEOR | `0x5C` | |
| 0x55 (85) | WZ_VERMILION | `0x5A` | |
| 0x57 (87) | WZ_ICEWALL | `0x4A` | dur 5 s |
| 0x59 (89) | WZ_STORMGUST | `0x59` | dur 4.6 s |
| 0x5B (91) | WZ_HEAVENDRIVE | `0x8E` | |
| 0x5C (92) | WZ_QUAGMIRE | `0x5F` | |
| 0x6E (110) | BS_HAMMERFALL | `0x66` | |
| 0x6F/0x1CB | BS_ADRENALINE / …2 | `0x62` | |
| 0x73 (115) | HT_SKIDTRAP | `0x45` | |
| 0x82 (130) | HT_DETECTING | `0x77` | + child obj `0xC2` |
| 0x8C (140) | AS_VENOMDUST | `0x7C` | |
| 0x11D–0x120 | (advanced) | `0xE1/0xEC/0xED/0xEE` | |
| 0x1E3 (483) | Fire Wall (renewal id) | `0xDF` | plays `EF_FireWall.wav` |
| 0x1DE (478) | Grimtooth-class | `0x1F1`(<lv6) / `0x1F2`(lv6–9) / `0x1F3`(≥lv10) | + `assulter_attack.wav` |
| 0x53 (83) | WZ_METEOR (open) | + `FUN_00548270` warp-open anim | |

(The above skillIds match the classic RO ids — cross-check `data/skill-server-table.tsv`.)
The ground effect's **lifespan** is `DAT_006d9100[effectId]` (§7) but the field's real
gameplay **Duration** is the server `skill_cast_db` Duration1 (e.g. Storm Gust 4600 ms).

---

## 4. Damage-skill on-hit visuals (Channel 2) — `FUN_005566e0`

For each hit the target handler runs three data-array lookups plus special cases:
- **`FUN_006445f0(skillId, &atkEffect, &motion)`** `0x006445f0` → the **caster's** attack
  effect + action motion (default `atkEffect=0x10`, `motion=7`).
- **`FUN_00636480(skillId)`** `0x00636480` → the **on-target** effectId from the
  data-driven array `obj+0x544` (the exhaustive per-skill table). Spawned then positioned on
  the target (msg `0x0E`).
- **`FUN_006364b0(skillId)`** `0x006364b0` → a secondary/blow effect from `obj+0x554`.
- `FUN_005066d0(skillId)` decides whether the caster self-effect also spawns.

Hard-coded multi-effect skills (all extra `FUN_00549180` spawns, caster unless noted):
`0x110→0x106,0x107,0x111`; `0x111→0x107,0x14A`(+shake); `0x172→0x178`;
`0x173→0x179,0x107`; `0x174→0x107,0x200`(target); `0x88→0x6C/0x7A` (multi-hit);
combo attacks `0x19D/0x19F/0x1A1/0x1A3/0x1A5` (msg `0x86` per hit); `0x8D→0x81`;
`0x296→0x174`.

---

## 5. Status / EF effects (Channel 4) — `FUN_0054cfc0` msg `0x6E`

`param4 = EF#`, `param5 = apply(≠0)/remove(0)`; **self-anchored, looped** (prior instance
removed via `0x6D` before re-spawn). Selected entries (EF# → effectId(s)):

| EF# | effectId(s) | notes |
|---|---|---|
| 0x98 (152 Blind) | `0x1DA + level` (0x1DB–0x1E4) + `0x1E7` | + `_blind.wav`; level from lua tbl 0x1B9 |
| 0xA5/A6/A7 (Quicken) | `0x1A2` + `0x220` | |
| 0x115–0x119 (stat-ups) | `0x1C8` | + floating "ATK/FLEE/HP/SP/DEX UP" text |
| 0x1F (31) | `0xA6` colour 0xA9 | (aura tint) |
| 0x44 (68) | `0xA6` colour 0xFA | |
| 0x56 (86) | `0xCB` + `0xA6` | |
| 0x67 (103) | `0x16F` + `0x1DA` | |
| 0x79 (121) | `0x18B` + `0x18C` | |
| 0x85/0x86 | `0x1BB` / `0x1AA` | |
| default | loop-remove `0x1A8` + fog `FUN_005eae00` | |

Death/respawn (msg `0x7E`): `0x16A×2 + 0x18D + 0x18E` (map-conditional variants).

Channel 5 (`0x19B`): `0→0x173` level-up, `1→0x9E`, `2→0x9B`, `3→0x9A`, `5→0x131`,
`6→0x132`, `7→0x152`, `8→0x151`, `9→0x246`.

---

## 6. Sprites, textures & sounds per skill

### 6.1 What "sprite" means for a skill effect
- **`.str` effects** — layered texture-keyframe animations; the textures are the `.str`'s
  own layer bitmaps (`effect\*.bmp/.tga`). effectId→`.str` name via `FUN_005c2630` (e.g.
  `0x239 defense.str`, `0x1B8 asum.str`). See doc 14 §3.
- **Coded/particle effects** — emitters loading specific `effect\*.tga` textures (clouds,
  rings, bolts). Full catalogue in doc 14 §5 (e.g. Grand Cross = `cross_old.bmp` +
  `explosive_1_128.bmp` + `alpha_center.tga`; Asura = `asura1..16.tga`; Soul Breaker =
  `soul_s/o/u/l.tga`).
- **Caster-attached `.spr`** — a few skills (Sight, Detoxify) draw a `이팩트\*.spr` sprite on
  the caster rather than a `.str` (port memory `sprite-func-skill-effects-gap`).
- **Cast circle** — the ground ring emitters (`ring_white/red/blue/… .tga`), flat-on-ground
  render mode (doc 14 §8).

### 6.2 Cast sounds (per skill/effect)
Each effect class plays its own `.wav` via **`FUN_004396b0(name, x,y,z, vol, pan, pitch)`**
`0x004396b0`. Representative (full list in the sound scratch; here the headline skills):

| skill | cast wav |
|-------|----------|
| generic spell cast start | `EF_BeginSpell.wav` |
| Bash | `EF_Bash.wav` (+ weapon `_hit_*`) |
| Magnum Break | `EF_MagnumBreak.wav` |
| Fire Bolt/Ball | `EF_FireBall.wav` |
| Fire Wall | `EF_FireWall.wav` |
| Frost Diver | `EF_FrostDiver.wav` (cast) / `EF_FrostDiver2.wav` (hit) |
| Storm Gust | `wizard_stormgust.wav` |
| Meteor Storm | `wizard_meteor.wav` / impact `wizard_meteo.wav` |
| Lord of Vermilion | (thunder cluster) |
| Ice Wall | `wizard_icewall.wav` |
| Heaven's Drive / Earth Spike | `wizard_earthspike.wav` |
| Sanctuary | `priest_sanctuary.wav` |
| Magnus Exorcismus | `priest_magnus.wav` |
| Grand Cross | `cru_grand_cross.wav` |
| Sonic Blow | `assasin_sonicblow.wav` |
| Asura Strike | `apocalips_attack.wav` |
| Soul Breaker | (Soul Breaker render) |
| Teleport / Warp open / enter | `EF_Teleportation.wav` / `EF_ReadyPortal.wav` / `EF_Portal.wav` |
| Blitz Beat | `hunter_blitzbeat.wav` (+ `_1st`) |
| Two-Hand Quicken | `knight_twohandquicken.wav` |
| Heal | `_heal_effect.wav` |

### 6.3 Hit / weapon sounds
Melee **skill** hits reuse the normal-attack weapon table (so Bash layers `EF_Bash.wav` over
the weapon `_hit_*`): builders `FUN_00637590` (hit) / `FUN_00637af0` (attack) fill a table
at `this+0x18cc` (stride 0x10) indexed by **weapon type** (0/1 mace, 2/3/4 sword, 5 bow/arrow,
6/7 spear, 8/9 axe, 10 rod). Selector **`FUN_006379e0(weaponType)`** `0x006379e0`:
`-1`→random `_enemy_hit_normal1..4`, `0`(fist)→random `_hit_fist1..4`, else the table entry;
elemental overrides `_enemy_hit_fire1/2`, `_enemy_hit_wind1/2` by hit element. Generic magic
hit-splash sounds `EF_hit2..hit6.wav` scale with damage magnitude. Element-arrow hits use the
numbered builder `FUN_006713a8` (`EF_FireArrow_%d`, `EF_IceArrow_%d`, `rand()%3+1`).

---

## 7. Timings & delays (the exact numbers)

Three independent clocks govern a skill's visuals; **do not conflate them**:

### 7.1 Client effect lifespan — `DAT_006d9100[effectId]` (from the exe)
How long the **visual** persists once spawned, in ms (`INF = 0x05F5E0FF`). Full dump in
[`data/effect-timing-table.tsv`](data/effect-timing-table.tsv). Coded range 300–373:

```
300:300  301:INF 302:99999 303:300 304:200 305:90  306:90  307:36000 308:200 309:299
310:300  311:300 312:100  313:100 314:200 315:9999 316:200 317:9999 318:9999 319:9999
320:100  321:INF 322:36000 323:36000 324:36000 325:100 326:340 327:340 328:1000 329:100
330:200  331:300 332:300  333:INF 334:INF 335:INF 336:200 337:200 338:200 339:100
340:3000 341:9999 342:1000 343:1000 344:100 345:INF 346:INF 347:9999 348:1000 349:INF
350:INF  351:INF 352:INF  353:INF 354:INF 355:INF 356:INF 357:INF 358:4000 359:4000
360:4000 361:300 362:INF  363:100 364:100 365:300 366:200 367:60  368:99999 369:99999
370:19999 371:200 372:200 373:50
```
So e.g. Grand Cross beam (365) shows for **300 ms**, Asura burst (328) **1000 ms**, cloud
(307/322-324) **36 s**, aura/wing/water types **∞** (dismissed by the `0x6D` remove).

### 7.2 Server cast / delay / duration — `skill_cast_db` (per level)
The gameplay timings the server enforces and streams to the client (cast bar length comes in
`0x13E`). Columns: **CastingTime, AfterCastActDelay, AfterCastWalkDelay, Duration1, Duration2**
(colon-separated per skill level). Full table: [`data/skill-server-table.tsv`](data/skill-server-table.tsv).
Headline examples (level 1 → …):

| skill | cast (ms) | after-cast act delay | duration |
|-------|-----------|----------------------|----------|
| Storm Gust (89) | 6000→15000 | 5000 | 4600 |
| Meteor Storm (83) | 15000 | 2000→7000 | 500 (per meteor) |
| Magnus Exorcismus (79) | 15000 | 4000 | 5000→14000 |
| Grand Cross (254) | 2000 | 1500 | 900 |
| Asura Strike (271) | 4000→2000 | 3000→1000 | — |
| Soul Destroyer (379) | 700 | 1000→2800 | — |
| Sanctuary (70) | 5000 | 0 | 4000 |
| Pneuma (25) | 0 | 0 | 10000 |
| Ice Wall (87) | 0 | 0 | 5000 |
| Heal (28) | 0 | 1000 | 0 |
| Fire Bolt (19) | 700 | 1000 | 0 |

**Cooldown:** this `skill_cast_db` format has no separate cooldown column — cooldown/global
skill delay is `AfterCastActDelay` plus the server's `battle_conf` `default_skill_delay`;
true per-skill cooldowns (renewal) would live in `skill_cast_db2`/`skill_db` if present.

### 7.3 Animation frame delay
The caster's **cast motion** and the effect's frame pacing come from the `.act`/`.str` fps:
`.act` per-frame **delay** (scaled by ASPD for attack motions), `.str` header **fps** field
(doc 6 / doc 14 §3). The cast-motion id per skill is the `castMotion` out-param of
`FUN_00644ed0` (default 8; e.g. `0x12`, `0x13`, `0x1C`, `0x29`, `0x2A` for specific tiers).

---

## 8. Master reference (headline skills)

Merged view (server `skill_db`+`skill_cast_db` × client exe channels). `cast/delay/dur` are
level-1 ms; `groundEff`/`castCircle` are client effectIds; sound is the cast wav.

| id | SKID | name | target | cast | delay | dur | client effect | cast wav |
|----|------|------|--------|------|-------|-----|---------------|----------|
| 5 | SM_BASH | Bash | enemy | 0 | 0 | — | on-hit (data arr) + `EF_Bash` | EF_Bash.wav |
| 19 | MG_FIREBOLT | Fire Bolt | enemy | 700 | 1000 | — | bolt (coded) | EF_FireBall.wav |
| 25 | AL_PNEUMA | Pneuma | ground | 0 | 0 | 10000 | ground `0x8D` | — |
| 26 | AL_TELEPORT | Teleport | self | 0 | 0 | — | `0x9F`? Teleport | EF_Teleportation.wav |
| 28 | AL_HEAL | Heal | support | 0 | 1000 | — | heal effect | _heal_effect.wav |
| 70 | PR_SANCTUARY | Sanctuary | ground | 5000 | 0 | 4000 | ground `0x53` | priest_sanctuary.wav |
| 79 | PR_MAGNUS | Magnus Exorcismus | ground | 15000 | 4000 | 5000+ | ground `0x71` | priest_magnus.wav |
| 83 | WZ_METEOR | Meteor Storm | ground | 15000 | 2000+ | 500 | ground `0x5C` | wizard_meteor.wav |
| 85 | WZ_VERMILION | Lord of Vermilion | ground | 15000 | 5000 | 4000 | ground `0x5A` | (thunder) |
| 87 | WZ_ICEWALL | Ice Wall | ground | 0 | 0 | 5000 | ground `0x4A` | wizard_icewall.wav |
| 89 | WZ_STORMGUST | Storm Gust | ground | 6000+ | 5000 | 4600 | ground `0x59` | wizard_stormgust.wav |
| 91 | WZ_HEAVENDRIVE | Heaven's Drive | ground | 1000 | 700 | 500 | ground `0x8E` | wizard_earthspike.wav |
| 92 | WZ_QUAGMIRE | Quagmire | ground | 0 | 1000 | 5000 | ground `0x5F` | wizard_quagmire.wav |
| 110 | BS_HAMMERFALL | Hammer Fall | ground | 0 | 0 | — | ground `0x66` | — |
| 136 | AS_SONICBLOW | Sonic Blow | enemy | — | — | — | on-hit sonicblow | assasin_sonicblow.wav |
| 254 | CR_GRANDCROSS | Grand Cross | self/gnd | 2000 | 1500 | 900 | coded 365 (0x300ms) | cru_grand_cross.wav |
| 271 | MO_EXTREMITYFIST | Asura Strike | enemy | 4000→2000 | 3000→1000 | — | coded 328 (1000ms) | apocalips_attack.wav |
| 379 | ASC_BREAKER | Soul Destroyer | enemy | 700 | 1000+ | — | Soul Breaker render | (soul breaker) |
| 483 | (Fire Wall renewal) | Fire Wall | ground | — | — | — | ground `0xDF` | EF_FireWall.wav |

(For every other skill: server timings in `data/skill-server-table.tsv`; on-hit effectId in
the Lua `obj+0x544` array; visual/texture/sound resolved via the channels + doc 14.)

---

## 9. Address index

| role | address |
|------|---------|
| master packet dispatch | `0x00579900` |
| actor message handler (0x6E status / 0x7E death) | `0x0054cfc0` |
| spawn effect | `0x00549180` |
| effect factory (id→.str/coded) | `0x005c2630` |
| loop guard / loop mgr (0x55/56/6D) | `0x00549ae0` / `0x00549670` |
| ground persistent spawn | `0x0055d370` |
| **cast bar 0x13E** | `0x0058b560` |
| **cast-circle table** | `0x00644ed0` |
| **damage 0x1DE → on-hit** | `0x0059aa50` / `0x005566e0` |
| **no-damage/ground 0x11A** | `0x0058ba60` / `0x00557540` |
| generic 0x19B | `0x00594c80` |
| attack fx + motion (per skill) | `0x006445f0` |
| on-target effect array (obj+0x544) | `0x00636480` |
| blow effect array (obj+0x554) | `0x006364b0` |
| self-effect predicate | `0x005066d0` |
| **positional sound** | `0x004396b0` |
| weapon hit/attack tables + selector | `0x00637590` / `0x00637af0` / `0x006379e0` |
| numbered-wav builder | `0x006713a8` |
| effect lifespan table | `DAT_006d9100` |

Related: `14-effects-particles-render-deep-dive.md` (how effects draw), `08-effect-engine.md`,
`06-sprite-spr-act.md`, `effect-ids-from-exe.md`; memories `skill-effects-key-by-id`,
`skill-inf-and-nonstd-ids`, `sprite-func-skill-effects-gap`, `skill-packet-audit-2026-07`.
