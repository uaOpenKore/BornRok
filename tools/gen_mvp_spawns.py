#!/usr/bin/env python3
# Regenerate src/resource/EmbeddedMvpSpawns.cpp from the server (S. 2026-11-23, MVP timers after uaRO HD):
# every SPAWNED monster with MVP experience (mob_db MEXP > 0) and its respawn delay. On the server a boss comes back
# exactly its spawn line's delay1 after it died (no window), so the client can count down from a death it saw.
# Follows the npc: / import: lines from npc/scripts_main.conf like the map-server. Usage (from the client root):
#   python3 tools/gen_mvp_spawns.py /path/to/uAthena
import os, re, sys

root = sys.argv[1] if len(sys.argv) > 1 else "../uAthena"
OUT = "src/resource/EmbeddedMvpSpawns.cpp"

mexp = {}
for name in ("db/mob_db.txt", "db/mob_db2.txt"):
    p = os.path.join(root, name)
    if not os.path.exists(p):
        continue
    for line in open(p, encoding="utf-8", errors="replace"):
        f = line.strip().split(",")
        if len(f) > 30 and f[0].isdigit():
            mexp[int(f[0])] = int(f[30]) if f[30].strip().lstrip("-").isdigit() else 0

seen, spawns = set(), {}
def load_conf(path):
    if path in seen or not os.path.exists(os.path.join(root, path)):
        return
    seen.add(path)
    for line in open(os.path.join(root, path), encoding="utf-8", errors="replace"):
        s = line.strip()
        if s.startswith("//"):
            continue
        m = re.match(r"(npc|import)\s*:\s*(\S+)", s)
        if not m:
            continue
        if m.group(1) == "import":
            load_conf(m.group(2))
        else:
            load_npc(m.group(2))

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
        mp = parts[0].split(",")[0].replace(".gat", "")
        f = parts[3].split(",")
        if not f[0].strip().isdigit():
            continue
        mob = int(f[0])
        d1 = int(f[2]) if len(f) > 2 and f[2].strip().isdigit() else 0
        if mexp.get(mob, 0) > 0 and d1 > 0:
            spawns[(mp, mob)] = max(spawns.get((mp, mob), 0), d1)

load_conf("npc/scripts_main.conf")
rows = sorted(spawns.items())
with open(OUT, "w", encoding="utf-8") as o:
    o.write("// Auto-generated from the server's spawned MVPs (tools/gen_mvp_spawns.py) -- do not edit.\n")
    o.write("// (map, mob id) -> respawn delay in ms (the spawn line's delay1; the server has no random window). %d spawns.\n" % len(rows))
    o.write("#include <cstdint>\n#include <cstring>\n\nnamespace uaro {\n\n")
    o.write("struct MvpSpawn { const char* map; std::uint16_t mob; std::uint32_t delayMs; };\n")
    o.write("static const MvpSpawn kMvpSpawns[] = {\n")
    for (mp, mob), d in rows:
        o.write('    {"%s", %d, %d},\n' % (mp, mob, d))
    o.write("};\n\n")
    o.write("// The respawn delay of an MVP spawned on that map (ms), or 0 when it is not one.\n")
    o.write("std::uint32_t embedded_mvp_respawn_ms(const char* map, std::uint16_t mob) {\n")
    o.write("    for (const MvpSpawn& s : kMvpSpawns)\n")
    o.write("        if (s.mob == mob && std::strcmp(s.map, map) == 0) return s.delayMs;\n")
    o.write("    return 0;\n}\n\n}  // namespace uaro\n")
print("%s: %d MVP spawns, %d conf/npc files read" % (OUT, len(rows), len(seen)))
