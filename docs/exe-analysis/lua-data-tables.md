# GRF Lua data tables — decoded

The client's content ZIPs (`/root/uaro_content/*.zip`; `gro.zip` holds **470** `.lub`/`.lua`
files under `data/luafiles514/lua files/`) carry the game's lookup tables as **compiled Lua
bytecode**. These are the id↔name↔sprite maps the client (and a faithful port) needs.

**Decoded** by building patched Lua VMs (the bytecode is 32-bit; stock 64-bit Lua refuses
it): **Lua 5.0.3** for the `skilleffectinfo/*.lub` (see
[`skills/lua-skilleffectinfolist.md`](skills/lua-skilleffectinfolist.md)) and **Lua 5.1.5**
for the `datainfo`/`stateicon` tables here. Patches: `LoadString`/`LoadSize` read the string
length as **4 bytes**, and the header self-check accepts `sizeof(size_t)=4`. Each table is
loaded with symbolic-name proxies for enum globals (`SKID/EFID/JOBID/ACCESSORY/…`) and dumped
recursively; string values are cp949. Method saved in memory `lua-lub-decode-method`.

All extracted tables (TSV): [`data/lua-tables/`](data/lua-tables/).

## Decoded tables

| file → global | maps | rows | TSV |
|---------------|------|------|-----|
| `weapontable` → **WeaponNameTable** | weaponType → sprite suffix (`_단검` dagger, `_활` bow…) | 102 | `weapontable.WeaponNameTable.tsv` |
| `weapontable` → **WeaponHitWaveNameTable** | weaponType → hit `.wav` (`_hit_sword` etc.) | 30 | `weapontable.WeaponHitWaveNameTable.tsv` |
| `weapontable` → **Weapon_IDs** / **Expansion_Weapon_IDs** | item id → weaponType | 103 / 72 | `weapontable.Weapon_IDs.tsv` |
| `accessoryid` → **ACCESSORY_IDs** | headgear ACCESSORY_name → numeric id | 2827 | `accessoryid.ACCESSORY_IDs.tsv` |
| `accname` → **AccNameTable** | ACCESSORY_name → sprite (`_고글` goggles…) | 2827 | `accname.AccNameTable.tsv` |
| `jobname` → **JobNameTable** | JOBID → body-sprite folder name | 4891 | `jobname.JobNameTable.tsv` |
| `npcidentity` → **jobtbl** | JT_ npc name → class id | 5043 | `npcidentity.jobtbl.tsv` |
| `spriterobename` → **RobeNameTable(_Eng)** | garment id → sprite name | 315 | `spriterobename.RobeNameTable.tsv` |
| `spriterobename` → **RobeTopLayer** | garment id → draw-above-head flag | 210 | `spriterobename.RobeTopLayer.tsv` |
| `shadowtable` → **ShadowFactorTable** | job/class → shadow scale | 1236 | `shadowtable.ShadowFactorTable.tsv` |
| `efstids` → **EFST_IDs** | status name → EFST id (823) | 823 | `efstids.EFST_IDs.tsv` |
| `stateiconinfo` → **StateIconList** | EFST → status-icon `{descript, colours, haveTimeLimit,…}` | 416 | `stateiconinfo.StateIconList.tsv` |
| `petinfo` → **PetNameTable / PetEggItemID_PetJobID / PetFoodTable / PetIllustNameTable / PetAcc*** | pet id → name / egg-item / food-item / illust / accessory | ~126 each | `petinfo.*.tsv` |

### Ready-made joins
- **Headgear id → sprite**: [`data/lua-tables/_join.headgear-id-sprite.tsv`](data/lua-tables/_join.headgear-id-sprite.tsv)
  (`accId, ACCESSORY_name, sprite` — 2827 rows; e.g. `1  ACCESSORY_GOGGLES  _고글`). Directly
  answers headgear-sprite lookup (port task #135).
- **Weapon type → sprite + hit sound**: [`data/lua-tables/_join.weapontype-sprite-hitwav.tsv`](data/lua-tables/_join.weapontype-sprite-hitwav.tsv)
  (`weaponType, spriteSuffix, hitWav` — e.g. `1 _단검 _hit_sword.wav`, `4 _창 _hit_spear.wav`).
  Confirms the exe's weapon-hit-sound scheme (doc 15 §6.3) from data.

## Full content survey — 470 Lua files (by category)

| category (`lua files/…`) | count | what it is |
|--------------------------|-------|-----------|
| **effecttool** | 228 | per-effect construction scripts (one `.lub` per effect id — textures/layers/timing). The data-driven equivalent of the exe's coded effects for **modern** effects. |
| **datainfo** | 51 | the id/name/sprite tables (weapon/acc/job/npc/pet/robe/shadow/title/enumvar/…) — the valuable ones decoded above |
| **skillinfoz** | 26 | skill descriptions/tree (`skilldescript`, `skillinfolist`, per-lang) |
| **stateicon** | 18 | status-icon info (`stateiconinfo`, `efstids`, img info) — decoded above |
| **spreditinfo** | 18 | per-job sprite draw-offset/attach tables |
| **navigation** | 17 | in-client map navigation data |
| **skilleffectinfo** | 8 | the skill→effect table (decoded → `skills/lua-skilleffectinfolist.md`) |
| **hateffectinfo** | 6 | headgear-triggered aura effects |
| **optioninfo / seekparty / worldviewdata / debugui / ridingspreditinfo / emotion / chatwndinfo / damageskin / quest / stylingshop / transparentitem / offsetitempos / …** | ~40 | UI, party-finder, riding sprite offsets, emotes, damage-number skins, quests, stylist, etc. |

## Reproduce / extend
Patched interpreters kept at `scratchpad/lua-5.0.3/bin/lua` and `scratchpad/lua-5.1.5/src/lua`;
generic dumper `scratchpad/lub_set/dump.lua` (`LUB=<file>.lub lua dump.lua`). To decode any
other `.lub`: `unzip -j gro.zip "<path>" && LUB=x.lub <lua5.1> dump.lua`. Notably the **228
`effecttool/*.lub`** define modern effects' construction (textures/layers/timing) and can be
decoded the same way on request.
