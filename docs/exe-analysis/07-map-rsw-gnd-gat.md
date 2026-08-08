# Subsystem 7 — World / map (RSW + GND + GAT + RSM) load & render

A map is a set of files sharing the map's base name under `data\`:
- **`.rsw`** — world descriptor: which ground/altitude files, water plane, global light
  (ambient/diffuse/direction), and the placed **objects** (RSM/RSM2 models, light
  sources, sound sources, particle **effects**).
- **`.gnd`** — ground mesh: cell grid, per-cell surfaces (up/front/right faces), texture
  atlas references, vertex colours and the baked **lightmap**.
- **`.gat`** — altitude/collision grid at ¼-cell resolution: per-cell height (4 corners)
  + a **type** flag (walkable / blocked / water / cliff / snipeable).
- **`.rsm` / `.rsm2`** — the 3D static models placed by the RSW (see the port's
  `rsm2-model-support` notes).

## Scene switching — `FUN_004f6ae0` @ `0x004f6ae0`

The mode/scene switch driver. Given a scene index (`this[0x16]`) and a map name
(copied to `this+2…`), it **destroys the current scene object** (`this[1]` vtable
`+0x8` destructor) and constructs the requested one:
- index `0` → **`FUN_0059f9c0`** (in-game world scene, vtable `PTR_FUN_0069f4dc`),
- index `1` → **`FUN_005582d0`** (another scene, e.g. login/char).
It then calls the new scene's `init(name)` / `enter` / `load` (`vtbl+0x4/+0x8/+0xC`).
The loop is guarded by the global quit flag `DAT_007735e0`.

## Async map request — `FUN_004f6c40` @ `0x004f6c40`

Requesting a map does **not** block: `FUN_004f6c40` merely copies the map name into the
scene object at `this+0x30` and sets a pending flag at `this+0x5c`. The scene's per-frame
update detects the flag and performs the actual RSW→GND→GAT→model load (behind a
"Загрузка…"-style gate), which is why map transitions show a loading state.

## Map-change entry — `FUN_005a49f0` @ `0x005a49f0`

On a server map-change it formats **`"%s.rsw"`** from the current map global
`DAT_00771730` and calls `FUN_004f6c40(1, "<map>.rsw")`, resets the camera/lighting, and
appends a line to **`PingLog.txt`** (`server`, `ip`, `map\%s.gat`, `time`) for diagnostics.

## Render order (per frame, in the world scene)

1. **Ground (GND)** — the cell mesh is drawn as textured triangles with the baked
   lightmap through the second texture stage (Subsystem 3); water cells get the animated
   water plane (scrolling/`waterfall*.tga`, wave height from RSW).
2. **RSM models** — the placed static meshes (buildings, props); nodes with rotation keys
   animate (see the port's `rsm-node-animation`, e.g. airship propellers).
3. **Sprites** — characters/monsters/NPCs/items as billboards (Subsystem 6), depth-sorted
   against the ground/models via the Z-buffer.
4. **Effects** — RSW particle effects (`EF_` ids: smoke/firefly/sparkle…, see
   `rsw-map-effects-id44.md`) and `.str` skill effects (Subsystem 8).
5. **GAT** is not drawn; it feeds pathfinding, click-to-walk cell picking, and
   snap/height of sprites.

## Lighting

The RSW global light (ambient + diffuse colour + sun direction) plus the GND lightmap
produce the map's look; the day/night cycle and dungeon light sources modulate the
per-vertex/per-sprite environment light. (See the port docs `outdoor-map-lighting`,
`godray-sun-shaft`.)

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| Scene switch | `0x004f6ae0` | destroy/construct scene |
| World scene ctor | `0x0059f9c0` | in-game scene (`PTR_FUN_0069f4dc`) |
| Alt scene ctor | `0x005582d0` | login/char scene |
| Async map request | `0x004f6c40` | queue map name `this+0x30`, flag `this+0x5c` |
| Server map-change | `0x005a49f0` | build `"%s.rsw"`, PingLog |
| Current-map global | `DAT_00771730` | map base name |

> NOTE: the low-level RSW/GND/GAT/RSM byte parsers are methods of the world-scene object
> reached through its vtable (`PTR_FUN_0069f4dc`); they are enumerated with the scene
> state machine in Subsystem 13. The on-disk formats themselves are the standard RO map
> formats already implemented in the BornRok port (`Rsw`/`Gnd`/`Gat`/`Rsm` parsers).
