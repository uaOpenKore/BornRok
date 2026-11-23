#!/usr/bin/env python3
# Regenerate src/resource/EmbeddedItemWeights.cpp from the server's db/item_db.txt + db/item_db2.txt (S. 2026-11-22):
# the server never sends an item's weight, and the quantity dialog for a move into the bag or the cart caps the
# amount at what the free weight carries. Usage (from Client/):
#   python3 tools/gen_item_weights.py /path/to/uAthena/db
# Weights are in the server's units (tenths: Red Potion = 70 = 7.0), the same as the 0xb0 weight / max weight.
# item_db2.txt overrides item_db.txt, as on the server.
import os, sys

src = sys.argv[1] if len(sys.argv) > 1 else "../db"
OUT = "src/resource/EmbeddedItemWeights.cpp"
weights = {}
elements = {}  # id -> element of an elemental weapon (bAtkEle) or armour (bDefEle), for the name colour
EMAP = {"Ele_Neutral": 0, "Ele_Water": 1, "Ele_Earth": 2, "Ele_Fire": 3, "Ele_Wind": 4, "Ele_Poison": 5,
        "Ele_Holy": 6, "Ele_Dark": 7, "Ele_Ghost": 8, "Ele_Undead": 9}
import re
for name in ("item_db.txt", "item_db2.txt"):
    path = os.path.join(src, name)
    if not os.path.exists(path):
        continue
    for line in open(path, encoding="utf-8", errors="replace"):
        s = line.strip()
        if not s or s.startswith("//"):
            continue
        f = s.split(",", 7)
        if len(f) < 7 or not f[0].strip().isdigit():
            continue
        w = f[6].strip()
        weights[int(f[0])] = int(w) if w.isdigit() else 0
        m = re.search(r"bonus\s+b(?:Atk|Def)Ele\s*,\s*(Ele_\w+)", s)
        if m and EMAP.get(m.group(1), 0) > 0:
            elements[int(f[0])] = EMAP[m.group(1)]
        else:
            elements.pop(int(f[0]), None)
ids = sorted(weights)
with open(OUT, "w", encoding="utf-8") as o:
    o.write("// Auto-generated from the server's db/item_db.txt + item_db2.txt (tools/gen_item_weights.py) -- do not edit.\n")
    o.write("// Item id -> weight in tenths (the server's units). %d items.\n" % len(ids))
    o.write("#include <algorithm>\n#include <cstdint>\n\nnamespace uaro {\n\n")
    o.write("static const std::uint32_t kItemWeightIds[] = {\n")
    for i in range(0, len(ids), 12):
        o.write("    " + ", ".join(str(x) for x in ids[i:i + 12]) + ",\n")
    o.write("};\nstatic const std::uint16_t kItemWeights[] = {\n")
    for i in range(0, len(ids), 16):
        o.write("    " + ", ".join(str(min(weights[x], 65535)) for x in ids[i:i + 16]) + ",\n")
    o.write("};\n\n")
    o.write("// The weight of an item (tenths), or -1 when the table does not know it.\n")
    o.write("int embedded_item_weight(std::uint32_t nameid) {\n")
    o.write("    const std::uint32_t* end = kItemWeightIds + sizeof(kItemWeightIds) / sizeof(kItemWeightIds[0]);\n")
    o.write("    const std::uint32_t* it = std::lower_bound(kItemWeightIds, end, nameid);\n")
    o.write("    if (it == end || *it != nameid) return -1;\n")
    o.write("    return kItemWeights[it - kItemWeightIds];\n")
    o.write("}\n\n")
    eids = sorted(elements)
    o.write("// Elemental weapons / armours (bAtkEle / bDefEle): id -> element 1..9 (Water..Undead). %d items.\n" % len(eids))
    o.write("static const std::uint32_t kItemElemIds[] = {\n")
    for i in range(0, len(eids), 12):
        o.write("    " + ", ".join(str(x) for x in eids[i:i + 12]) + ",\n")
    o.write("};\nstatic const std::uint8_t kItemElems[] = {\n")
    for i in range(0, len(eids), 24):
        o.write("    " + ", ".join(str(elements[x]) for x in eids[i:i + 24]) + ",\n")
    o.write("};\n\n")
    o.write("// The element of an elemental weapon/armour (1 Water .. 9 Undead), or 0.\n")
    o.write("int embedded_item_element(std::uint32_t nameid) {\n")
    o.write("    const std::uint32_t* end = kItemElemIds + sizeof(kItemElemIds) / sizeof(kItemElemIds[0]);\n")
    o.write("    const std::uint32_t* it = std::lower_bound(kItemElemIds, end, nameid);\n")
    o.write("    if (it == end || *it != nameid) return 0;\n")
    o.write("    return kItemElems[it - kItemElemIds];\n")
    o.write("}\n\n}  // namespace uaro\n")
print("%s: %d items, %d elemental" % (OUT, len(ids), len(elements)))
