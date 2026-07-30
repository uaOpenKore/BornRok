#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Parse the classic client's skilldesctable.txt (we load data/english/skilldesctable.txt for the
// English client). Same block format as the item desc table, but keyed by the skill's INTERNAL
// name instead of a numeric id:
//   <SKILLNAME>#       (the internal name, e.g. SM_BASH, alone on its line followed by '#')
//   <description line>*  (free text, may carry ^RRGGBB colour codes)
//   #                  (a line that is just '#' ends the block)
// Returns internalName -> description (lines joined by '\n', colour codes kept).
inline std::unordered_map<std::string, std::string> parseSkillDescTable(const std::vector<u8>& bytes) {
    std::unordered_map<std::string, std::string> out;
    const std::string s(bytes.begin(), bytes.end());
    usize pos = 0;
    bool inEntry = false;
    std::string curName, acc;
    while (pos < s.size()) {
        const usize nl = s.find('\n', pos);
        usize beg = pos, end = (nl == std::string::npos) ? s.size() : nl;
        pos = (nl == std::string::npos) ? s.size() : nl + 1;
        while (end > beg && s[end - 1] == '\r') --end;  // trim trailing CR
        const std::string line = s.substr(beg, end - beg);
        if (!inEntry) {
            if (line.size() >= 2 && line[0] == '/' && line[1] == '/') continue;  // comment
            // "<NAME>#" opens a block: NAME is [A-Z][A-Z0-9_]* then a trailing '#'.
            if (line.size() >= 2 && line.back() == '#') {
                const std::string name = line.substr(0, line.size() - 1);
                bool ok = !name.empty() && (name[0] >= 'A' && name[0] <= 'Z');
                for (char c : name)
                    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) ok = false;
                if (ok) {
                    inEntry = true;
                    curName = name;
                    acc.clear();
                }
            }
        } else if (line == "#") {  // closes the block
            out[curName] = acc;
            inEntry = false;
        } else {
            if (!acc.empty()) acc += '\n';
            acc += line;
        }
    }
    return out;
}

} // namespace uaro
