# Subsystem 8 — Effect engine (CEffect + `.str`)

Visual effects (skill casts, buffs, level-up, warp, environment) are `CEffect` objects
that play a **`.str`** animation (a layered, keyframed texture animation) — plus a set of
hard-coded procedural effects (bolts, particles) and RSW map effects (Subsystem 7).

## CEffect factory / dispatch — `FUN_005c2630` @ `0x005c2630`

The `CEffect` initialiser. Parameters: `(this, owner, effectId, x, y, z)`.

- Stores `effectId` at `this+0x100`, owner at `this+0xf4`, owner transform at
  `this+4/8/0xc`, spawn position at `this+0x13c/0x140/0x144`.
- Looks up a **per-effect parameter record** from the table `&DAT_006d9100` indexed by
  `effectId` (`this+0x114 = DAT_006d9100[effectId]`) — this holds the effect's type /
  flags / timing.
- A **giant `switch(effectId)`** maps the id to a **`.str` file name**, e.g.
  `0x239 defense.str`, `0x151 JobLvUP.str`, `0x173 angel.str`, `0x174 devil.str`,
  `0x186 melt.str`, `0x187 cart.str`, `0x188 sword.str`, `0x1b8 asum.str`,
  `0x1ec ramadan.str`, … Cases without a `.str` fall through to procedural/built-in
  handlers (`switchD_…caseD_…`). The chosen name is loaded through the VFS (Subsystem 5).

This is the authoritative **effectId → asset** table for the client — see the pre-existing
`docs/effect-ids-from-exe.md` (extracted from this switch) and `docs/ragexe-effect-map.md`.

## `.str` format & rendering

`.str` (magic `STRM`, **version `0x94`** — the client rejects other versions) is a layered
2D texture-animation:
- header: fps, max-frame count, layer count, and a texture-name table;
- **layers[]**, each with **keyframes[]** carrying `{ position(x,y), uv/texture index,
  rgba (tint+alpha), rotation angle, scale(x,y), src/dst blend mode }`.
- Playback interpolates keyframes per frame; each layer is a **billboarded quad** drawn
  through the D3D device (Subsystem 3), usually **additive** (`src=ONE`,`dst=ONE`), tinted
  by the keyframe rgba. Effects are **one-shot**, body-anchored to the owner (≈ torso,
  ~-80px screen offset), camera-facing, with no scale multiplier — see the port notes
  `str-effect-orientation-scale`, `str-effect-version-0x94`, and the `EXE .str
  construction model` memory.

## Invocation channels (how the server triggers an effect)

Effects arrive over four distinct paths (see `docs`/memory `effect-invocation-server-channels`):
1. **`ZC_NOTIFY_EFFECT` 0x1F3** — an `EF_` id → CEffect factory directly.
2. **`ZC_NOTIFY_EFFECT2` / level-up 0x19B** — level/job-up, resurrection, etc.
3. **Skill packets** — skill cast/hit (`skillId` → effect, Subsystem 9/144 wiring).
4. **`ZC_NOTIFY_ACT` 0x8A** — action type (attack/sit/pick) drives combat visuals.

## Procedural / non-`.str` effects

Classic bolts (fire/cold/lightning), some auras and particle bursts are **generated in
code** (not `.str`) inside the fall-through switch cases — the port reproduces these as
procedural particle emitters (see `coded-skill-effects.md`, `never-invent-skill-effects`).

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| CEffect factory | `0x005c2630` | effectId → .str / procedural dispatch |
| Effect param table | `DAT_006d9100` | per-id record (`[id]`, type/timing) |

Related repo docs: `effect-ids-from-exe.md`, `ragexe-effect-map.md`,
`str-effect-orientation-scale`(memory), `str-effect-version-0x94`(memory),
`effect-invocation-server-channels`(memory). Deeper per-id decode continues under
Subsystem 9 (packet → effect) and the skill-wiring task #144.
