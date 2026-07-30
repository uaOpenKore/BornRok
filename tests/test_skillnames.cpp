#include "microtest.hpp"
#include "net/SkillNames.hpp"

using namespace uaro;

TEST_CASE(skillnames_known) {
    // A few representative skills from roBrowser's SkillInfo table.
    CHECK(net::skillName("SN_WINDWALK") == "Wind Walk");
    CHECK(net::skillName("AL_RUWACH") == "Ruwach");
    CHECK(net::skillName("LK_SPIRALPIERCE") == "Spiral Pierce");
}

TEST_CASE(skillnames_unknown_falls_back_to_internal) {
    CHECK(net::skillName("ZZ_NOT_A_SKILL") == "ZZ_NOT_A_SKILL");
    CHECK(net::skillName("") == "");
}
