#pragma once
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// uaRO.exe keeps its job / hair / palette name tables in text files (data/class_tab.txt,
// data/reality_tab.txt, data/vanity_m_tab.txt, ...; CP949, CRLF) and reads them all with one parser
// (exe 0x0061e6f1). Its rules, reproduced here:
//   - a line holds the value of the NEXT index (starting at 0), then the index advances;
//   - "!N" (decimal) makes N the next index (the exe accepts N <= 0xFFFF);
//   - "*X" is the default for every index without a value of its own;
//   - "#" starts a comment; leading/trailing spaces, tabs and CRs are dropped; an empty line takes no
//     index; "$" / "%" / "&" include another table file (no uaRO table uses that -> skipped here).
// The table holds (highest index with a value) + 1 entries; the exe reads past that unchecked.
inline std::vector<std::string> parseExeNameTable(const std::vector<u8>& bytes) {
    const auto trim = [](const std::string& t) {
        const auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
        usize b = 0, e = t.size();
        while (b < e && ws(t[b])) ++b;
        while (e > b && ws(t[e - 1])) --e;
        return t.substr(b, e - b);
    };
    std::vector<std::string> out;  // "" = no value of its own (values are never empty)
    std::string def;
    usize next = 0;
    const std::string s(bytes.begin(), bytes.end());
    usize pos = 0;
    while (pos < s.size()) {
        const usize nl = s.find('\n', pos);
        std::string line = s.substr(pos, (nl == std::string::npos ? s.size() : nl) - pos);
        pos = (nl == std::string::npos) ? s.size() : nl + 1;
        if (const usize hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '!') {
            usize n = 0;
            for (usize i = 1; i < line.size() && line[i] >= '0' && line[i] <= '9' && n <= 0xFFFF; ++i)
                n = n * 10 + static_cast<usize>(line[i] - '0');
            if (n <= 0xFFFF) next = n;  // past the exe's limit it would crash; ignore the line instead
            continue;
        }
        if (line[0] == '*') {
            def = trim(line.substr(1));
            continue;
        }
        if (line[0] == '$' || line[0] == '%' || line[0] == '&') continue;  // include: unused by uaRO
        if (out.size() <= next) out.resize(next + 1);
        out[next++] = line;
    }
    for (std::string& v : out)
        if (v.empty()) v = def;
    return out;
}

// Row of a uaRO.exe job table for a server class id: ids below 3951 index directly, higher ones (the
// 4001.. trans / baby / expanded classes) are shifted down by 3950 (exe 0x006399e0): 4001 -> 51.
inline usize exeJobTableIndex(u16 classId) {
    return classId < 3951 ? classId : static_cast<usize>(classId) - 3950;
}

// The class id uaRO.exe renders (and picks the palette of) a player as (exe 0x0054aee0): with the riding
// option a knight / crusader / lord knight / paladin and their baby forms become their peco id. The server
// already sends that id for other players, but the own character arrives as its base class + riding.
// (The exe also turns 4047 + OPTION_FLYING into 4048; the server sends 4048 itself, same palette row.)
inline u16 exeRenderClassId(u16 classId, bool riding) {
    if (!riding) return classId;
    switch (classId) {
        case 7:    return 13;    // Knight        -> Peco Knight
        case 14:   return 21;    // Crusader      -> Peco Crusader
        case 4008: return 4014;  // Lord Knight   -> Peco Lord Knight
        case 4015: return 4022;  // Paladin       -> Peco Paladin
        case 4030: return 4036;  // Baby Knight   -> Baby Peco Knight
        case 4037: return 4044;  // Baby Crusader -> Baby Peco Crusader
        default:   return classId;
    }
}

// The clothes-dye palette family uaRO.exe uses for a class: its row in data/reality_tab.txt (Crusader
// -> 기사, Assassin Cross -> 어세신, every peco rider -> 페코페코_기사, ...). Takes the class id the exe
// renders (exeRenderClassId: the peco ids 13 / 21 / 4014 / 4022 have rows of their own). "" when the
// table is empty or the id is past its end: the caller falls back to its own name.
inline std::string bodyPaletteName(const std::vector<std::string>& realityTab, u16 classId) {
    const usize i = exeJobTableIndex(classId);
    return i < realityTab.size() ? realityTab[i] : std::string();
}

// The clothes-dye palette path uaRO.exe loads: palette\ + "몸\%s%s_%d.pal" with the sex suffix _여 (0)
// or _남 (1) -> data/palette/몸/<name>_<sex>_<colour>.pal in CP949. Colour 0 = no dye -> "".
inline std::string bodyPalettePath(const std::string& name, bool male, u16 colour) {
    if (colour == 0 || name.empty()) return {};
    const char* body = "\xb8\xf6";                     // 몸
    const char* sex = male ? "\xb3\xb2" : "\xbf\xa9";  // 남 / 여
    return std::string("data/palette/") + body + "/" + name + "_" + sex + "_" + std::to_string(colour) + ".pal";
}

} // namespace uaro
