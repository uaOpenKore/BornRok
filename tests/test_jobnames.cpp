#include "microtest.hpp"
#include "net/JobNames.hpp"

using namespace uaro;

TEST_CASE(jobnames_classic) {
    CHECK(net::jobName(0) == "Novice");
    CHECK(net::jobName(1) == "Swordman");
    CHECK(net::jobName(7) == "Knight");
    CHECK(net::jobName(8) == "Priest");
    CHECK(net::jobName(23) == "Super Novice");
    CHECK(net::jobName(24) == "Gunslinger");
    CHECK(net::jobName(25) == "Ninja");
}

TEST_CASE(jobnames_trans_and_baby) {
    CHECK(net::jobName(4008) == "Lord Knight");
    CHECK(net::jobName(4011) == "Whitesmith");
    CHECK(net::jobName(4013) == "Assassin Cross");
    CHECK(net::jobName(4023) == "Baby Novice");
    CHECK(net::jobName(4045) == "Super Baby");
}

TEST_CASE(jobnames_expanded_and_third) {
    CHECK(net::jobName(4046) == "Taekwon");
    CHECK(net::jobName(4049) == "Soul Linker");
    CHECK(net::jobName(4054) == "Rune Knight");
    CHECK(net::jobName(4057) == "Arch Bishop");
    // transcendent 3rd (_H) and the mounted variant share the base name
    CHECK(net::jobName(4060) == "Rune Knight");
    CHECK(net::jobName(4080) == "Rune Knight");
    CHECK(net::jobName(4096) == "Baby Rune Knight");
}

TEST_CASE(jobnames_unknown_fallback) {
    CHECK(net::jobName(9999) == "Job 9999");
    CHECK(net::jobName(50000) == "Job 50000");
}
