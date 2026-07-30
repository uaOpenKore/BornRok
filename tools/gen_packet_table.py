#!/usr/bin/env python3
# Regenerate Client/src/net/PacketTable.cpp from the server's db/packet_db.txt.
#
# The map-server frames its stream with packet_len(id) read from packet_db.txt at
# the runtime default version (packet_db_ver: default = MAX_PACKET_VER = 22). The
# client must skip packets it does not decode, so it needs the same id->length
# table. We bake the effective table (versions 5..22 applied cumulatively,
# last definition wins) into a self-contained source so the client has no runtime
# dependency on the server file.
#
#   usage: python3 tools/gen_packet_table.py [path/to/packet_db.txt]
import re
import sys

src = sys.argv[1] if len(sys.argv) > 1 else "../db/packet_db.txt"
eff = {}
ver = 0
for line in open(src):
    line = line.split("//")[0].strip()
    if not line:
        continue
    if line.startswith("packet_ver:"):
        v = line.split(":")[1].strip()
        ver = int(v) if v.isdigit() else ver
        continue
    m = re.match(r"^(0x[0-9a-fA-F]+)\s*,\s*(-?\d+)", line)
    if m and ver <= 22:
        eff[int(m.group(1), 16)] = int(m.group(2))

items = sorted((pid, ln) for pid, ln in eff.items() if pid > 0 and (ln > 0 or ln == -1))
out = [
    "// GENERATED from uAthena db/packet_db.txt (effective packet_ver 22, the",
    "// server's runtime default = MAX_PACKET_VER). Maps a server->client packet",
    "// id to its on-wire length: >0 fixed, -1 variable (u16 length at offset 2).",
    "// Regenerate with tools/gen_packet_table.py if the server DB changes.",
    '#include "net/PacketTable.hpp"',
    "",
    "#include <algorithm>",
    "",
    "namespace uaro::net {",
    "namespace {",
    "// Sorted by id for binary search. {id, length}.",
    "struct Entry { u16 id; i16 len; };",
    "constexpr Entry kTable[] = {",
]
line = "    "
for pid, ln in items:
    tok = "{0x%04x,%d}," % (pid, ln)
    if len(line) + len(tok) > 96:
        out.append(line)
        line = "    "
    line += tok
if line.strip():
    out.append(line)
out += [
    "};",
    "} // namespace",
    "",
    "int packetDbLength(u16 id) {",
    "    const auto* end = kTable + (sizeof(kTable) / sizeof(kTable[0]));",
    "    const auto* it = std::lower_bound(kTable, end, id,",
    "                                      [](const Entry& e, u16 v) { return e.id < v; });",
    "    return (it != end && it->id == id) ? it->len : 0;",
    "}",
    "",
    "} // namespace uaro::net",
]
open("src/net/PacketTable.cpp", "w").write("\n".join(out) + "\n")
print("wrote src/net/PacketTable.cpp with", len(items), "entries")
