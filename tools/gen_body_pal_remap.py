#!/usr/bin/env python3
# Regenerate src/resource/BodyPalRemapTable.cpp: per-body palette-index remaps so the clothes-dye palettes
# (data/palette/몸, made for the uaRO body sprites) give BornRok's bodies the colours uaRO.exe shows.
# BornRok's 1k/4k sprite.zip carries a newer, RE-INDEXED copy of those bodies (same art, palette order
# changed), so a dye palette applied as-is recolours skin and outline too. For every body below that
# differs from the uaRO original, the frames of both versions are paired (same size, same silhouette) and
# every pixel votes "sprite.zip index -> uaRO index"; the majority wins. The client then dyes with
# eff[i] = dye[map[i]] (CharacterActor::load, resource/BodyPalRemap.hpp).
#
# Run from the client root (Python 3 standard library only):
#   python3 tools/gen_body_pal_remap.py <BornRok sprite.zip> <uaRO GRF or zip holding data/sprite/인간족/몸통>
# Re-run whenever sprite.zip's player bodies change: each map carries a fingerprint of the sprite.zip
# body's own palette, and a body whose fingerprint no longer matches simply gets no remap.
import struct
import sys
import zipfile
import zlib
from collections import Counter, defaultdict

OUT = "src/resource/BodyPalRemapTable.cpp"

# The body sprites CharacterActor draws for this server's player classes (jobSpriteName + ridingBodyName).
BODIES = [
    "초보자", "검사", "마법사", "궁수", "성직자", "상인", "도둑", "기사", "프리스트", "위저드", "제철공", "헌터",
    "어세신", "크루세이더", "몽크", "세이지", "로그", "연금술사", "바드", "무희", "슈퍼노비스", "건너", "닌자", "결혼",
    "산타", "여름", "태권소년", "권성", "권성융합", "소울링커", "로드나이트", "하이프리", "하이위저드", "화이트스미스",
    "스나이퍼", "어쌔신크로스", "팔라딘", "챔피온", "프로페서", "스토커", "크리에이터", "클라운", "집시",
    "페코페코_기사", "신페코크루세이더", "로드페코", "페코팔라딘",
]
SEXES = [("남", True), ("여", False)]


class GrfReader:
    """GRF 0x200 (unencrypted) -> {normalized vpath: bytes} on demand."""

    def __init__(self, path):
        self.f = open(path, "rb")
        hdr = self.f.read(46)
        table_off, _seed, _n, ver = struct.unpack_from("<IIII", hdr, 0x1E)
        if ver != 0x200:
            raise SystemExit(f"{path}: GRF version {ver:#x} unsupported")
        self.f.seek(46 + table_off)
        csz, _usz = struct.unpack("<II", self.f.read(8))
        tbl = zlib.decompress(self.f.read(csz))
        self.ents = {}
        p = 0
        while p < len(tbl):
            e = tbl.index(b"\0", p)
            name = tbl[p:e].decode("cp949", "replace").replace("\\", "/").lower()
            _cs, csa, us, flags, off = struct.unpack_from("<IIIBI", tbl, e + 1)
            self.ents[name] = (csa, us, flags, off)
            p = e + 18

    def read(self, vpath):
        e = self.ents.get(vpath.lower())
        if not e:
            return None
        csa, us, flags, off = e
        if flags & 6:
            raise SystemExit(f"{vpath}: encrypted GRF entry unsupported")
        self.f.seek(46 + off)
        return zlib.decompress(self.f.read(csa))[:us]


class ZipReader:
    """ZIP with UTF-8 (bit 11) or CP949 names, rooted at data/ or at a data subfolder (sprite/...)."""

    def __init__(self, path):
        self.z = zipfile.ZipFile(path)
        self.ents = {}
        for info in self.z.infolist():
            name = info.filename
            if not info.flag_bits & 0x800:
                try:
                    name = name.encode("cp437").decode("cp949")
                except (UnicodeEncodeError, UnicodeDecodeError):
                    pass
            name = name.replace("\\", "/").lower()
            if not name.startswith("data/"):
                name = "data/" + name
            self.ents[name] = info

    def read(self, vpath):
        info = self.ents.get(vpath.lower())
        return self.z.read(info) if info else None


def open_archive(path):
    with open(path, "rb") as f:
        magic = f.read(15)
    return GrfReader(path) if magic == b"Master of Magic" else ZipReader(path)


def parse_spr(b):
    """Indexed frames [(w, h, bytes)] + embedded palette [(r, g, b)] of a .spr (v1.x/2.0 raw, 2.1 RLE)."""
    if b[:2] != b"SP":
        raise ValueError("not a .spr")
    ver, nidx = struct.unpack_from("<HH", b, 2)
    p = 8 if ver >= 0x200 else 6
    frames = []
    for _ in range(nidx):
        w, h = struct.unpack_from("<HH", b, p)
        p += 4
        if ver >= 0x201:
            (sz,) = struct.unpack_from("<H", b, p)
            rle = b[p + 2:p + 2 + sz]
            p += 2 + sz
            out = bytearray()
            i = 0
            while i < len(rle) and len(out) < w * h:
                if rle[i] == 0:
                    out += b"\0" * (rle[i + 1] if i + 1 < len(rle) else 0)
                    i += 2
                else:
                    out.append(rle[i])
                    i += 1
            px = bytes(out[:w * h]).ljust(w * h, b"\0")
        else:
            px = b[p:p + w * h]
            p += w * h
        frames.append((w, h, px))
    pal = [tuple(b[len(b) - 1024 + i * 4:len(b) - 1024 + i * 4 + 3]) for i in range(256)]
    return frames, pal


def fingerprint(spr_bytes):
    """FNV-1a 64 over the R, G, B bytes of the embedded palette (resource/BodyPalRemap.hpp)."""
    h = 0xCBF29CE484222325
    pal = spr_bytes[-1024:]
    for i in range(256):
        for v in pal[i * 4:i * 4 + 3]:
            h = ((h ^ v) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


OPAQUE = bytes([0] + [1] * 255)


def pair_frames(new, old):
    """(new frame, old frame) pairs: same size and at least 90% of the silhouette in common. The same frame
    index first; otherwise the best same-size old frame (frames were inserted in some newer bodies)."""
    by_size = defaultdict(list)
    for j, (w, h, _px) in enumerate(old):
        by_size[(w, h)].append(j)
    pairs, unpaired = [], 0
    for i, (w, h, px) in enumerate(new):
        if not px:
            continue
        mask = px.translate(OPAQUE)
        best = None
        for j in sorted(by_size.get((w, h), ()), key=lambda j: abs(i - j)):
            omask = old[j][2].translate(OPAQUE)
            share = 1.0 if omask == mask else sum(a == b for a, b in zip(mask, omask)) / len(mask)
            if best is None or share > best[0]:
                best = (share, j)
            if share == 1.0:
                break
        if best and best[0] >= 0.9:
            pairs.append((i, best[1]))
        else:
            unpaired += 1
    return pairs, unpaired


def build_map(new_frames, new_pal, old_frames, old_pal):
    pairs, unpaired = pair_frames(new_frames, old_frames)
    votes = defaultdict(Counter)
    for i, j in pairs:
        for (a, b), n in Counter(zip(new_frames[i][2], old_frames[j][2])).items():
            if a and b:
                votes[a][b] += n

    def dist(a, b):
        return sum((x - y) ** 2 for x, y in zip(new_pal[a], old_pal[b]))

    used_new = set()
    for _w, _h, px in new_frames:
        used_new.update(px)
    used_old = set()
    for _w, _h, px in old_frames:
        used_old.update(px)
    used_old.discard(0)
    m = list(range(256))
    fallback = 0
    for a in range(1, 256):
        if votes[a]:
            top = max(votes[a].values())
            m[a] = min((b for b, n in votes[a].items() if n == top), key=lambda b: (dist(a, b), b))
        elif a in used_new and used_old:
            m[a] = min(used_old, key=lambda b: (dist(a, b), b))  # drawn, never paired: nearest uaRO colour
            fallback += 1
    total = sum(sum(c.values()) for c in votes.values())
    agree = sum(c[m[a]] for a, c in votes.items())
    return m, pairs, unpaired, (agree / total if total else 1.0), fallback


def cpp_string(s):
    return '"' + "".join(f"\\{c:03o}" if c > 0x7E or c < 0x20 or chr(c) in '"\\' else chr(c)
                         for c in s.encode("cp949")) + '"'


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__ or "usage: gen_body_pal_remap.py <sprite.zip> <uaRO grf/zip>")
    sprite_zip, uaro = open_archive(sys.argv[1]), open_archive(sys.argv[2])
    rows = []
    for body in BODIES:
        for sex, male in SEXES:
            vpath = f"data/sprite/인간족/몸통/{sex}/{body}_{sex}.spr"
            nb, ob = sprite_zip.read(vpath), uaro.read(vpath)
            if nb is None or ob is None:
                continue
            if nb == ob:
                print(f"{body}_{sex}: identical, no map")
                continue
            nf, npal = parse_spr(nb)
            of, opal = parse_spr(ob)
            m, pairs, unpaired, agree, fallback = build_map(nf, npal, of, opal)
            print(f"{body}_{sex}: frames {len(nf)}/{len(of)} paired {len(pairs)} unpaired {unpaired} "
                  f"agree {agree * 100:.2f}% fallback-indices {fallback}")
            rows.append((body, sex, male, fingerprint(nb), m))
    with open(OUT, "w", encoding="utf-8", newline="\n") as o:
        o.write("// Auto-generated by tools/gen_body_pal_remap.py from BornRok's sprite.zip and the uaRO GRF -- do not\n")
        o.write("// edit. Per body sprite + sex: sprite.zip palette index -> uaRO body palette index, so a clothes-dye\n")
        o.write("// palette (made for the uaRO bodies) colours BornRok's re-indexed bodies like uaRO.exe does.\n")
        o.write('#include "resource/BodyPalRemap.hpp"\n\nnamespace uaro {\n\n')
        o.write("const BodyPalRemap kBodyPalRemaps[] = {\n")
        for body, sex, male, fp, m in rows:
            o.write(f"    {{{cpp_string(body)}, {'true' if male else 'false'}, 0x{fp:016x}ULL,  // {body}_{sex}\n")
            for k in range(0, 256, 32):
                o.write("     " + ("{" if k == 0 else " ") + ",".join(str(v) for v in m[k:k + 32]) +
                        ("}},\n" if k == 224 else ",\n"))
        o.write("};\n\n")
        o.write("const usize kBodyPalRemapCount = sizeof(kBodyPalRemaps) / sizeof(kBodyPalRemaps[0]);\n\n")
        o.write("}  // namespace uaro\n")
    print(f"{OUT}: {len(rows)} maps ({len(rows) * 256} bytes of map data)")


if __name__ == "__main__":
    main()
