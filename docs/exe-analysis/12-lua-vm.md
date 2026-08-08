# Subsystem 12 — Lua / `.lub` script VM

The client embeds a **Lua interpreter** (Lua 5.0/5.1, statically linked — `lua_getinfo`,
`lua_error`, `lua_trace`, `LUA_PATH` are recovered symbols). It serves two purposes: data
tables (`luafiles`) and the **homunculus / mercenary AI**.

## Data tables (`luafiles` / `.lub`)

Item/skill/monster metadata is stored as Lua tables compiled to `.lub` inside the GRF and
executed into the VM at startup, then read from C via `lua_getglobal` + field access.
Evidence: strings `itemInfo.m_location`, `dragitemInfo.m_itemType`, `MonsterSkillInfo.xml`,
and `LUA_PATH` configuration. (See the port's `grf-lub-files-survey` and
`lub-data-from-grf` — the BornRok port runs the same tables in an embedded Lua 5.1 VM for
tooltips, item names and mob-name fallback.)

## Homunculus & Mercenary AI

Each homunculus/mercenary owns its own AI VM, driven from the owner actor's per-frame
update (in the character update around `0x0055xxxx`):

- **Create** — `FUN_005a6ac0(type)` @ `0x005a6ac0` (0 = homun, 1 = merc): allocates the AI
  object (0x18 bytes) + a Lua state and registers the C callback API (`GetActors`, `GetV`,
  move/attack/skill commands — the `GetActors` string is present).
- **Load script** — `FUN_005a6b60(path)` @ `0x005a6b60` runs the AI Lua file:
  - Homunculus → `\AI\AI.lua`, or `\AI\USER_AI\AI.lua` when the user-AI flag
    `DAT_00772ecc` is set.
  - Mercenary → `\AI\AI_M.lua`, or `\AI\USER_AI\AI_M.lua` when `DAT_00772f68` is set.
  After loading it sends an **init event** via the actor vtable `(*vtbl+8)(0, 0x7D, type,
  0, 0)`.
- **Tick** — `FUN_005a6c60(id)` @ `0x005a6c60` calls the script's `AI(id)` each frame once
  the VM is live; the script senses via the callbacks and issues motion/skill commands
  (actual skill casts are validated server-side).
- **Errors** — Lua faults are logged through `AI_lua_error` / `AI_lua_trace...`
  (`FUN_00405bf0`, `0x00405bf0`).

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| AI VM create | `0x005a6ac0` | new Lua state + register C API |
| AI script load | `0x005a6b60` | run AI.lua / AI_M.lua |
| AI per-tick call | `0x005a6c60` | `AI(id)` each frame |
| Log helper | `0x00405bf0` | AI_lua_error/trace |
| user-AI flags | `DAT_00772ecc` / `DAT_00772f68` | homun / merc USER_AI |

> Port note: BornRok runs `data/ai/ai.lua` in an embedded **Lua 5.1** VM (memory
> `homun-ai-lua-vm`, task #148); `_FORTIFY_SOURCE=0` was needed for the Android build
> (memory `android-lua-fortify-crash`).
