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
    o.write("}\n\n}  // namespace uaro\n")
print("%s: %d items" % (OUT, len(ids)))
