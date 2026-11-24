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

STAT = {"bStr": "STR", "bAgi": "AGI", "bVit": "VIT", "bInt": "INT", "bDex": "DEX", "bLuk": "LUK", "bAllStats": "All stats",
        "bMaxHP": "Max HP", "bMaxSP": "Max SP", "bMaxHPrate": "Max HP %", "bMaxSPrate": "Max SP %", "bAtk": "ATK",
        "bDef": "DEF", "bMdef": "MDEF", "bHit": "HIT", "bFlee": "FLEE", "bFlee2": "Perfect Dodge", "bCritical": "CRIT",
        "bAspdRate": "ASPD %", "bMatkRate": "MATK %", "bAtkRate": "ATK %", "bSpeedRate": "Move speed %",
        "bHPrecovRate": "HP recovery %", "bSPrecovRate": "SP recovery %", "bCastrate": "Cast time %",
        "bUseSPrate": "SP cost %", "bDelayRate": "Skill delay %", "bBaseAtk": "ATK", "bMatk": "MATK"}
def amount(x):
    x = x.strip()
    m = re.match(r"rand\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", x)
    return "%s-%s" % (m.group(1), m.group(2)) if m else x
def args2(x):
    d = 0
    for i, c in enumerate(x):
        if c == "(": d += 1
        elif c == ")": d -= 1
        elif c == "," and d == 0: return x[:i], x[i + 1:]
    return x, "0"
def summary(script):
    out = []
    for st in [t.strip() for t in script.split(";") if t.strip()]:
        m = re.match(r"itemheal\s+(.+)$", st)
        if m:
            a1, a2 = args2(m.group(1))
            hp, sp = amount(a1), amount(a2)
            if hp not in ("0", ""): out.append("Restores %s HP" % hp)
            if sp not in ("0", ""): out.append("Restores %s SP" % sp)
            continue
        m = re.match(r"percentheal\s+(.+)$", st)
        if m:
            a1, a2 = args2(m.group(1))
            if a1.strip() != "0": out.append("Restores %s%% HP" % a1.strip())
            if a2.strip() != "0": out.append("Restores %s%% SP" % a2.strip())
            continue
        m = re.match(r"bonus\s+(\w+)\s*,\s*(-?\w+)$", st)
        if m and m.group(1) in STAT:
            v = m.group(2)
            out.append("%s %s%s" % (STAT[m.group(1)], "+" if not v.startswith("-") else "", v))
            continue
        m = re.match(r"sc_start\s+SC_(\w+)\s*,\s*(\d+)", st)
        if m:
            out.append("Effect: %s (%d s)" % (m.group(1).replace("_", " ").title(), int(m.group(2)) // 1000))
            continue
        if st.startswith(("if", "else", "set ", "end", "{", "}", "specialeffect", "misceffect")) or len(st) > 70:
            continue
        out.append(st)
        if len(out) >= 8:
            break
    return "; ".join(out)[:300]
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
        sc = ""
        if len(f) > 19:
            m = re.match(r"\s*\{(.*?)\}", f[19])
            if m: sc = summary(m.group(1))
        items[int(f[0])] = dict(name=f[2].strip(), type=num(f[3]), buy=num(f[4]), sell=num(f[5]),
                                w=num(f[6]), atk=num(f[7]), df=num(f[8]), slots=num(f[10]), elv=num(f[16]), sc=sc)

spawns = {}  # mob -> {map: [count, delay]}
npcs = []     # (map, name, x, y, kind 0 npc / 1 shop)
warps = []    # (map, x, y, to, tx, ty)
shop_items = {}  # item id -> "npc name (map x,y)" of the first shop selling it
def npc_name(n):
    n = n.split("::")[0]
    return n.split("#")[0].strip()
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
        if len(parts) >= 4 and parts[1] == "warp":
            a, b = parts[0].split(","), parts[3].split(",")
            if len(a) >= 3 and len(b) >= 5:
                warps.append((a[0].replace(".gat", "").strip(), num(a[1]), num(a[2]), b[2].replace(".gat", "").strip(), num(b[3]), num(b[4])))
            continue
        if len(parts) >= 4 and (parts[1] in ("script", "shop", "cashshop") or parts[1].startswith("duplicate(")):
            a = parts[0].split(",")
            spr = parts[3].split(",")[0].strip()
            if parts[1] in ("shop", "cashshop"):
                for tok in parts[3].split(",")[1:]:
                    iid = tok.split(":")[0].strip()
                    if iid.isdigit() and int(iid) not in shop_items:
                        shop_items[int(iid)] = (npc_name(parts[2]) + " (a script's shop)") if a[0].strip() == "-" else \
                            "%s (%s %s,%s)" % (npc_name(parts[2]), a[0].replace(".gat", "").strip(), a[1].strip() if len(a) > 1 else "", a[2].strip() if len(a) > 2 else "")
            if len(a) >= 3 and a[0] != "-" and spr.lstrip("-").isdigit() and int(spr) not in (-1, 111, 139):
                nm = npc_name(parts[2])
                if nm:
                    npcs.append((a[0].replace(".gat", "").strip(), nm, num(a[1]), num(a[2]), 0 if parts[1] in ("script",) or parts[1].startswith("duplicate(") else 1))
            continue
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

# element table: attr_fix.txt, 4 levels x 10 x 10 (rows = attack element, columns = defense element)
attr = []
p = os.path.join(root, "db/attr_fix.txt")
if os.path.exists(p):
    for line in open(p, encoding="utf-8", errors="replace"):
        s2 = line.split("//")[0].strip()
        if not s2 or "\t" in line.split("//")[0] and re.match(r"^\d+,\d+", s2):
            continue
        vals = [v for v in s2.replace(" ", "").split(",") if v != ""]
        if len(vals) == 10 and all(v.lstrip("-").isdigit() for v in vals):
            attr.extend(int(v) for v in vals)
attr = (attr + [100] * 400)[:400]
npcs.sort()
warps.sort()
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
        o.write("    {%d, %s, %d, %d, %d, %d, %d, %d, %d, %d, %s},\n" % (iid, cstr(it["name"]), it["type"], it["buy"],
                it["sell"], it["w"], it["atk"], it["df"], it["slots"], it["elv"], cstr(it["sc"])))
    o.write("};\n\n")
    o.write("static const Npc kNpcs[] = {\n")
    for mp, nm, x, y, k in npcs:
        o.write("    {%s, %s, %d, %d, %d},\n" % (cstr(mp), cstr(nm), x, y, k))
    o.write('    {"", "", 0, 0, 0}};\nstatic const Warp kWarps[] = {\n')
    for mp, x, y, to, tx, ty in warps:
        o.write("    {%s, %d, %d, %s, %d, %d},\n" % (cstr(mp), x, y, cstr(to), tx, ty))
    o.write('    {"", 0, 0, "", 0, 0}};\nstatic const short kAttr[400] = {\n')
    for i in range(0, 400, 20):
        o.write("    " + ", ".join(str(v) for v in attr[i:i + 20]) + ",\n")
    o.write("};\n\n")
    o.write("static const ShopItem kShopItems[] = {\n")
    for iid in sorted(shop_items):
        o.write("    {%d, %s},\n" % (iid, cstr(shop_items[iid])))
    o.write('    {0, ""}};\n')
    o.write("const ShopItem* shopItems(std::size_t& n) { n = sizeof(kShopItems) / sizeof(kShopItems[0]) - 1; return kShopItems; }\n")
    o.write("const Npc* npcs(std::size_t& n) { n = sizeof(kNpcs) / sizeof(kNpcs[0]) - 1; return kNpcs; }\n")
    o.write("const Warp* warps(std::size_t& n) { n = sizeof(kWarps) / sizeof(kWarps[0]) - 1; return kWarps; }\n")
    o.write("int attrFix(int level, int atk, int def) { return kAttr[((level - 1) * 10 + atk) * 10 + def]; }\n")
    o.write("const Mob* mobs(std::size_t& n) { n = sizeof(kMobs) / sizeof(kMobs[0]); return kMobs; }\n")
    o.write("const Item* items(std::size_t& n) { n = sizeof(kItems) / sizeof(kItems[0]); return kItems; }\n")
    o.write("const Drop* drops() { return kDrops; }\n")
    o.write("const Spawn* spawns() { return kSpawns; }\n\n}  // namespace uaro::ref\n")
print("%s: %d monsters, %d spawned, %d items, %d npcs, %d warps, %d shop items" % (OUT, len(mob_ids), len(spawns), len(item_ids), len(npcs), len(warps), len(shop_items)))
