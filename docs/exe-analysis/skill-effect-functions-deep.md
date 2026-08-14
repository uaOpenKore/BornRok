# Skill effect functions — DEEP disassembly (line-by-line, not summary)

Honest re-do after S. (2026-08-14): the earlier `16-skill-master-table.md` was a *summary*
(function address + rough geometry), and the coded effects were approximated from it, not ported
from the real per-function logic. This file records the EXACT mechanics read line-by-line from
`winEXE/decomp/uaRO_decomp.c`.

**Effect engine facts:**
- Dispatch `FUN_005c2630` handles **183 effect-id cases** and calls **56 effect constructors**;
  the 0x5c–0x5d cluster has **201 functions**. 50 skill-effect constructors carry an `EF_*` sound.
- Every particle is built by `FUN_005c3670(TYPE,0,0,0)` where TYPE selects the internal primitive
  behaviour. Common offsets on the returned particle object:
  - `+0x110` = the effect's frame counter (age in frames).
  - `+0x198` = lifespan in frames.
  - `+0x358 / +0x35c / +0x360` = velocity x / y / z.  `_DAT_0069f174` = gravity subtracted from vy.
  - `+0x2e4` (= `+0x2d8`) = size;  `+0x29c` = scale;  `+0x188` = frame-strip count;  `+0x1b8` = tex count.
  - `+0x290`/`+0x294` = spin angle/rate;  `+0x240` = spin rate = spin/life;  `+0x1a4` = fade-start frame.
- Sound: `FUN_004396b0(wav, dx, 0, dz, 0xfa, 0x28, 0x3f800000)` — 0xfa=250 range, 0x28=40, gain 1.0.

## Exact mechanics (verified from the decompiled bodies)

### Napalm Beat — `FUN_005cc300` (EF_NapalmBeat)
- Emit WHILE `+0x110 < 0xf` (15 frames). Sound only on frame 0.
- Particle **TYPE 0**, life `+0x198 = 0x1e` (30). Radial scatter: `angle = rand%0x168` (0–359°),
  `radius = rand%0x32 + 10` (10–60). `vx = cos·radius`, `vy = −sin·radius − gravity`.
  Size `+0x2e4 = rand%0x28 + 0x14` (20–60). 8-frame animated texture set (`+0x1b8=8`, base `DAT_006dc45c`).
- Anchor = the caster/target projected ground point.

### Soul Strike — `FUN_005c7920` (EF_SoulStrike)
- Soul COUNT = cast level (case 1→1 … 5→5). Per level an X-offset pair (iVar4 base, iVar5 step):
  lvl2 = base −0x5a(−90), step 0xb4(180); lvl3 = −90/0x5a(90); lvl4 = −90/0x3c(60); lvl5 = −90/0x2d(45).
- Emit ONE soul every 11 frames: gate `(+0x110 + 5) % 0xb == 0 && idx <= count`. Sound per soul.
- Particle **TYPE 8**. Horizontal placement `+0x298 = idx*step + base` (fans souls across the target).
  Vertical curve via `+0x270/+0x2b8` and derived `+0x274/+0x2c4`; spin `+0x240 = +0x2cc/life`.
- **Anchor = the TARGET cell** — souls RISE at the target, fanned by level. (My code flew them from
  the caster — WRONG; must rise at target, staggered 1 per 11 frames.)

### Cold Bolt — `FUN_005cae20` (EF_IceArrow_%d)
- On frame `0xc` (12): play `IceArrow_%d` sound (`rand%3+1`), and `+0x10c *= 10`.
- WHILE frame > 0xb: every 10th frame (`(frame−0xc)%10==0`) and frame < `+0x10c`: spawn one shard.
- Particle **TYPE 0x10** (16). `vx = rand%10 + 0x19` (25–34), `vz = rand%10 + 0xf` (15–24), vy derived.
  Random ±spin. So Cold Bolt = a STREAM of type-16 ice shards over time, NOT falling balls.

### Frost Diver — travel `FUN_005d3cf0` / impact `FUN_005cb720` (EF_FrostDiver / 2)
- Travel: marches caster→target, every 3rd frame emits a shard (**TYPE 9**), life 40, strong up-vel 20.
- Impact: one-shot 8-crystal radial shatter (**TYPE 9**), big scale, strong up-vel 30, `ice.tga`.

### Earth Spike — `FUN_005cf9f0` (mode byte param_2)
- mode 0: sound `wizard_earthspike`, texture `stone.bmp`. mode 2: `IceArrow` sound (shared with Heaven's Drive).
- Central spike + radial spikes of `stone.bmp`.

### Fire Ball — `FUN_005cab90` (EF_FireBall)
- The exe animates `이펙트\fireball.spr` IN PLACE at the target (CEffect @0x5cadde, sizes 80/130/180 by
  level). So explode-in-place IS exe-correct; a "wrong sprite" report is a fireball.spr asset issue.

### Others carrying an EF_ sound (constructors to port next, TYPE/life to be read per function):
Bash `005c7c70`, Magnum Break `005c8040`, Steal `005c8400`, Detoxify `005c8710`, Endure `005ca2b0`,
Thunderstorm `005cba90`, Teleport `005cc830`, Ready Portal `005cc990`, Inc/Dec AGI `005cced0/005cd2b0`,
Aqua `005cd6a0`, Stone Curse `005d3840`, Poison Attack `005d43b0`, Angelus `005d77a0`, Signum `005d7810`,
Fire Wall `005da5b0`, Glass Wall (Safety Wall) `005c6730`, Blessing `005c6af0`, IncAgiDex `005c6f80`.

**Method to finish:** for each constructor, read its body, record TYPE + life + velocity formula +
size + texture + frame gate, then re-implement the client particle emit to match those exact numbers
(not an eyeballed approximation).

## EXACT exe-prescribed resources (from objdump of uaRO.exe .data, VA−0x400000=file off)

The exe names the precise texture/sprite each effect loads. **Use THESE, do not pick your own.**

| Skill (fn) | exe resource(s) | in content? |
|---|---|---|
| Cold Bolt `005cae20` | `effect\icearrow.tga` + `effect\ring_blue.tga` | ✅ both |
| Frost Diver impact `005cb720` | `effect\ice.tga` | ✅ |
| Frost Diver travel `005d3cf0` | `effect\stone.bmp` | ✅ |
| Earth Spike `005cf9f0` | `effect\ice.tga` + `effect\stone.bmp` | ✅ |
| Magnum Break `005c8040/005d9810` | `effect\ring_yellow.tga` (+ 대폭발.tga) | ✅ ring |
| Bash `005c7c70` | `effect\alpha_down.tga` | ✅ |
| Endure `005ca2b0` | `effect\endure.tga` + `effect\alpha_down.tga` | ✅ |
| Inc AGI `005cced0` | `effect\ac_center2.tga` + `effect\agi_up.bmp` | ✅ |
| Inc AGI/Dex `005c6f80` | `effect\ac_center2.tga` + `effect\dex_agi_up.bmp` | ✅ |
| Dec AGI `005cd2b0` | `effect\ac_center2.tga` + `effect\slow.bmp` | ✅ |
| Stone Curse `005d3840` | `effect\alpha_center.tga` + `effect\magic_red.tga` | ✅ |
| Teleport / Ready Portal | `effect\ring_blue.tga` | ✅ |
| Safety Wall (GlassWall) `005c6730` | `effect\alpha_down.tga` + `effect\ring_blue.tga` | ✅ |
| Blessing `005c6af0` | `effect\alpha_down.tga` | ✅ |
| Poison Attack `005d4a70` | `effect\magic_red.tga` + `effect\ring_yellow.tga` | ✅ |
| **Soul Strike** `005c7920` | `이펙트\particle1.spr` (+ particle2/5) | ❌ **.spr NOT in content** |
| **Fire Ball** `005cab90` | `이펙트\fireball.spr` | ❌ **.spr NOT in content** |
| **Sight** | `이펙트\Sight.spr` | ❌ **.spr NOT in content** |
| Napalm Beat `005cc300` | numbered `%d.tga` frame strip (8) | ❓ template, resolve frames |

Conclusion: for the ✅ rows I can wire the EXACT exe texture (all present). The ❌ `.spr` effect
sprites (particle1/fireball/Sight/…) are ABSENT from the content pack — the content manager must add
them from the original uaRO GRF (`data/sprite/이펙트/*.spr`); until then those skills have no authentic
sprite and any coded stand-in is a guess.

## Napalm Beat template resolved (2026-08-14)
`DAT_006dc45c` (Napalm 8-frame set, via FUN_006713a8(buf,&DAT,i)) = raw bytes
`effect\` + cp949 `\xc6\xf8\xb9\xdf` + `%d.TGA` = **`effect\폭발%d.TGA`** (폭발 = explosion,
frames 1..8). NOT the same as Magnum's `대폭발.tga`. The classic `폭발1..8.tga` are ABSENT from
local texture_x4.zip (only renewal `explosionblaster/*` present) — likely in the game sprite/GRF.
To port faithfully: a numbered-.tga frame cycler (like a mini .str) driving 폭발1..8 over ~30 frames.
