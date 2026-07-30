#include "microtest.hpp"
#include "resource/Mp3NameTable.hpp"

#include <string>

using namespace uaro;

static std::vector<u8> bytesOf(const std::string& s) { return {s.begin(), s.end()}; }

TEST_CASE(mp3nametable_parses_map_to_bgm) {
    const std::string txt =
        "//map.rsw#bgm\\\\num.mp3#\r\n"     // comment line, ignored
        "prontera.rsw#bgm\\\\116.mp3#\r\n"
        "ma_zif09.rsw#bgm\\\\149.mp3#\r\n";
    auto m = parseMp3NameTable(bytesOf(txt));
    CHECK_EQ(m.size(), static_cast<usize>(2));
    CHECK(m.count("prontera") == 1);
    CHECK(m["prontera"] == "bgm/116.mp3");      // .rsw dropped, backslashes -> '/'
    CHECK(m["ma_zif09"] == "bgm/149.mp3");
}

TEST_CASE(mp3nametable_lowercases_and_strips_ext) {
    auto m = parseMp3NameTable(bytesOf("PRT_Fild01.rsw#bgm\\\\05.mp3#\r\n"));
    CHECK(m.count("prt_fild01") == 1);
    CHECK(m["prt_fild01"] == "bgm/05.mp3");
}
