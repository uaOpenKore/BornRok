#include "microtest.hpp"
#include "net/SkillCastDelay.hpp"

using namespace uaro;

TEST_CASE(skillcastdelay_known) {
    // Baked from db/skill_cast_db.txt (AfterCastActDelay, top level).
    CHECK_EQ(net::skillCastDelayMs(83), 7000u);   // WZ_METEOR
    CHECK_EQ(net::skillCastDelayMs(89), 5000u);   // WZ_STORMGUST
    CHECK_EQ(net::skillCastDelayMs(11), 500u);    // MG_FIREBOLT
}

TEST_CASE(skillcastdelay_instant_or_unknown_is_zero) {
    CHECK_EQ(net::skillCastDelayMs(1), 0u);       // NV_BASIC (not in table)
    CHECK_EQ(net::skillCastDelayMs(60000), 0u);   // nonsense id
}
