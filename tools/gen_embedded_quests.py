#!/usr/bin/env python3
# Regenerate src/resource/EmbeddedQuestTitles.cpp from the server db/quest_db.txt so the client quest
# journal shows quest names (not "Quest #id"). Run from Client/. Emitted as a BYTE ARRAY (not one big
# string literal -- MSVC caps string literals at ~16KB, C2026). Format of the blob: <id>\t<title>\n.
import re
DB = "../db/quest_db.txt"
OUT = "src/resource/EmbeddedQuestTitles.cpp"
seen = {}
for line in open(DB, encoding="utf-8", errors="replace"):
    s = line.strip()
    if not s or s.startswith("//"): continue
    m = re.match(r'^(\d+),.*?,"(.*)"\s*$', s)
    if m: qid, title = m.group(1), m.group(2)
    else:
        parts = s.split(",")
        if len(parts) >= 9 and parts[0].isdigit(): qid, title = parts[0], parts[-1].strip().strip('"')
        else: continue
    title = title.replace("\t", " ").replace("\n", " ").strip()
    if title: seen[int(qid)] = title
blob = "".join(f"{k}\t{v}\n" for k, v in sorted(seen.items())).encode("utf-8")
with open(OUT, "w", encoding="utf-8") as o:
    o.write("// Auto-generated from server db/quest_db.txt (tools/gen_embedded_quests.py) -- do not edit.\n")
    o.write("// Quest id -> display title, baked in so the quest journal shows names not \"Quest #id\".\n")
    o.write("// A byte array (not a string literal: MSVC caps literals at ~16KB). Blob: <id>\\t<title>\\n.\n")
    o.write("// An on-disk data/quest_title.txt (VFS) still overrides these.\n")
    o.write("#include <string>\n\nnamespace uaro {\n\n")
    o.write("static const unsigned char kQuestTitlesData[] = {\n")
    line = []
    for b in blob:
        line.append(str(b))
        if len(line) == 24:
            o.write("    " + ",".join(line) + ",\n"); line = []
    if line: o.write("    " + ",".join(line) + ",\n")
    o.write("};\n\n")
    o.write("const std::string& embedded_quest_titles() {\n")
    o.write("    static const std::string kT(reinterpret_cast<const char*>(kQuestTitlesData), sizeof(kQuestTitlesData));\n")
    o.write("    return kT;\n}\n\n}  // namespace uaro\n")
print("entries:", len(seen), "blob bytes:", len(blob))
