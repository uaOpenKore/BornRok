#include "core/Ini.hpp"

#include "microtest.hpp"

using namespace uaro;

// Mirrors the real Client/winEXE/data.ini (GRF load order).
TEST_CASE(ini_parses_data_ini_layout) {
    const char* text =
        "[Data]\n"
        "\n"
        "0=new.grf\n"
        "1=Palettes.grf\n"
        "2=data.grf\n"
        "\n";
    Ini ini = Ini::parse(text);
    CHECK(ini.get("Data", "0") == "new.grf");
    CHECK(ini.get("Data", "1") == "Palettes.grf");
    CHECK(ini.get("Data", "2") == "data.grf");
    CHECK(ini.has("Data", "0"));
    CHECK(!ini.has("Data", "3"));
    CHECK(ini.get("Data", "3", "fallback") == "fallback");
}

TEST_CASE(ini_handles_comments_and_whitespace) {
    const char* text =
        "; a comment\n"
        "# another\n"
        "[ Server ]\n"
        "  host =  127.0.0.1  \n"
        "port=6900\n";
    Ini ini = Ini::parse(text);
    CHECK(ini.get("Server", "host") == "127.0.0.1");
    CHECK(ini.get("Server", "port") == "6900");
}

TEST_CASE(ini_missing_section_returns_default) {
    Ini ini = Ini::parse("[A]\nx=1\n");
    CHECK(ini.get("B", "x", "none") == "none");
}
