#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Parse the classic client's mp3nametable.txt (we load data/english/mp3nametable.txt or the default):
// repeated lines `<map>.rsw#bgm\\<NN>.mp3#`. Comment lines start with //. Returns a map keyed by the
// map's BASE name (lowercased, extension stripped) -> BGM path with separators normalised to '/'
// (e.g. "prontera" -> "bgm/116.mp3"). Look up with the in-game map base name. (#103)
inline std::unordered_map<std::string, std::string> parseMp3NameTable(const std::vector<u8>& bytes) {
    std::unordered_map<std::string, std::string> out;
    const std::string s(bytes.begin(), bytes.end());
    usize pos = 0;
    while (pos < s.size()) {
        const usize nl = s.find('\n', pos);
        usize beg = pos, end = (nl == std::string::npos) ? s.size() : nl;
        pos = (nl == std::string::npos) ? s.size() : nl + 1;
        while (end > beg && (s[end - 1] == '\r' || s[end - 1] == ' ')) --end;
        std::string line = s.substr(beg, end - beg);
        if (line.size() < 3 || (line[0] == '/' && line[1] == '/')) continue;
        const usize h1 = line.find('#');
        if (h1 == std::string::npos) continue;
        std::string map = line.substr(0, h1);
        usize h2 = line.find('#', h1 + 1);
        std::string bgm = line.substr(h1 + 1, (h2 == std::string::npos) ? std::string::npos : h2 - h1 - 1);
        if (map.empty() || bgm.empty()) continue;
        // map base name: lowercase ASCII, drop the extension.
        if (const usize dot = map.rfind('.'); dot != std::string::npos) map = map.substr(0, dot);
        for (char& c : map)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        // bgm path: collapse backslashes (incl. the file's escaped "\\") to single '/'.
        std::string p;
        for (usize i = 0; i < bgm.size(); ++i) {
            if (bgm[i] == '\\') {
                if (p.empty() || p.back() != '/') p += '/';
            } else {
                p += bgm[i];
            }
        }
        out[map] = p;
    }
    return out;
}

} // namespace uaro
