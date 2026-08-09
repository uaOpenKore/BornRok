# Complete reverse — Effects, particles & the render system

Exhaustive algorithmic description of **how every special effect, particle, cloud, smoke
and skill visual is created, animated and drawn**, plus the underlying **Direct3D7 render
pipeline** they draw through. Every claim is anchored to a real exe address in
`winEXE/uaRO.exe` (decompile `winEXE/decomp/uaRO_decomp.c`).

Contents:
1. Architecture — the three effect classes + the spawn entry point
2. Effect class hierarchy & vtables
3. `.str` script effects (parser + keyframe interpolation + draw)
4. Procedural particle engine (emitter, update math, billboard render)
5. Coded-effect catalogue (every id → emit fn → texture)
6. Render core — the Direct3D7 device
7. Billboard/quad submission, blend & depth sorting
8. Special effect types & server triggers (ground circles, 3D-mesh, sound, RSW, packets)
9. Object memory layouts (reference)
10. Master address index

---

## 1. Architecture

Every visual effect is a C++ object anchored to an owner actor and linked into per-scene
and per-actor lists. There are **three cooperating classes** plus one spawn entry point.

| Role | Ctor | vtable | Size | Created by |
|------|------|--------|------|-----------|
| CEffect **base** (abstract billboard/animated primitive) | — | shared slots +0x10..+0x20 | — | — |
| **`.str` / coded controller** (the effect instance) | `FUN_005b98c0` @ `0x005b98c0` | `PTR_FUN_0069f9cc` | `0x11C3C` | `FUN_00549180` → `FUN_005c2630` |
| **Particle emitter** ("primitive") | `FUN_00610e80` @ `0x00610e80` | `PTR_FUN_0069fbc8` | `0xF9E4` | `FUN_005c3670` |
| *(unrelated)* 3D-mesh effect family | `FUN_005b2c30`/`FUN_005b70f0` | `0x0069f840`/`0x0069f8f8` | — | Granny path (§8) |

**Spawn entry — `FUN_00549180` @ `0x00549180`** `(this=ownerActor, effectId, offX, offZ, offY)`:
1. A whitelist `switch(effectId)` force-allows some ids even when effects are globally
   disabled (`DAT_00772d20`).
2. Rotates the offset by the **camera yaw** (`FUN_0040ae60`/`FUN_0040aea0` sin/cos) so
   offsets are screen-space.
3. `operator_new(0x11C3C)` + `FUN_005b98c0`, binds the effect via `FUN_005c2630`, and
   **links the object into two intrusive lists**: the global scene list
   (`FUN_004f6ad0()+0xC4 → +0xC`) and the **owner actor's** own list (`owner+0x104`,
   count `owner+0x108`) so the effect follows the actor.

The **controller** decides `.str` vs coded (see §2/§3); coded controllers spawn one or more
**particle emitters** via `FUN_005c3670` (§4).

---

## 2. Effect class hierarchy & vtables

### Particle-emitter vtable `PTR_FUN_0069fbc8` (9 slots)

| +off | addr | role |
|------|------|------|
| +0x00 | `0x00611270` | scalar-deleting destructor (real dtor `0x006112a0`) |
| +0x04 | `0x00611bf0` | **UPDATE/tick** — `switch(primitiveId@+0x184)` → per-type step; `++age@+0x194`; returns `age ≤ lifetime@+0x198` |
| +0x08 | `0x0061a030` | **message** — handles only `0x18`(stop) → `+0xF8=0` |
| +0x0C | `0x0061a050` | **RENDER** — `switch(primitiveId)` → per-type draw (gated `+0x8C≠0`) |
| +0x10 | `0x00544800` | *(base)* GetCameraDir → facing/direction index (wrap 2π, `__ftol`) |
| +0x14 | `0x00548150` | *(base)* stub |
| +0x18 | `0x00548160` | *(base)* ProjectToScreen → `+0x98/+0x9C` |
| +0x1C | `0x00545a10` | *(base)* SetScaleColor → `+0x44` scale, `+0x58` color |
| +0x20 | `0x00545030` | *(base)* AdvanceFrame → `+0x20/24/28`, loop/ping-pong `+0xE7`, `timeGetTime()` |

### `.str`/coded controller vtable `PTR_FUN_0069f9cc` (11 slots)

+0x00 dtor `0x005b99f0`; +0x04 update `0x005bd3d0`; **+0x08 message `0x005c22a0`**;
+0x0C render `0x005c25a0`; +0x10..+0x20 = **same base methods** as the emitter; +0x24
`0x005b9b70` (the coded-effect emit switch, §5); +0x28 `0x005ba930` (sibling render
dispatch). `FUN_005c2630` (the `.str` loader/init) is a method of this class.

### Message protocol — `vtbl+8` `(effect, sender, msgId, aptr, bptr, c)`

Controller handler `FUN_005c22a0`:

| msgId | action |
|-------|--------|
| `0x0E` set pos/target | `aptr`→pos `+0x13C/140/144`; `bptr`→target `+0x148/14C/150` |
| `0x18` stop | `+0xF4 = 0` (drop owner) |
| `0x19` clear | `aptr==0 & sender≠0` → unlink the child whose `node[2]==sender`, `--count(+0x168)`; `aptr≠0` → broadcast `0x18` to all children then free all |
| `0x2C` variant/preset | count `+0x10C` (≤0x32) or hard-coded position presets |
| `0x2E` | 4 floats `+0x118..124` |
| `0x50` | timing ptr `+0x114` |

Emitter handler `FUN_0061a030`: only `0x18` (stop → `+0xF8=0` → dies next update).
`0x7D` (init) is consumed by the *3D-mesh* family, not these.
**Closed lifecycle:** a dying emitter's dtor `FUN_006112a0` sends `0x19` (sender=self) to
its controller, which unlinks it.

### Lists & lifecycle

List node = 3 ints `operator_new(0xC)` `{next, prev, obj}`, circular doubly-linked. A
per-frame walker (template `FUN_004dbd80` @ `0x004dbd80`) calls `update` (`vtbl+4`) on each
node and reaps dead objects (`update` returns *alive*). Add: `FUN_005c3670` links to both
the controller child-list (`+0x164`) and the global renderer list. Remove-by-object:
`FUN_004dbd30` @ `0x004dbd30`.

---

## 3. `.str` script effects

`.str` (magic `STRM`, **version `0x94`**) is a layered 2D texture-keyframe animation.

| role | fn |
|------|----|
| effectId → `"*.str"` + load | `FUN_005c2630` @ `0x005c2630` |
| **header/layer parser** ("LoadAniClip") | `FUN_0042ca10` @ `0x0042ca10` (version gate: `!=0x94` → `"LoadAniClip: Version is incorrect"`) |
| texture-slot resolver (aniframe→texture) | `FUN_0042c850` @ `0x0042c850` |
| **update + keyframe interpolation** | `FUN_005c3870` @ `0x005c3870` |
| layer draw loop | `FUN_005c25a0` @ `0x005c25a0` |
| **quad build + submit** | `FUN_005c3cb0` @ `0x005c3cb0` |

### Format

**Header (28 B):** `0x00` magic `"STRM"`; `0x04` version(0x94); `0x08` **fps**(→`+0x114`);
`0x0C` maxKey(→`+0x110`); `0x10` **layerNum**(→`+0x118`); `0x14` 16 B reserved.
**Layer array:** base `res+0x120`, **stride `0x380`**, `layerNum` entries. Layer dwords:
`[0]` texcnt (= texture-anim wrap modulus); `[0x70..0xDD]` ≤110 texture-name pointers
(128-B arena slots, unused filled `"1.bmp"`); `[0xDE]` framecnt; `[0xDF]` → keyframe array.

**Keyframe (0x7C B, 31 dwords):** `[0]` framenum; `[1]` type (0=absolute, ≠0=additive
delta); `[2,3]` pos x,y; `[4..0x0B]` uv[8] (source-rect texels); `[0x0C..0x13]` xy[8] (quad
4 corner offsets); `[0x14]` aniframe (texture index); `[0x15]` anitype (0–5 cycling mode);
`[0x16]` delay/speed; `[0x17]` angle; `[0x18..0x1B]` color rgba; `[0x1C]` **srcalpha**
(D3D SRCBLEND enum); `[0x1D]` **destalpha** (DESTBLEND); `[0x1E]` mtpreset. Each layer keeps
a running **state buffer** of 29 floats (`0x74`) at `effect+0x7B4`.

### Update / interpolation — `FUN_005c3870`

Advances an integer frame clock (driven at `fps`). For each layer, when the clock reaches
the current keyframe's `framenum`:
- **absolute key** (`type==0`) → snapshot `kf[2..0x1E]` into the layer state;
- **additive key** (`type≠0`) → **accumulate deltas every frame**: `pos += `, `uv[] += `,
  `xy[] += `, `angle += `, `color += ` (linear tween); `aniframe` advances per `anitype`
  (1 add, 2 hold-at-end, 3 modulo-loop, 4 reverse-loop, 5 ping-pong; wrap = `texcnt`).
- `srcalpha/destalpha/mtpreset` are **discrete** (snapshot only, never interpolated).

The owner world position (`+0xF4`) is projected through the view matrix and offset by the
caller's screen offset — the factory passes **`argX = 0xC2A00000 = −80.0f`**, i.e. the
effect is **anchored 80 px above the owner (torso/body)**.

### Draw — `FUN_005c25a0` → `FUN_005c3cb0`

Per active layer: build 4 vertices (corners from `xy[]`, UV from `uv[]` with H/V flip),
resolve the texture `FUN_0042c850(layer, floor(aniframe))`, correct UV for pow-2 padding
(`realW/potW`), rotate the quad by `angle` about its center, place it at
`(state.pos − anchor + rotated_corner)*worldScale + effect.screen`, pack one ARGB colour on
all 4 verts, and append to the render batch with **`SRCBLEND=srcalpha`, `DESTBLEND=destalpha`**
via `FUN_00412020`. One textured triangle-pair per layer per frame; **camera-billboarded**.

**One-shot:** finish target `effect+0x41B8 = layerNum`; each layer past its last keyframe
sets `keyIndex=-1` and `++effect+0x41B4`; when `+0x41B4 ≥ +0x41B8` the update returns false
and the manager unlinks the effect. No global repeat — looping exists only *within* a layer
via `anitype` 3/4/5.

Example `effectId → .str`: `0x239 defense`, `0x173 angel`, `0x174 devil`, `0x13B SafetyWall`,
`0xC3 StoneCurse`, `0xA9 EnergyCoat`, `0x99 Concentration`, `0x1B8 asum`, `0x27B fire_dragon`,
`0x9D LevelUP`, `0x251..0x256 food_str/int/vit/agi/dex/luk`.

---

## 4. Procedural particle engine

Coded effects with no `.str` spawn **particle emitters** (`FUN_005c3670(primitiveId,x,y,z)`;
ctor `FUN_00610e80`; vtable `PTR_FUN_0069fbc8`). `primitiveId` is stored at `+0x184` and
selects a `(step, render)` pair. All types share one engine, parameterised per type.

The emitter object (0xF9E4 B) holds three working regions (allocated in the ctor):
- **`+0xF704`** — up to **4 particle "sources"**, stride **`0xB8` (184 B)** — the actual
  moving particles for ribbon/cloud/trail types.
- **`+0x1F04`** — the per-frame **triangle render batch**, 512 records × `0x6C` — geometry
  built each frame and flushed to D3D.
- **`+0x3B8`** — 50 secondary sub-primitive records × `0x8C`.

### The 0xB8 particle struct (`P = emitter+0xF704 + i*0xB8`)

| off | type | field |
|-----|------|-------|
| +0x00 | u8 | **active** (>0 alive) |
| +0x02 | s16 | ribbon-sample count (≤0x168) |
| +0x04 | f32 | base X / oscillation center |
| +0x08 | s32 | **age** (`+=1`/frame — the clock) |
| +0x0C | s16 | **rotation deg** (wrap `>0x167 → −0x168`) |
| +0x0E | s16 | **alpha / fade** (0…0xB4 = 180) |
| +0x6C | f32 | spiral radius/scale (`*= decay`) |
| +0x70 | u8 | subtype byte (0x01/0x02/0x0A/0x0B/'c') |
| +0x94 | f32×3 | **current world position** (seed = emitter pos) |
| +0xAC | f32×3 | velocity / 2nd endpoint (seed = pos − fixed offset) |

Angular motion uses two 360-entry LUTs: **cos `DAT_006ec09c`**, **sin `DAT_006ecbe4`**.

### Update (per-frame) — `FUN_00611bf0` → per-type step (e.g. `0x42` cloud `FUN_005e61a0`)

Motion is **parametric, not Euler**: position is recomputed each frame from `(age, angle)`
through the LUTs plus a saved base point — gravity/drift are baked into the seeded endpoint
and the geometric radius decay. Skeleton:
```
for i in 0..4:
  if P[+0x00] active:
    P[+0x08] age++;  P[+0x0C] angle += (i+3);  wrap 360
    // FADE (alpha at +0x0E):
    if age < lifetime-0x28:  if age < 0x14:  alpha += 10 (clamp 180)   // fast fade-in (~18f)
    else:                    alpha -= 5 (main) / 2 (top layer), floor 0 // slow fade-out (last 40f)
    // POSITION: oscillate about saved base via LUT
    P[+0x04] = baseX + cos[(age % 0x2D0)/2] * speed
```
The **emitter** dies when `emitter+0x194 (frame) > emitter+0x198 (lifetime)`; per-particle
death is via the `active` byte. **Fade is asymmetric-linear (fast in +10, slow out −5/−2),
no easing.** Size-over-life for subtype `0x0B` is triangular (`+1` while `age<0x29`, `−1`
after `age>0x8C`).

### Render (per-particle → billboard) — `FUN_0061a050` → per-type render

Each live particle expands into a **camera-projected ribbon/quad**. Corners are computed in
**world space** (`corner = pos + cos/sin[angle]*radius`), then perspective-projected to
screen by **`FUN_00412cb0`**:
```
w  = k / (m2*x + m5*y + m8*z + m11)          // perspective divide
sx = (m0*x+m3*y+m6*z+m9)*w*scaleX + screenCX ; sy = (...)*w*scaleY + screenCY ; rhw = w
```
producing **`D3DFVF_TLVERTEX`** (screen-space) vertices — so RO particles are *screen quads
whose corners are re-derived each frame*, which is why they always face the viewer.
Diffuse **ARGB**: `A` = the particle's `+0x0E` fade; `RGB` from a per-subtype palette
`switch` (cloud `0x42` hard-codes `E6,FF,E6`). The sprite-billboard variant `FUN_00547e20`
builds a screen-aligned rotated quad directly (`cos/sin` about center, UV `(0,0)(1,0)(0,1)
(1,1)` + ½-texel bias).

Each triangle record carries a **blend token** (`2` = alpha, `6` = additive) and is routed
by `FUN_00412020` into a depth-sorted bucket (sort key = particle z).

---

## 5. Coded-effect catalogue

The coded emit dispatch is **`FUN_005b9b70`** (`switchD_005b9b88`), indexed by
**`effectId − 300`** (so case `N` ⇒ effectId `N+300`). Each case spawns emitter(s) via
`FUN_005c3670(type,…)`, loads `effect\*.tga/bmp` via `FUN_0040c590`, optionally plays a
positional `.wav` via `FUN_004396b0`, and reads lifespan from **`DAT_006d9100[effectId]`**
(`0x05F5E0FF` ≈ ∞).

| effectId | emit fn | type | texture(s) | ms | notes |
|---|---|---|---|---|---|
| 307/322/323/324 | `FUN_005e08b0` | 0x42 | `cloud11.tga` (variants 0/1/2/10) | 36000 | **clouds** |
| 304 | `FUN_00600a00` | 0x39 | `Magic_Violet.tga` | 200 | teleport |
| 308 | `FUN_005e2950` | 0x35 | `…\item\돌.bmp` | 200 | stone |
| 309 | `FUN_005e24d0` | 0x3A | `pikapika2.bmp` | 299 | |
| 310/331/332 | `FUN_00602360`/`FUN_006025e0` | 0x3B | `thunder_center.bmp`/`white02.bmp` | 300 | |
| 312/320 | `FUN_00602f20` | 0x3C | `alpha_down.tga` | 100 | |
| 313/325 | `FUN_00604c50` | 0x3C | `ring_white.tga` | 100 | ring |
| 314 | `FUN_00603750` | 0x3C | `ring_purple.tga` | 200 | ring |
| 316/344 | `FUN_00605980`/`FUN_006070f0` | 0x3F/0x3C | `ring_blue.tga` | 200/100 | portal |
| 317/318/319 | `FUN_005fcca0`/`FUN_00605cf0` | 0x40/0x3C | `ring_red`/`alpha_down`/`magic_violet` | 9999 | |
| 321 | `FUN_00607500` | 0x41 | `ring_blue.tga` | ∞ | |
| 326/327 | `FUN_005fed20` | 0x43 | `foot_l_b`/`foot_r_b.tga` | 340 | footprints |
| **328** | `FUN_005db010` | 0x44 | `asura1..6`,`asura11..16.tga` | 1000 | **Asura Strike** (render `0x005db1b3`) |
| 329 | `FUN_00608ed0` | 0x45 | `cloud11.tga` | 100 | hit smoke |
| 336 | `FUN_00603b30` | 0x3D | `guardK/guardK2.tga` | 200 | block |
| 337 | `FUN_005dffa0` | 0x4B | `magic_green.tga` | 200 | |
| 339 | `FUN_0060f3a0` | 0x49 | `ring_yellow.tga` | 100 | Magnum Break |
| 341 | `FUN_00605cf0(3)` | 0x3E | `cloud11.tga` | 9999 | Pneuma-class |
| 342/343 | `FUN_005fbd40`/`FUN_005fbdd0` | 0x6E | `ring_red.tga` ×20 ground ring | 1000 | cast circle |
| 345/346 | `FUN_00604370`/`FUN_00604510` | 0x4C/0x4D | `wing003.bmp`/`blue_ivy.bmp` | ∞ | |
| 349–356 | `FUN_005e0bc0`+`FUN_005e1470` | 0x4F/0x50 | `waterfall11/12/13` or `31/32/33.tga` + `waterp1.tga` | ∞ | **waterfall/stream** |
| **365** | `FUN_005e20a0`+`FUN_005ec140` | 0x57/0x58 | `cross_old.bmp`+`explosive_1_128.bmp` | 300 | **Grand Cross** (render `0x005ba4e2`, +`alpha_center.tga`) |
| 366 | `FUN_005e2300` | 0x59 | `alpha_center.tga` ×5 | 200 | Sacrifice |
| 370 | `FUN_005fd800` | 0x2F | `cross_old`+`melody_a/b`+`spell_01..08.bmp` | 19999 | |

Not in the emit switch (reached via sibling render dispatch `FUN_005ba930`): **Soul Breaker**
`FUN_005db750` (`soul_s/o/u/l` + `soul_k/n/i/l.tga`), and `FUN_005db5e0` (`hanmoon1..7.tga`).
208 distinct `effect\*.tga/bmp` textures exist; Tarot/status-glyph/skybox/fog textures are
consumed by other coded renderers. (Full case-by-case table in the commit's agent notes.)

**Resolver** (`FUN_005c2630`): a `switch(effectId)` (nested byte tables `0x5C32AC/0x5C3414/
0x5C34DC` → jump tables `0x5C315C/0x5C3360/0x5C3490`) assigns a `.str` filename; the
**default** (`0x5C30F6`) means *coded* — and if `300 ≤ effectId ≤ 373` the visuals come from
`FUN_005b9b70` case `effectId−300`. Both paths share `DAT_006d9100[effectId]` lifespan.

---

## 6. Render core — Direct3D7 device

Rendering is a thin **DirectDraw7 + Direct3D7 immediate-mode** wrapper. Two objects: the
**DDraw wrapper** (owns `IDirectDraw7`, surfaces, `IDirect3DDevice7` at field `[0xF]`) and
the **render-context** (0x294 B, `FUN_0040cdb0`, reached everywhere as `param+0x7C`, holding
the per-frame draw-queue buckets). `FUN_00404ed0` sets `ctx[0x7C] = wrapper[0xF]`.

### Device vtable offsets (IDirect3DDevice7, confirmed by call sites)

`BeginScene=0x14 · EndScene=0x18 · Clear=0x28 · SetTransform=0x2C (1=WORLD,2=VIEW,3=PROJ) ·
SetViewport=0x34 · SetMaterial=0x40 · SetLight=0x48 · SetRenderState=0x50 ·
DrawPrimitive=0x64 (decimal 100!) · DrawIndexedPrimitive=0x68 · SetTexture=0x8C ·
SetTextureStageState=0x94 · LightEnable=0xB0`.

### Default render state (`FUN_00403f30`)

`CULLMODE=NONE`, `ZFUNC=LESSEQUAL`, `ALPHAFUNC=GREATEREQUAL`, `ALPHAREF=240`,
**`COLORKEYENABLE=1`** (color-key transparency), `ZENABLE=1`, `ZWRITEENABLE=1`,
`SRCBLEND=SRCALPHA`, `DESTBLEND=INVSRCALPHA`. Two texture stages: **stage 0** =
`MODULATE(TEXTURE, DIFFUSE)`, bilinear; **stage 1** = `MODULATEALPHA_ADDCOLOR` (lightmap),
border `0xFF000000`; stage 2 disabled.

### Vertex format & the frame

The client transforms on the **CPU** and submits **`D3DFVF_TLVERTEX` (`0x1C4`)** screen-space
vertices (`XYZRHW|DIFFUSE|SPECULAR|TEX1`); the multitexture/lightmap pass uses `0x2C4`
(+TEX2). `SetTransform` is used only for fog + identity WORLD/VIEW + an ortho PROJECTION.

Per-frame driver: `FUN_0040d890(1)` **clear** (`Clear` TARGET|ZBUFFER, `0xFF000000`, z=1.0) →
build scene → **`FUN_0040d8d0`** (`BeginScene` → `FUN_0040e4d0` draw-all → recycle →
`EndScene`) → **`FUN_0040fdb0`** present.

**Scene order (`FUN_0040e4d0`)** — sorted buckets: (1) fog/stage setup; (2) **opaque
ground/terrain** (Z-write on, alpha-test, no blend); (3) **3D models** (ambient, cull CW);
(4) **water / multitexture** (stage-1 add, FVF `0x2C4`); (5) **alpha-blended sprite/effect
buckets** in fixed order, incl. a **custom-blend bucket** where each entry carries its own
`SRCBLEND/DESTBLEND` at `entry+0x14/+0x18`, with **Z-write OFF** for the translucent/additive
passes; (6) **overlays** (`ZENABLE=0`, UI/text then additive). **Present** (`FUN_0040fdb0` →
`FUN_00404bc0`): windowed → `Blt` back→primary; fullscreen → `Flip`.

**Textures** (`FUN_0040c590`): name-keyed LRU cache; on miss decode (`FUN_005131f0`),
downscale by the mip/quality divisor `DAT_006e4bd8`, create a `0x128`-B texture object
(`PTR_FUN_006957c8`) and upload; missing → null texture `DAT_006ee290`.

---

## 7. Billboard submission, blend & depth sorting

- Every effect quad becomes one or more **0x6C-byte batch records** (`vertex0/1/2`, texture
  handle, primitive tag `5`, **blend token** `+0x6C`: `2` = alpha bucket, `6` = additive).
- `FUN_00412020` routes each record by the emitter's layer id (`+0x17C`) into one of several
  **z-sorted buckets**; the scene flush `FUN_0040e4d0` sorts each bucket by depth
  (`FUN_00415c50`) and issues `SetTexture` (only on change) + `Draw(Indexed)Primitive`.
- Blend equations at flush (via `SetRenderState` `0x13`/`0x14`): **alpha** = `SRCALPHA /
  INVSRCALPHA` (clouds, smoke, soft sprites); **additive** = `ONE / ONE` (glows, magic,
  bolts, auras); glow variant `ONE / SRCALPHA`. `ALPHABLENDENABLE(0x1B)` toggled per bucket;
  `ZENABLE(0x07)=1` (test on); **`ZWRITEENABLE(0x0E)=0` for the translucent/additive particle
  pass** — the exact reason overlapping particles blend instead of occluding (port memory
  `onTop-sprite-no-writez-particle-bleed`).
- Draw call: `FUN_0040bd90` — `DrawIndexedPrimitive`(dev `+0x68`) when indexed, else
  `DrawPrimitive`(dev `+0x64`), FVF `0x1C4`.

---

## 8. Special effect types & server triggers

### Ground / cylinder (flat cast circles)

The magic cast circle is a **coded** effect (rings `FUN_00604c50/…/FUN_0060f3a0`), laid down
by `FUN_005fbdd0` @ `0x005fbdd0` (called ~20× with hard-coded `(x,z)` offsets tracing a ring
on the horizontal plane; emitter type `0x6E`). **Orientation is a render-mode field**:
`*(emitter+0x17C)=5` marks **flat-on-ground** (world X/Z kept, fixed vertical), whereas the
billboard path `FUN_005c3cb0/FUN_005c3870` rebuilds the quad from the view matrix each frame
(camera-facing). These are the three `StrEffect::render` modes (flat / upright / default).

### 3D-mesh effects (Granny `.gr2`)

A **separate** subsystem: loader `FUN_004342b0` @ `0x004342b0` (`_GrannyReadEntireFile…`,
mesh→`+0x55`, skeleton→`+0x57`, bind pose). The **guild flag** is a gr2
(`model\3dmob\guildflag90_1.gr2`); 3D mobs via `FUN_00434530`. Not routed through the
2D effect factory.

### Positional sound in effects

`FUN_004396b0` @ `0x004396b0` `(name, x, y, z, vol, pan, pitch)` — resolves the wav
(`FUN_005131f0`), plays 2D (`FUN_00421af0`) or **3D positional** (`FUN_00421c20`). Fires on
the effect's **first frame** (`*(effect+0x110)==0`), positioned as `selfPos − cameraPos`.
`.str` effects can also carry embedded sound keyframes.

### RSW map effects (clouds, smoke, fireflies, sparkles)

Map-resident effects use the **same** spawner with EF_ ids at world positions:
`(*(scene+0x14))(0x21, 44, …)` = `EF_smoke`, `165 = EF_sparkle`, etc.
`FUN_00549180(EFid, x,y,z, angle)` places them; **persistent/looping** ones re-attach via
command **`0x6D`** (`(*(actor+8))(0, 0x6D, EFid, 0,0)`). Cloud/smoke visuals are the
`cloud1/2/4/11.tga` + `smoke.tga` emitters (§4/§5); the airship/sky maps add drifting cloud
billboards (port memory `airship-sky-clouds`).

### Packet triggers (`FUN_00579900`, 306 cases)

| packet | handler | effect | anchor |
|--------|---------|--------|--------|
| `0x1F3` ZC_NOTIFY_EFFECT2 | case 499 `0x316105` | effectId from packet → `FUN_00549180` | the bl actor (self if GID=`DAT_00771f7c`), single |
| `0x19B` ZC_NOTIFY_EFFECT | `FUN_00594c80` | type→effectId table (0→`0x173` base-up, 8→`0x151` job-up, 5→pharmacy…) | GID actor, self-centered |
| `0x8A` ZC_NOTIFY_ACT | `FUN_0057e300` | action-type→hit spark/blocked/crit + hit `.wav` | **target** (attacker variants) |
| skill packets | `FUN_00554…` region | `switch(skillId)`→effectId; ground skills pass the cell, buffs the actor | caster / ground / target |

Self vs target vs caster is decided by which GID the handler resolves (`FUN_004ebd10(GID)`
vs self `DAT_00771f7c`); that actor becomes `this` for `FUN_00549180`, so the effect is
linked into its `+0x104` list and follows it. `FUN_00549ae0` de-dupes and `0x6D` loops
persistent ground effects (Sanctuary/Pneuma).

---

## 9. Object memory layouts (reference)

**Controller (0x11C3C):** `+0x04/08/0C` pos · `+0xF4` owner · `+0xFC` procedural flag ·
`+0x100` effectId · `+0x10C` particle count · `+0x114` timing (`DAT_006d9100[id]`) ·
`+0x128` owner+0x38 copy · `+0x13C/140/144` spawn pos · `+0x148/14C/150` target ·
`+0x164/168` child list + count · `+0x7AC` .str handle · `+0x7B0` .str data · `+0x7B4`
per-layer state (0x74 stride) · `+0x41B4/41B8` loop counter / finish target · `+0x423C`
per-layer keyIndex.

**Emitter (0xF9E4):** `+0x00` vtable · `+0x04/08/0C` world pos · `+0x8C` render-enable ·
`+0x98/9C` screen XY · `+0xF8` parent controller · `+0x17C` render-mode/layer ·
`+0x184` primitiveId · `+0x194` frame · `+0x198` lifetime · `+0x340/344/348` anchor ·
`+0x3B8` 50×0x8C sub-pool · `+0x1F04` 512×0x6C triangle batch · `+0xF704` 4×0xB8 particles.

---

## 10. Master address index

| area | address |
|------|---------|
| spawn entry `FUN_00549180` | `0x00549180` |
| controller ctor / vtable | `0x005b98c0` / `PTR_FUN_0069f9cc` |
| effectId→.str/coded factory | `0x005c2630` |
| coded emit switch | `0x005b9b70` |
| controller message | `0x005c22a0` |
| .str parser (LoadAniClip) | `0x0042ca10` |
| .str update/interp | `0x005c3870` |
| .str draw layer | `0x005c25a0` / `0x005c3cb0` |
| emitter factory / ctor / vtable | `0x005c3670` / `0x00610e80` / `PTR_FUN_0069fbc8` |
| particle update / render | `0x00611bf0` / `0x0061a050` |
| particle spawn (cloud) | `0x005e08b0` |
| vertex transform (TL) | `0x00412cb0` |
| batch add (blend) | `0x00412020` |
| ground ring | `0x005fbdd0` |
| positional sound | `0x004396b0` |
| Granny gr2 loader | `0x004342b0` |
| effect timing table | `DAT_006d9100` |
| **render device init** | `0x00403f30` |
| DDraw/D3D create | `0x00404260` |
| scene draw-all | `0x0040e4d0` |
| draw call (Draw[Indexed]Primitive) | `0x0040bd90` |
| present | `0x0040fdb0` / `0x00404bc0` |
| texture cache | `0x0040c590` |
| packet dispatcher | `0x00579900` |
| 0x1F3 / 0x19B / 0x8A handlers | `0x00316105` / `0x00594c80` / `0x0057e300` |
| cos/sin LUTs | `0x006ec09c` / `0x006ecbe4` |

Related: `08-effect-engine.md`, `03-directdraw-render-backend.md`, `effect-ids-from-exe.md`,
`ragexe-effect-map.md`, `particle-engine-re.md`; memories `str-effect-orientation-scale`,
`onTop-sprite-no-writez-particle-bleed`, `airship-sky-clouds`, `never-invent-skill-effects`.
