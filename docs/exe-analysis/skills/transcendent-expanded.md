# Transcendent & expanded classes

> Per-skill effect dossiers reversed from `winEXE/uaRO.exe`. See [`README.md`](README.md) for the shared conventions and field meanings.

## Transcendent & expanded classes — skill effect dossiers

**Architectural finding (read this first).** uaRO.exe has a hardcoded effect engine that only covers **classic 1st/2nd-job skills**. Two master tables drive it, both keyed by the network `effectId`:

- **Visual builder dispatcher `FUN_005ba930` @0x5ba930** — `switch(*(obj+0x100) - 0x1eb)` over an internal *resource-type* enum (base 0x1eb, ~0xba/186 entries). Calls the specialized builders below (`FUN_005db750`, `FUN_005db010`, texture-strip loaders). Ceiling case is `0xba` at line 364163; everything above falls through to `return 1` / generic path.
- **`.str` effect-file resolver `FUN_005c2630` @0x5c2630** (line 367038) — `switch(effectId)` returning a baked `.str` filename string (loaded via `FUN_0040c590`). All cases are `< 0x100` (classic ids). The full baked `.str` table is ~150 files (list below).
- **Sound dispatcher `FUN_005bd3d0` @0x5bd3d0** (line 364183) groups the transcendent EF ids (`0x18d, 0x1a2, 0x1b4–0x1b6, 0x1b9, 0x1c6, 0x1d3–0x1d9, 0x1e8–0x1ea, 0x1f7/8/a, 0x20b–0x218, 0x24e, 0x25b, 0x275`, lines 364220-364257) into **one silent/generic case** — i.e. no dedicated audio or visual.

**Consequence:** Napalm Vulcan, Spiral Pierce, Head Crush, Joint Beat, Aura Blade, Gravitation, Ganbantein, Gospel/Battle Chant, Martyr's Reckoning, Pressure, Sacrifice, Rapid Smiting, Meteor Assault, Create/Enchant Deadly Poison, Falcon Assault, Sharp Shooting, True Sight, Wind Walk, Falcon Eyes, Assumptio, Basilica, Meditatio, Cart Termination, Throw Spirit Sphere, Chain Crush Combo, Palm Strike, Tiger Fist, Critical Explosion, Fury, and all Gunslinger/Ninja/TK/SG/SL/Super-Novice damage skills have **no dedicated C++ builder and no baked `.str` binding** in this exe. They are server-driven (base-class `effectId` reuse) or fall to the generic path. The only expanded-class-specific hardcoded art is the **status-icon kanji resolver** and the **Soul Link letter effect**, documented below.

The dossiers below cover every target skill that *does* resolve to real hardcoded engine code (mostly via a reused base-2nd-job builder), then the exhaustive "not found" list.

---

### Soul Linker — "SOUL / LINK" letter cast — `FUN_005db750` @0x5db750 (called from builder case `0xb`, line 362992, via `FUN_005db750(0)`)
- **Sound:** none in this fn (the `EF_BeginSpell.wav` at line 362960 plays from the enclosing `case 0xb`, frame 0).
- **Textures (sequence):** loaded via `FUN_0040c590`, 4-texture strip (`+0x1b8 = 4`):
  - sub-state `+0x110 == 1`: `soul_l`, `soul_u`, `soul_o`, `soul_s` → spells **"SOUL"** (lines 378826-378833).
  - sub-state `+0x110 == 0x15`: `soul_k`, `soul_n`, `soul_i`, `soul_l` → spells **"LINK"** (lines 378889-378896).
- **Geometry:** particle emitter `FUN_005c3670(0x44,0,0,0)`, ground-quad flag `+0x17c = 5`; 4 per-instance letter billboards on the `+0xf704` strip (0xb8 stride), each flagged `+0xf778 = 2`.
- **Anchor:** caster (position copied from owner `+0xf4` → `+4/8/0xc`).
- **Sequence/timing:** one-shot; lifespan `+0x198 = *(owner+0x114)`. "SOUL": x-offsets `-60,-40,-20,0` (`local_8` −0x3c step +0x14), height `+0xf718` = `local_c` −4 step −6, size `+0xf770 = 5.0` (0x40a00000). "LINK": x-offsets from `-0x78` step +0x14, height from `+0x16` step −6, same size.
- **Blend/colour:** white letter glyphs (per `soul_*` tga), billboarded, additive.

### Assassin Cross — Enchant (Deadly) Poison motes — `FUN_005d43b0` @0x5d43b0 (builder case `0x14`/`0x7d`, line 364390)
- **Sound:** frame 0 (`+0x110 == 0`): `assasin_enchantpoison.wav` (375058) **and** `EF_PoisonAttack.wav` (375061), both at torso-relative pos, vol 0xfa.
- **Textures:** particle type `FUN_005c3670(5,...)` (type-5 = poison mote sprite); colour tables set via `FUN_005481c0(&DAT_006dc950)` / `FUN_00548230(&DAT_006dc938)` (green).
- **Geometry:** billboard poison motes; one spawned every 5th frame (`+0x110 % 5 == 0`).
- **Anchor:** caster.
- **Timing:** looped emission over the buff cast; per-mote lifespan `+0x198 = 0x28` (40); random spherical launch dir (angle `rand%0x168`, speed `rand%6+2`), gravity `+0x2ac`, fade-in start `+0x1a4 = lifespan·4/5`.
- **Blend/colour:** green, alpha-blended rising motes. (Create/Enchant *Deadly* Poison sends the same effectId → reuses this; `.str` fallbacks `Invenom_str`/`poison_str`/`VenomDust_str` exist for the poison overlay.)

### Whitesmith — Maximum Overthrust / Maximum Power Thrust — `FUN_005d4850` @0x5d4850 (builder case, line 365131 `FUN_005d4850()`)
- **Sound:** frame 0: `black_overthrust.wav` (375184); then a second `EF_StoneCurse.wav` (375208) when the burst spawns.
- **Textures:** `alpha_center.tga` (375225) on a single-texture strip.
- **Geometry:** projected screen-space colour via `FUN_00412cb0` (owner colour → `+0x130/0x134`); burst emitter `FUN_005c3670(4,...)` (type-4).
- **Anchor:** caster torso.
- **Timing:** one-shot at frame 0; sets buff lifespan `+0x114 = 9999`; emitter lifespan `+0x198 = 0x28`, rises (`+0x35c = -20.0`), scale `+0x2d0 = 7.0` growing (`+0x2d4`), `+0x20c/+0x278 = 10.0`.
- **Blend/colour:** additive white central flare. Also note `.str` fallbacks: `MaximizePower_str`/`maximize_min_str` (effectId 0x68), `WeaponPerfection_str` (0x67), `RepairWeapon_str` (0x65), `melt_str` (Melt Down) all resolve through `FUN_005c2630`.

### High Wizard — Napalm Vulcan (base = Napalm Beat) — `FUN_005cc300` @0x5cc300 (builder case, line 364475 `FUN_005cc300()`)
- **Sound:** frame 0 only (`+0x110 == 0`): `EF_NapalmBeat.wav` (371707) at target-relative pos, vol 0xfa.
- **Textures:** 8-frame numbered strip built by `FUN_006713a8(buf, &DAT_006dc45c, n)` (template `napalm%d`) → `FUN_0040c590` (371743-371745), `+0x1b8 = 8`.
- **Geometry:** billboard cluster `FUN_005c3670(0,0,0,0)`, animated `+0x19c = 2`.
- **Anchor:** target (colour-projected via `FUN_00412cb0` → `+0x130/0x134`).
- **Timing:** fires for sub-frames `+0x110 < 0xf`; lifespan `+0x198 = 0x1e` (30); per-shard random radial velocity `+0x358/+0x35c = cos/sin(rand%0x168)·(rand%0x32+10)`; size `+0x2d8/+0x2e4 = rand%0x28+0x14`; fade start `+0x1a4 = 2/3·lifespan`.
- **Blend/colour:** additive purple-white napalm shards (Napalm Vulcan repeats this per hit).

### Assassin Cross — Venom Splasher (Meteor Assault neighbourhood) — `FUN_005d9480` @0x5d9480 (builder case, line 365143)
- **Sound:** sub-state `+0x110 == 10`: `assasin_venomsplasher.wav` (377668).
- **Geometry/anchor:** only sets torso anchor `FUN_005c3870(0,-80.0,0)` — the visible burst is the `.str` file `VenomSplasher_str` (effectId 0x81 via `FUN_005c2630`, line 367556). Companion: `PoisonReact_str`/`PoisonReact_1st_str` (0x7f, via `FUN_005d9330` @0x5d9330, sound `assasin_poisonreact.wav`). Meteor Assault itself: **not found** (generic).

### Lord Knight — Concentration / (Sniper True Sight shares sound) — `FUN_005d96c0` @0x5d96c0 (builder case, line 365233)
- **Sound:** frame 0: `ac_concentration.wav` (377744).
- **Geometry/anchor:** torso anchor only `FUN_005c3870(0,-80.0,0)`; visible aura = `.str` `Concentration_str` (effectId 0x99, `FUN_005c2630` line 367614). (`ac_concentration.wav` is reused widely: lines 330170, 365478/488, 377890/912/945 — Improve Concentration, True Sight, etc.)

### MVP / generic hero flash — `FUN_005d7be0` @0x5d7be0 (builder case, line 364716)
- **Sound:** frame 0: `st_mvp.wav` (376719). Torso anchor only; visual = `.str` `Mvp_str` (0xf-range).

### Paladin — Shield Boomerang / Shield Chain / Rapid Smiting projectile — `FUN_005e1e00` @0x5e1e00 (spinning) and `FUN_005e1b60` @0x5e1b60 (charge) — called line 363104 / 365630 with `shield_boomerang.bmp`
- **Sound:** `cru_shield_boomerang.wav` — `FUN_005e1e00` fires on sub-states 0/3/6/9/0xc (382122); `FUN_005e1b60` fires on frame 0 (382050).
- **Texture:** single `shield_boomerang.bmp` (arg `param_2`, loaded 382131 / 382058) on a ribbon/quad strip; `shield.bmp` variant loaded at `FUN_005ff700(shield.bmp,5.0)` line 362840.
- **Geometry — `FUN_005e1e00`:** emitter type `FUN_005c3670(0x74,...)`, ground flag `+0x17c = 5`; spinning billboard projectile — random spin angle `+0xf710 = rand%0x168`, spin rate `+0xf71c = 2.5` (0x40200000), radius `+0xf712 = 0x19`, size `+0xf770 = 5.0`; travels along an arc offset by the `&DAT_006ecbe4`/`&DAT_006ec09c` sin/cos LUTs.
- **Geometry — `FUN_005e1b60`:** emitter type `FUN_005c3670(0x2a,...)`, ground flag 5; a **straight charge** — computes caster→target distance `sqrt(dx²+dy²)` into strip length `+0xf708` (382069), aims toward target world coords `+0x13c/0x140/0x144`, spin rate `+0xf71c = 2.0`, size `+0xf770 = 7.0`, alpha `+0xf712 = 0xff`.
- **Anchor:** caster origin → travels to target.
- **Timing:** one-shot per throw; lifespan `+0x198 = *(owner+0x114)`.
- **Blend/colour:** opaque shield bitmap, spinning. Shield Chain and Rapid Smiting reuse this projectile plus `.str` `shield_charge_str` (effectId 0xf6) / `holy_cross_str` (0xf5) / `providence_str` (0xf8) / `devotion_str` (0xfb).

### Expanded classes — status-icon "kanji" overlay resolver — `FUN_~0x648c00` (switch, lines 448950-449120)
Not a particle builder — a `switch(status_id)` returning the overlay `.tga` drawn above the actor. This is the only hardcoded rendering for most TK/SG/SL/Ninja/GS buffs:
- **TaeKwon kicks:** `i_STORMKICK` (0x22), `i_DOWNKICK` (0x23), `i_TURNKICK` (0x24), `i_COUNTER` (0x25), `i_DODGE` (0x26), `i_RUN` (0x27).
- **Soul Linker element endows:** `i_p_DARK` (0x28), `i_p_TELE` (0x29), `i_p_WIND`/`WATER`/`FIRE`/`EARTH`/`SAINT` (element-property, gated on serverver `FUN_0063b080` 0xfcd..0xfd2, cases 0x10/0x11), `i_KAIZEL` (0x2a), `i_KAAHI` (0x2b), `i_KAUPE` (0x2c), `i_ONEHAND` (0x2d).
- **Star Gladiator comforts:** `i_SUNCOMFORT` (0x2e), `i_MOONCOMFORT` (0x2f), `i_STARCOMFORT` (0x30).
- **Ninja / misc:** `i_shrink` (0x36), `i_sightblaster` (0x37), `i_closeconfine` (0x38), `i_MADNESS` (0x39), `i_FEVER` (0x3a), `i_MAEMI` (0x3b), `i_BUNSIN` (0x3c), `i_NEN` (0x3d).
- **Gunslinger:** `i_ADJUSTMENT` (0x3e), `i_ACCURACY` (0x3f).
- Geometry: single overlay quad, actor-anchored, drawn by the status-icon renderer (data-driven from this string map).

---

### Not found (data-driven / generic-path / `.str`-only)

No dedicated builder **and** no dedicated wav/tga; not in the `FUN_005c2630` `.str` table (fall to the generic/high-id group in `FUN_005bd3d0`):

- **High Wizard:** Magic Amplify, Gravitation Field, Ganbantein (Napalm Vulcan → reuses Napalm Beat builder above).
- **Lord Knight:** Spiral Pierce, Head Crush, Joint Beat, Aura Blade, Berserk/Frenzy (Concentration → reuses builder above; `SpearBoomerang_str`/`SpearStab_str`/`Pierce_str`/`TwoHand_str` exist as base-Knight `.str` only).
- **Paladin:** Gospel/Battle Chant, Martyr's Reckoning, Pressure, Sacrifice (Devotion, Shield Chain, Rapid Smiting → reuse Shield-Boomerang builder + `devotion_str`/`shield_charge_str`/`providence_str`).
- **Assassin Cross:** Meteor Assault, Soul Destroyer *(covered elsewhere)* (Create/Enchant Deadly Poison → reuse Enchant-Poison builder above).
- **Sniper:** Falcon Assault, Sharp Shooting, Wind Walk, Falcon Eyes (True Sight → shares `ac_concentration.wav`/`Concentration_str`).
- **High Priest:** Assumptio, Basilica, Meditatio.
- **Whitesmith:** Cart Termination (Melt Down → `melt_str`; Maximum Power Thrust → reuse Overthrust builder; Weapon Perfection/Maximize Power/Repair Weapon → `.str`).
- **Champion:** Throw Spirit Sphere, Chain Crush Combo, Palm Strike, Tiger Fist, Zen/Critical Explosion, Fury (Asura Strike `FUN_005db010` *covered*).
- **Gunslinger:** Desperado, Tracking, Rapid Shower, Full Buster, etc. (only `i_ADJUSTMENT`/`i_ACCURACY` status icons hardcoded).
- **Ninja:** all attack skills — Huuma Shuriken, Kunai Throw, Flip Tatami, Cast Ninja Stone, Jupiter Thunder (only `i_MAEMI`/`i_BUNSIN`/`i_NEN`/`i_shrink` icons).
- **Taekwon / Star Gladiator / Soul Linker:** all kicks/warmth/link damage (Soul Link letters `FUN_005db750` + comfort/kick/element status icons hardcoded; everything else generic).
- **Super Novice:** no dedicated effects.

Baked `.str` files present in the exe (via `FUN_005c2630`) that *are* transcendent-adjacent and would render if the server sends the classic effectId: `devotion`, `shield_charge`, `providence`, `holy_cross`, `Lord`, `MaximizePower`/`maximize_min`, `WeaponPerfection`/`_min`, `RepairWeapon`, `melt`, `Concentration`, `EnergyCoat`, `VenomSplasher`, `VenomDust`, `PoisonReact`/`_1st`, `Invenom`, `enc_earth/fire/ice/wind`, `SpearBoomerang`, `SpearStab`, `homing`, `providence`.

**Key addresses:** builder dispatcher `FUN_005ba930` @0x5ba930 (switch base 0x1eb); `.str` resolver `FUN_005c2630` @0x5c2630; sound dispatcher `FUN_005bd3d0` @0x5bd3d0; status-icon kanji map @~0x648c00. Source: `/root/BornRok/winEXE/decomp/uaRO_decomp.c`.
