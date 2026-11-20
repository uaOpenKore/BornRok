#include "microtest.hpp"
#include "core/crypto/Sha512.hpp"
#include "resource/ExeNameTable.hpp"

#include <string>
#include <vector>

using namespace uaro;

namespace {

std::vector<u8> bytesOf(const std::string& s) { return {s.begin(), s.end()}; }

// Palette family names in CP949, as data/reality_tab.txt stores them.
const std::string kNovice = "\xc3\xca\xba\xb8\xc0\xda";                            // 초보자
const std::string kSwordman = "\xb0\xcb\xbb\xe7";                                  // 검사
const std::string kMage = "\xb8\xb6\xb9\xfd\xbb\xe7";                              // 마법사
const std::string kArcher = "\xb1\xc3\xbc\xf6";                                    // 궁수
const std::string kAcolyte = "\xbc\xba\xc1\xf7\xc0\xda";                           // 성직자
const std::string kMerchant = "\xbb\xf3\xc0\xce";                                  // 상인
const std::string kThief = "\xb5\xb5\xb5\xcf";                                     // 도둑
const std::string kKnight = "\xb1\xe2\xbb\xe7";                                    // 기사
const std::string kPriest = "\xc7\xc1\xb8\xae\xbd\xba\xc6\xae";                    // 프리스트
const std::string kWizard = "\xc0\xa7\xc0\xfa\xb5\xe5";                            // 위저드
const std::string kBlacksmith = "\xc1\xa6\xc3\xb6\xb0\xf8";                        // 제철공
const std::string kHunter = "\xc7\xe5\xc5\xcd";                                    // 헌터
const std::string kAssassin = "\xbe\xee\xbc\xbc\xbd\xc5";                          // 어세신
const std::string kPecoKnight = "\xc6\xe4\xc4\xda\xc6\xe4\xc4\xda_\xb1\xe2\xbb\xe7";  // 페코페코_기사
const std::string kWedding = "\xb0\xe1\xc8\xa5";                                   // 결혼
const std::string kSuperNovice = "\xbd\xb4\xc6\xdb\xb3\xeb\xba\xf1\xbd\xba";       // 슈퍼노비스
const std::string kTaekwon = "\xc5\xc2\xb1\xc7\xbc\xd2\xb3\xe2";                   // 태권소년

// data/reality_tab.txt exactly as uaRO ships it (data.zip / the uaRO GRF: 632 bytes, CP949, CRLF).
std::string realityTabTxt() {
    const std::vector<std::string> lines = {
        "!1", kSwordman, kMage, kArcher, kAcolyte, kMerchant, kThief, kKnight, kPriest, kWizard,
        kBlacksmith, kHunter, kAssassin, kPecoKnight, kKnight, kPriest, kWizard, kAssassin, kBlacksmith,
        kHunter, kHunter, kPecoKnight, kWedding, kSuperNovice, "!26", kPriest, "!52", kSwordman, kMage,
        kArcher, kAcolyte, kMerchant, kThief, kKnight, kPriest, kWizard, kBlacksmith, kHunter, kAssassin,
        kPecoKnight, kKnight, kPriest, kWizard, kKnight, kBlacksmith, kHunter, kHunter, kPecoKnight, "!74",
        kSwordman, kMage, kArcher, kAcolyte, kMerchant, kThief, kKnight, kPriest, kWizard, kBlacksmith,
        kHunter, kAssassin, kPecoKnight, kKnight, kPriest, kWizard, kAssassin, kBlacksmith, kHunter,
        kHunter, kPecoKnight, kSuperNovice, kTaekwon, kTaekwon, kTaekwon, kTaekwon, kAcolyte, kKnight,
        kWizard, "*" + kNovice};
    std::string s;
    for (const std::string& l : lines) s += l + "\r\n";
    return s;
}

std::string palNameFor(u16 classId, bool riding = false) {
    static const std::vector<std::string> tab = parseExeNameTable(bytesOf(realityTabTxt()));
    return bodyPaletteName(tab, exeRenderClassId(classId, riding));
}

}  // namespace

TEST_CASE(exenametable_fixture_is_the_shipped_reality_tab) {
    const std::string txt = realityTabTxt();
    CHECK_EQ(txt.size(), static_cast<usize>(632));
    CHECK(Sha512::hashHex(txt.data(), txt.size()) ==
          "db5da98af8fbc6eb9966300e4841bd6076de596e864c737d1bfc17850a1fff23"
          "c4167103254e6f94b88f5a7def897c2ae3f62655da277e0f99a75f8a829bc976");
}

TEST_CASE(exenametable_index_jump_and_default) {
    // "!N" sets the next index, "*X" fills every index without a value, the size is max index + 1.
    const auto t = parseExeNameTable(bytesOf("!2\r\na\r\nb\r\n!5\r\nc\r\n*z\r\n"));
    CHECK_EQ(t.size(), static_cast<usize>(6));
    CHECK(t[0] == "z");
    CHECK(t[1] == "z");
    CHECK(t[2] == "a");
    CHECK(t[3] == "b");
    CHECK(t[4] == "z");
    CHECK(t[5] == "c");
}

TEST_CASE(exenametable_blank_comment_and_whitespace) {
    // Empty lines take no index; '#' starts a comment; spaces/tabs/CR around a value are dropped; an
    // include ($ % &) is skipped; LF-only files work too.
    const auto t = parseExeNameTable(bytesOf("  x \t\r\n\r\n# note\r\ny # tail\n$other.txt\nz"));
    CHECK_EQ(t.size(), static_cast<usize>(3));
    CHECK(t[0] == "x");
    CHECK(t[1] == "y");
    CHECK(t[2] == "z");
}

TEST_CASE(exenametable_index_past_exe_limit_is_ignored) {
    // The exe accepts "!N" only up to 0xFFFF (beyond, it would crash): such a line changes nothing.
    const auto t = parseExeNameTable(bytesOf("!70000\r\na\r\n!3\r\nb\r\n"));
    CHECK_EQ(t.size(), static_cast<usize>(4));
    CHECK(t[0] == "a");
    CHECK(t[3] == "b");
    CHECK(t[1].empty());  // no "*X" default -> empty
    CHECK(parseExeNameTable({}).empty());
}

TEST_CASE(exenametable_job_index) {
    CHECK_EQ(exeJobTableIndex(0), static_cast<usize>(0));
    CHECK_EQ(exeJobTableIndex(26), static_cast<usize>(26));
    CHECK_EQ(exeJobTableIndex(3950), static_cast<usize>(3950));
    CHECK_EQ(exeJobTableIndex(3951), static_cast<usize>(1));
    CHECK_EQ(exeJobTableIndex(4001), static_cast<usize>(51));
    CHECK_EQ(exeJobTableIndex(4049), static_cast<usize>(99));
}

TEST_CASE(exenametable_render_class_maps_riders_to_peco_ids) {
    CHECK_EQ(exeRenderClassId(7, true), 13);        // Knight + riding
    CHECK_EQ(exeRenderClassId(14, true), 21);       // Crusader
    CHECK_EQ(exeRenderClassId(4008, true), 4014);   // Lord Knight
    CHECK_EQ(exeRenderClassId(4015, true), 4022);   // Paladin
    CHECK_EQ(exeRenderClassId(4030, true), 4036);   // Baby Knight
    CHECK_EQ(exeRenderClassId(4037, true), 4044);   // Baby Crusader
    CHECK_EQ(exeRenderClassId(4014, true), 4014);   // already a peco id
    CHECK_EQ(exeRenderClassId(7, false), 7);
    CHECK_EQ(exeRenderClassId(4012, true), 4012);   // no peco form
}

TEST_CASE(exenametable_reality_tab_palette_names) {
    CHECK(palNameFor(0) == kNovice);       // Novice: no row -> the "*" default
    CHECK(palNameFor(4001) == kNovice);    // High Novice: no row either
    CHECK(palNameFor(1) == kSwordman);
    CHECK(palNameFor(4002) == kSwordman);  // High Swordman
    CHECK(palNameFor(14) == kKnight);      // Crusader uses the Knight palettes
    CHECK(palNameFor(15) == kPriest);      // Monk -> Priest
    CHECK(palNameFor(19) == kHunter);      // Bard -> Hunter
    CHECK(palNameFor(20) == kHunter);      // Dancer -> Hunter
    CHECK(palNameFor(24) == kNovice);      // Gunslinger -> default
    CHECK(palNameFor(25) == kNovice);      // Ninja -> default
    CHECK(palNameFor(26) == kPriest);      // Xmas -> Priest
    CHECK(palNameFor(27) == kNovice);      // Summer -> default
    CHECK(palNameFor(22) == kWedding);
    // Transcendent 2nd classes use the base 2-1 / 2-2 families.
    CHECK(palNameFor(4008) == kKnight);      // Lord Knight
    CHECK(palNameFor(4009) == kPriest);      // High Priest
    CHECK(palNameFor(4010) == kWizard);      // High Wizard
    CHECK(palNameFor(4011) == kBlacksmith);  // Whitesmith
    CHECK(palNameFor(4012) == kHunter);      // Sniper
    CHECK(palNameFor(4013) == kAssassin);    // Assassin Cross
    CHECK(palNameFor(4015) == kKnight);      // Paladin
    CHECK(palNameFor(4016) == kPriest);      // Champion
    CHECK(palNameFor(4017) == kWizard);      // Professor
    CHECK(palNameFor(4018) == kKnight);      // Stalker (sic, uaRO's table)
    CHECK(palNameFor(4019) == kBlacksmith);  // Creator
    CHECK(palNameFor(4020) == kHunter);      // Clown
    CHECK(palNameFor(4021) == kHunter);      // Gypsy
    // Every peco rider uses the Peco Knight family, whether the server sent the peco id or the own
    // character came as base class + riding.
    CHECK(palNameFor(13) == kPecoKnight);
    CHECK(palNameFor(21) == kPecoKnight);    // Peco Crusader
    CHECK(palNameFor(4014) == kPecoKnight);  // Peco Lord Knight
    CHECK(palNameFor(4022) == kPecoKnight);  // Peco Paladin
    CHECK(palNameFor(4008, true) == kPecoKnight);
    CHECK(palNameFor(4015, true) == kPecoKnight);
    CHECK(palNameFor(4037) == kKnight);      // Baby Crusader
    CHECK(palNameFor(4045) == kSuperNovice); // Super Baby
    CHECK(palNameFor(4046) == kTaekwon);
    CHECK(palNameFor(4047) == kTaekwon);     // Star Gladiator
    CHECK(palNameFor(4048) == kTaekwon);     // Star Gladiator (union)
    CHECK(palNameFor(4049) == kTaekwon);     // Soul Linker
    CHECK(palNameFor(4054).empty());         // past the table (3rd class) -> caller's fallback
    CHECK(bodyPaletteName({}, 14).empty());  // no table at all -> caller's fallback
}

TEST_CASE(exenametable_body_palette_path) {
    // data/palette/몸/<name>_<남|여>_<colour>.pal; colour 0 means "no dye" -> no path.
    CHECK(bodyPalettePath(kKnight, true, 5) ==
          "data/palette/\xb8\xf6/" + kKnight + "_\xb3\xb2_5.pal");
    CHECK(bodyPalettePath(kAssassin, false, 77) ==
          "data/palette/\xb8\xf6/" + kAssassin + "_\xbf\xa9_77.pal");
    CHECK(bodyPalettePath(kKnight, true, 0).empty());
    CHECK(bodyPalettePath("", true, 3).empty());
}
