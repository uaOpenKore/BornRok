# `.str` effect inventory (all 2308 parsed)

Every `.str` skill/environment effect in `texture_x4.zip` parsed with a from-scratch **STRM
v148 (0x94)** reader (format from doc 14 §3): `signature "STRM", version, fps, maxKey,
layerNum, 16 reserved; per layer: texCount + texCount×128-byte texture names + frameCount +
frameCount× 124-byte keyframe`. Keyframe (31 dwords): `frame, type, pos(2f), uv[8], xy[8],
aniframe, anitype, delay, angle, color[4], srcAlpha, dstAlpha, mtPreset`.

**Result: 2308 `.str` parsed, 0 malformed** — the parser is exact for the whole content set.
All are **version 148, 60 fps**. **12 345** distinct layer textures across them. Blend combos
(`srcAlpha/dstAlpha` D3DBLEND, doc 14 §7) seen: **21** — dominant `5/7` (SRCALPHA/DESTALPHA)
and `0/0`, plus additive `2/2`, `2/6`, `5/6`, `5/9`, etc.

Full per-file inventory: [`data/str-inventory.tsv`](data/str-inventory.tsv) —
`str, version, fps, maxKey, layers, frames, textures, blend(src/dst)`.

Examples:
| .str | layers | frames | textures | blend |
|------|--------|--------|----------|-------|
| `defense.str` | 7 | 84 | pokjuk_c, ring_b, shield, thunder_pang | 0/0; 5/7 |
| `angel.str` | 37 | 816 | 8 | 0/0; 5/7 |
| `asum.str` (Assumptio) | 33 | 684 | … | 0/0; 2/1; 2/2; 5/7 |
| `stormgust.str` | 41 | 3693 | … | 0/0; 5/7 |
| `safetywall.str` | 13 | 413 | … | 0/0; 5/7 |

This is the per-effect **content** layer to sit under the per-skill effectId→`.str` mapping
(`effect-ids-from-exe.md` / doc 15) and the CEffect `.str` renderer (doc 14 §3). To dump a
single effect's exact **per-keyframe** timeline (pos/uv/scale/angle/rgba/blend per frame),
run the parser (`scratchpad/str_parse.py`) on that one file — it decodes every keyframe;
only the aggregate inventory is committed here to keep the repo lean.
