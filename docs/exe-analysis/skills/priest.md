# Priest

> Per-skill effect dossiers reversed from `winEXE/uaRO.exe`. See [`README.md`](README.md) for the shared conventions and field meanings.

## Priest — skill effect dossiers

### Reverse-engineering notes (apply to all entries)

- These sixteen `FUN_005d*` routines are **per-frame effect-update callbacks** stored in the CEffect vtable/dispatch (mirrored by the big switch at `FUN_005bd4cf`). They run every frame with `param_1` = the effect/emitter struct; `param_1+0x110` = **current frame counter**, `param_1+4 / +0xc` = effect world X/Z.
- **Common anchor call** (last line of almost every fn): `FUN_005c3870(0, 0xc2a00000, 0)` — `0xc2a00000 = -80.0f` → the effect billboard is **torso-anchored** (−80px up the owner), camera-billboarded, and this call's bool return drives lifespan (returns 0 → despawn). Lifespan itself is table-driven off `DAT_006d9100[effectId]`.
- **Common sound call**: `FUN_004396b0(name, dx, 0, dz, 0xfa, 0x28, 0x3f800000)` = play wav at owner-relative offset `dx/dz` (owner minus self-camera-target from `FUN_004f6ad0()→+0xc4→+0x3c`), **vol 250, pan 40, pitch 1.0**.
- The **actual sprite/quad geometry** for the sound-only entries is the effect's registered `.str`/animation (loaded by the spawner `FUN_00549180(effectId,…)`); these callbacks only sequence **sound + torso anchor**. Entries that additionally call `FUN_005c3670(type,x,y,z)` build **procedural particles** in-code — those are detailed fully below.
- effectId shown is the dispatch case that routes to the callback.

---

### Sanctuary — `FUN_005d8020` (effectId 0x53 / 83)
- **Sound**: `effect\priest_sanctuary.wav` at **frame 0** only (owner-relative, vol250).
- **Geometry**: no in-code particles; visual = registered Sanctuary `.str`/ground animation (this is a ground-AoE skill — the ground-quad marker is set by its spawner, not here).
- **Anchor**: torso (−80f) billboard via `FUN_005c3870`.
- **Sequence/timing**: one-shot; sound gates on frame==0, anchor every frame, lifespan from `DAT_006d9100[0x53]`.
- **Blend/colour**: default (external `.str`).

### Magnus Exorcismus — `FUN_005d92c0` (effectId 0x71 / 113)
- **Sound**: `effect\priest_magnus.wav` at **frame 0** (owner-relative).
- **Geometry**: none in-code; external Magnus Exorcismus `.str` (ground AoE, spawner-owned).
- **Anchor**: torso (−80f).
- **Sequence**: one-shot, frame-0 sound + per-frame anchor.
- **Blend**: external.

### B.S. Sacramenti (Benedictio) — `FUN_005d8ac0` (effectId 0x5b / 91)
- **Sound**: `effect\priest_benedictio.wav` at **frame 0**.
- **Geometry**: none in-code; external Benedictio `.str`.
- **Anchor**: torso (−80f).
- **Sequence**: one-shot, frame-0 sound.
- **Blend**: external.

### Kyrie Eleison — `FUN_005d9200` (effectId 0x70 / 112)
- **Sounds** (two, sequenced): `effect\priest_kyrie_eleison_b.wav` at **frame 0**; `effect\priest_kyrie_eleison_a.wav` at **frame 0x0F (15)**. Both owner-relative, vol250. (Note: a separate `effect\kyrie_guard.wav` exists for the shield-block hit, played elsewhere.)
- **Geometry**: none in-code; external Kyrie shield `.str` (torso barrier sprite).
- **Anchor**: torso (−80f).
- **Sequence**: one-shot with a 2-stage audio cue (spawn at f0, "seal" at f15).
- **Blend**: external.

### Gloria — `FUN_005d7e60` (effectId 0x4b / 75)
- **Sound**: `effect\priest_gloria.wav` at **frame 0**.
- **Geometry**: none in-code; external Gloria buff `.str`.
- **Anchor**: torso (−80f).
- **Sequence**: one-shot, frame-0 sound.
- **Blend**: external.

### Magnificat — `FUN_005d7ed0` (effectId 0x4c / 76)
- **Sound**: `effect\priest_magnificat.wav` at **frame 0**.
- **Geometry**: none in-code; external Magnificat buff `.str`.
- **Anchor**: torso (−80f).
- **Sequence**: one-shot.
- **Blend**: external.

### Impositio Manus — `FUN_005d85c0` (effectId 0x54 / 84)
- **Sound**: `effect\priest_impositio.wav` at **frame 0x3C (60)** — note the sound fires *late* (~60 frames in), timed to the laying-on-hands climax, not spawn.
- **Geometry**: none in-code; external Impositio `.str`.
- **Anchor**: torso (−80f).
- **Sequence**: one-shot, single delayed audio hit at frame 60.
- **Blend**: external.

### Suffragium — `FUN_005d8630` (effectId 0x58 / 88)
- **Sound**: `effect\priest_suffragium.wav` at **frame 0**.
- **Geometry**: none in-code; external Suffragium `.str`.
- **Anchor**: torso (−80f).
- **Sequence**: one-shot.
- **Blend**: external.

### Lex Aeterna — `FUN_005d8840` (effectId 0x55 / 85)
- **Sound**: `effect\priest_lexaeterna.wav` at **frame 0x0F (15)** (delayed, timed to the mark landing on target).
- **Geometry**: none in-code; external Lex Aeterna `.str` (marks target).
- **Anchor**: torso (−80f) — anchored on the effect owner (target).
- **Sequence**: one-shot, single audio hit at frame 15.
- **Blend**: external.

### Lex Divina — `FUN_005d86a0` (effectId 0x57 / 87)
- **Sound**: `effect\priest_lexdivina.wav` played **5 times** at frames **0, 0x14 (20), 0x23 (35), 0x3C (60), 0x46 (70)** — a repeating "chant/toll" cadence across the cast.
- **Geometry**: none in-code; external Lex Divina `.str` (silence seal on target).
- **Anchor**: torso (−80f).
- **Sequence**: one-shot animation with 5 evenly-spaced audio pulses; lifespan `DAT_006d9100[0x57]`.
- **Blend**: external.

### Aspersio — `FUN_005d88b0` (effectId 0x56 / 86) — *procedural particles*
- **Sound**: `effect\priest_aspersio.wav` at **frame 0x46 (70)**.
- **Geometry**: **procedural particle burst** via `FUN_005c3670(7, 0,0,0)` — particle **type 7**, emitted on **every 5th frame while 0x3C < frame < 0x8C** (i.e. frames 65,70,…,135 → ~14 emissions). Each particle:
  - texture/attrib block `0x198 = 0x28` (40), scale `0x2cc = 0x3fc00000` (1.5f), growth `0x270 = 0.19f`, `0x274 = 0.01f`;
  - random azimuth `0x290 = rand()%0x168` (0–359°), `0x29c = 0x3f99999a` (1.2f);
  - world-space position (`0x358/0x35c/0x360`) computed from camera basis × scale (screen-projected placement around owner);
  - `0x1a4 = 0x1e` (30-frame life); registered via `FUN_005481c0(&DAT_006dbf2c)` / `FUN_00548230(&DAT_006dbf14)` / `FUN_006113a0()` (holy-water sparkle textures).
- **Anchor**: torso (−80f) for the base sprite; particles are placed around the owner via camera projection.
- **Sequence**: `param_1+0x114` set to 400 (max-life guard); when `frame == param_1+0x114` returns 0 → despawn. One-shot; a stream of ~14 short-lived (30f) sparkle motes over frames 65–135 plus a wav at f70.
- **Blend/colour**: additive-style holy sparkles (the `alpha`-mapped particle textures).

### Recovery — `FUN_005d7fb0` (effectId 0x4e / 78)
- **Sound**: `effect\priest_recovery.wav` at **frame 0**.
- **Geometry**: none in-code; external Recovery `.str` (status-cure flash on target).
- **Anchor**: torso (−80f).
- **Sequence**: one-shot.
- **Blend**: external.

### Resurrection — `FUN_005d7f40` (effectId 0x4d / 77)
- **Sound**: `effect\priest_resurrection.wav` at **frame 0**.
- **Geometry**: none in-code; external Resurrection `.str` (rising light column on target).
- **Anchor**: torso (−80f).
- **Sequence**: one-shot.
- **Blend**: external.

### Cure — `FUN_005d7c50` (effectId 0x42 / 66)
- **Sound**: `effect\Acolyte_cure.wav` at **frame 0** (note: Acolyte-namespaced wav).
- **Geometry**: none in-code; external Cure `.str`.
- **Anchor**: torso (−80f).
- **Sequence**: one-shot.
- **Blend**: external.

### Angelus — `FUN_005d77a0` (effectId 0x29 / 41)
- **Sound**: `effect\EF_Angelus.wav` at **frame 0**.
- **Geometry**: none in-code; external Angelus `.str` (halo/wings buff).
- **Anchor**: torso (−80f).
- **Sequence**: one-shot.
- **Blend**: external.

### Signum Crucis — `FUN_005d7810` (effectId 0x28 / 40) — *procedural particles*
- **Sounds** (both at **frame 0x1E / 30**): `effect\EF_Signum.wav` **and** `effect\EF_Bash.wav` (layered impact).
- **Per-frame billboard math**: every frame it re-projects the owner's world pos (`iVar3+4/8/0xc`) through the camera matrix (`FUN_00412cb0`, colour `0xff000000`) and writes screen coords to `param_1+0x130/0x134` (keeps the cross billboard camera-locked).
- **Geometry (fires once at frame 30)** — two sub-emitter groups:
  1. **Central column**: `FUN_005c3670(4, 0,0,0)` — particle **type 4**; attrib `0x198=0x28`, flag `|1` set at `+0x190`, vertical offset `0x35c = 0xc2200000` (−40f), scale `0x2cc = 0x42c80000` (100f), field `0x258 = 0x432a0000` (170f) → `0x2ac = 170·const`; life `0x1a4 = size − size/3`; texture **`effect\alpha_down.tga`** via `FUN_0040c590` (single big central darkening/cross shaft).
  2. **14 rising motes** (`do…while` counter starts 0x14=20 but decrements from 0x14 with the inner loop iterating 14× effectively): loop body `FUN_005c3670(3, x,0,z)` — particle **type 3**; per-particle: attrib `0x198=0x23` (35), y-offset `0x35c=−40f`, random azimuth `0x290 = rand()%0x168`, radial speed `0x2b4 = (rand()%0x3c+10)·const` with decay `0x2c0`, spin `0x20c = (rand()%0x19+0xf)·const`, `0x2e4 = rand()%0x3c+0x1e`, `0x2e8 = (rand()%0x23+0x5f)·const`, height field `0x258=0x43480000` (200f); texture **`EFFECT\alpha_center.tga`**.
- **Anchor**: caster/target torso (−80f) base; the type-4 column and type-3 motes are body-anchored around the owner.
- **Sequence/timing**: one-shot; nothing until **frame 30**, where the dual wav + full particle construction fire simultaneously; particles then live out their own `0x1a4` lifespans; base despawns on the anchor-bool.
- **Blend/colour**: `alpha_down.tga` (downward-fading cross shaft) + `alpha_center.tga` (radial motes), alpha-mapped holy particles.

---

### Slow Poison — `FUN_0058ba60` (**not a per-effect callback**)
- **What it actually is**: this is the large **status-change / skill-completion packet dispatcher** (handles packets 0xF9/0x201C/0x1DE/0x14E/0x14F/0x11A-class, status-icon add/remove at `+0x1c`/`+0x28`, and the giant `switch(skillId)` that routes each skill to a visual via `FUN_00549180(effectId,…)`). It is *shared by all skills*, not owned by Slow Poison.
- **Slow Poison specifically**: routes through the status path (add/clear status icon via the actor vtable) and has **no dedicated in-code particle construction and no `.str` spawn** in this function — it is a status buff (green ailment-immunity icon), so its only client feedback is the status-icon toggle, not a CEffect. The visually-rich cases in this dispatcher (e.g. Stone Curse `case 0x10`→`FUN_00549180(0x17)` + `stonecurse.wav`, and the many `FUN_00549180(effectId)` cases) belong to *other* skills, not Slow Poison.
- **Recommendation**: treat Slow Poison as icon-only; do not synthesize a `.str`/particle for it. If a buff shimmer is desired it must come from a real source, not this dispatcher.

**Cross-reference table (effectId → skill → callback):**

| effectId | skill | callback | in-code particles |
|---|---|---|---|
| 0x28 (40) | Signum Crucis | `FUN_005d7810` | yes (type4 + 14×type3) |
| 0x29 (41) | Angelus | `FUN_005d77a0` | no |
| 0x42 (66) | Cure | `FUN_005d7c50` | no |
| 0x4b (75) | Gloria | `FUN_005d7e60` | no |
| 0x4c (76) | Magnificat | `FUN_005d7ed0` | no |
| 0x4d (77) | Resurrection | `FUN_005d7f40` | no |
| 0x4e (78) | Recovery | `FUN_005d7fb0` | no |
| 0x53 (83) | Sanctuary | `FUN_005d8020` | no (ground `.str`) |
| 0x54 (84) | Impositio Manus | `FUN_005d85c0` | no |
| 0x55 (85) | Lex Aeterna | `FUN_005d8840` | no |
| 0x56 (86) | Aspersio | `FUN_005d88b0` | yes (type7 sparkle stream) |
| 0x57 (87) | Lex Divina | `FUN_005d86a0` | no (5× audio) |
| 0x58 (88) | Suffragium | `FUN_005d8630` | no |
| 0x5b (91) | B.S. Sacramenti | `FUN_005d8ac0` | no |
| 0x70 (112) | Kyrie Eleison | `FUN_005d9200` | no (2× audio) |
| 0x71 (113) | Magnus Exorcismus | `FUN_005d92c0` | no (ground `.str`) |

Source: `/root/BornRok/winEXE/decomp/uaRO_decomp.c` (callbacks at the listed addresses; effectId dispatch switch at `FUN_005bd4cf`, ~line 364490–365090).
