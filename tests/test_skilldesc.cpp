#include "microtest.hpp"
#include "resource/SkillDescTable.hpp"

#include <string>

using namespace uaro;

static std::vector<u8> bytesOf(const std::string& s) { return {s.begin(), s.end()}; }

TEST_CASE(skilldesc_parses_named_blocks) {
    // Two blocks in the classic format: NAME# ... lines ... # terminator. CRLF line endings.
    const std::string txt =
        "//comment\r\n"
        "SM_BASH#\r\nBash\r\n^777777+Atk 130%^000000\r\n#\r\n"
        "AL_HEAL#\r\nHeal\r\n#\r\n";
    auto m = parseSkillDescTable(bytesOf(txt));
    CHECK_EQ(m.size(), static_cast<usize>(2));
    CHECK(m.count("SM_BASH") == 1);
    CHECK(m["SM_BASH"] == "Bash\n^777777+Atk 130%^000000");
    CHECK(m["AL_HEAL"] == "Heal");
}

TEST_CASE(skilldesc_unknown_absent) {
    auto m = parseSkillDescTable(bytesOf("SM_BASH#\r\nBash\r\n#\r\n"));
    CHECK(m.count("NV_BASIC") == 0);
}
