#!/usr/bin/env python3
# Regenerate src/resource/EmbeddedReference.cpp -- the data of the in-game Reference window (S. 2026-11-23, after
# uaRO HD's «Довідник»): monsters (level, HP, exp, element, race, size, ATK/DEF, drops with chances, MVP drops)
# and where each one is spawned, items (type, prices, weight, ATK/DEF, slots, equip level), all from the server.
# Follows npc/scripts_main.conf like the map-server. Usage (from the client root):
#   python3 tools/gen_reference_db.py /path/to/uAthena
import os, re, sys

root = sys.argv[1] if len(sys.argv) > 1 else "../uAthena"
OUT = "src/resource/EmbeddedReference.cpp"

def num(x):
    x = x.strip()
    try:
        return int(x, 0)
    except ValueError:
        return 0

def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

mobs = {}
for name in ("db/mob_db.txt", "db/mob_db2.txt"):
    p = os.path.join(root, name)
    if not os.path.exists(p):
        continue
    for line in open(p, encoding="utf-8", errors="replace"):
        s = line.split("//")[0].strip()
        f = s.split(",")
        if len(f) < 56 or not f[0].strip().isdigit():
            continue
        mid = int(f[0])
        drops = []
        for k in range(38, 56, 2):
            iid, rate = num(f[k]), num(f[k + 1])
            if iid > 0 and rate > 0:
                drops.append((iid, rate, 1 if k == 54 else 0))
        mvp = []
        for k in range(32, 38, 2):
            iid, rate = num(f[k]), num(f[k + 1])
            if iid > 0 and rate > 0:
                mvp.append((iid, rate, 2))
        mobs[mid] = dict(name=f[3].strip(), lv=num(f[4]), hp=num(f[5]), exp=num(f[7]), jexp=num(f[8]),
                         atk1=num(f[10]), atk2=num(f[11]), df=num(f[12]), mdf=num(f[13]), size=num(f[22]),
                         race=num(f[23]), ele=num(f[24]), mexp=num(f[30]), drops=mvp + drops)

items = {}
for name in ("db/item_db.txt", "db/item_db2.txt"):
    p = os.path.join(root, name)
    if not os.path.exists(p):
        continue
    for line in open(p, encoding="utf-8", errors="replace"):
        s = line.strip()
        if not s or s.startswith("//"):
            continue
        f = s.split(",", 19)
        if len(f) < 17 or not f[0].strip().isdigit():
            continue
        items[int(f[0])] = dict(name=f[2].strip(), type=num(f[3]), buy=num(f[4]), sell=num(f[5]),
                                w=num(f[6]), atk=num(f[7]), df=num(f[8]), slots=num(f[10]), elv=num(f[16]))

spawns = {}  # mob -> {map: [count, delay]}
seen = set()
def load_conf(path):
    if path in seen or not os.path.exists(os.path.join(root, path)):
        return
    seen.add(path)
    for line in open(os.path.join(root, path), encoding="utf-8", errors="replace"):
        s = line.strip()
        if s.startswith("//"):
            continue
        m = re.match(r"(npc|import)\s*:\s*(\S+)", s)
        if m:
            (load_conf if m.group(1) == "import" else load_npc)(m.group(2))
def load_npc(path):
    p = os.path.join(root, path)
    if not os.path.exists(p):
        return
    for line in open(p, encoding="utf-8", errors="replace"):
        if line.lstrip().startswith("//"):
            continue
        parts = [x for x in line.rstrip("\r\n").split("\t") if x != ""]
        if len(parts) < 4 or parts[1] not in ("monster", "boss_monster"):
            continue
        mp = parts[0].split(",")[0].replace(".gat", "").strip()
        f = parts[3].split(",")
        if not f[0].strip().isdigit():
            continue
        mob, cnt = int(f[0]), (num(f[1]) if len(f) > 1 else 1)
        d = num(f[2]) if len(f) > 2 else 0
        e = spawns.setdefault(mob, {}).setdefault(mp, [0, 0])
        e[0] += cnt
        e[1] = max(e[1], d)
load_conf("npc/scripts_main.conf")

mob_ids = sorted(mobs)
item_ids = sorted(items)
with open(OUT, "w", encoding="utf-8") as o:
    o.write("// Auto-generated from the server's db/ + npc/ (tools/gen_reference_db.py) -- do not edit.\n")
    o.write("// %d monsters (%d spawned), %d items.\n" % (len(mob_ids), len(spawns), len(item_ids)))
    o.write('#include "resource/EmbeddedReference.hpp"\n\nnamespace uaro::ref {\n\n')
    o.write("static const Drop kDrops[] = {\n")
    dofs = {}
    n = 0
    for mid in mob_ids:
        dofs[mid] = n
        for iid, rate, kind in mobs[mid]["drops"]:
            o.write("    {%d, %d, %d},\n" % (iid, rate, kind)); n += 1
    o.write("    {0, 0, 0}};\n\nstatic const Spawn kSpawns[] = {\n")
    sofs = {}
    n2 = 0
    for mid in mob_ids:
        sofs[mid] = n2
        for mp, (cnt, d) in sorted(spawns.get(mid, {}).items()):
            o.write("    {%s, %d, %d},\n" % (cstr(mp), cnt, d)); n2 += 1
    o.write('    {"", 0, 0}};\n\nstatic const Mob kMobs[] = {\n')
    for mid in mob_ids:
        m = mobs[mid]
        o.write("    {%d, %s, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d},\n" % (
            mid, cstr(m["name"]), m["lv"], m["hp"], m["exp"], m["jexp"], m["atk1"], m["atk2"], m["df"], m["mdf"],
            m["size"], m["race"], m["ele"], 1 if m["mexp"] > 0 else 0, dofs[mid], len(m["drops"]), sofs[mid],
            len(spawns.get(mid, {}))))
    o.write("};\n\nstatic const Item kItems[] = {\n")
    for iid in item_ids:
        it = items[iid]
        o.write("    {%d, %s, %d, %d, %d, %d, %d, %d, %d, %d},\n" % (iid, cstr(it["name"]), it["type"], it["buy"],
                it["sell"], it["w"], it["atk"], it["df"], it["slots"], it["elv"]))
    o.write("};\n\n")
    o.write("const Mob* mobs(std::size_t& n) { n = sizeof(kMobs) / sizeof(kMobs[0]); return kMobs; }\n")
    o.write("const Item* items(std::size_t& n) { n = sizeof(kItems) / sizeof(kItems[0]); return kItems; }\n")
    o.write("const Drop* drops() { return kDrops; }\n")
    o.write("const Spawn* spawns() { return kSpawns; }\n\n}  // namespace uaro::ref\n")
print("%s: %d monsters, %d spawned, %d items" % (OUT, len(mob_ids), len(spawns), len(item_ids)))
