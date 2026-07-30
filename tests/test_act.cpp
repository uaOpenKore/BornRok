#include "formats/Act.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "microtest.hpp"

using namespace uaro;

namespace {
using B = std::vector<std::uint8_t>;

void pu8(B& v, std::uint8_t x) { v.push_back(x); }
void pu16(B& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xff));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xff));
}
void pi32(B& v, std::int32_t x) {
    auto u = static_cast<std::uint32_t>(x);
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>((u >> (i * 8)) & 0xff));
}
void pf32(B& v, float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>((u >> (i * 8)) & 0xff));
}
void pad(B& v, int n) {
    for (int i = 0; i < n; ++i) v.push_back(0);
}
void pname(B& v, const std::string& s) {
    for (char c : s) v.push_back(static_cast<std::uint8_t>(c));
    for (std::size_t i = s.size(); i < 40; ++i) v.push_back(0);
}
} // namespace

TEST_CASE(act_full_v205) {
    B b{'A', 'C'};
    pu16(b, 0x205);  // version 2.5
    pu16(b, 1);      // action count
    pad(b, 10);      // reserved

    // action 0
    pi32(b, 1);      // frame count (u32)
    // frame 0
    pad(b, 32);      // per-frame reserved range box
    pi32(b, 1);      // layer count
    // layer 0
    pi32(b, 10);     // x
    pi32(b, -20);    // y
    pi32(b, 3);      // spr index
    pi32(b, 1);      // mirror
    pu8(b, 255); pu8(b, 128); pu8(b, 64); pu8(b, 255);  // rgba
    pf32(b, 2.0f);   // scaleX
    pf32(b, 3.0f);   // scaleY (v >= 2.4)
    pi32(b, 45);     // rotation
    pi32(b, 0);      // spr type
    pi32(b, 16);     // width  (v >= 2.5)
    pi32(b, 24);     // height
    pi32(b, 0);      // event id (v >= 2.0)
    pi32(b, 1);      // anchor count (v >= 2.3)
    pi32(b, 0); pi32(b, 5); pi32(b, 6); pi32(b, 7);  // reserved, x, y, attr

    // events (v >= 2.1)
    pi32(b, 1);
    pname(b, "attack.wav");
    // delays (v >= 2.2)
    pf32(b, 4.0f);

    auto act = Action::parse(b);
    CHECK(act.has_value());
    if (!act) return;
    CHECK_EQ(act->version(), 0x205u);
    CHECK_EQ(act->actions().size(), static_cast<usize>(1));

    const ActAction& a0 = act->actions()[0];
    CHECK_EQ(a0.frames.size(), static_cast<usize>(1));
    CHECK_NEAR(a0.delay, 4.0, 1e-5);

    const ActFrame& f0 = a0.frames[0];
    CHECK_EQ(f0.layers.size(), static_cast<usize>(1));
    CHECK_EQ(f0.eventId, 0);
    CHECK_EQ(f0.anchors.size(), static_cast<usize>(1));
    CHECK_EQ(f0.anchors[0][0], 5);
    CHECK_EQ(f0.anchors[0][2], 7);

    const ActLayer& ly = f0.layers[0];
    CHECK_EQ(ly.x, 10);
    CHECK_EQ(ly.y, -20);
    CHECK_EQ(ly.sprIndex, 3);
    CHECK_EQ(ly.mirror, 1);
    CHECK_EQ(ly.g, 128);
    CHECK_NEAR(ly.scaleX, 2.0, 1e-5);
    CHECK_NEAR(ly.scaleY, 3.0, 1e-5);
    CHECK_EQ(ly.rotation, 45);
    CHECK_EQ(ly.width, 16);
    CHECK_EQ(ly.height, 24);

    CHECK_EQ(act->events().size(), static_cast<usize>(1));
    if (!act->events().empty()) CHECK(act->events()[0].name == "attack.wav");
}

TEST_CASE(act_v200_gates) {
    B b{'A', 'C'};
    pu16(b, 0x200);  // version 2.0
    pu16(b, 1);
    pad(b, 10);
    pi32(b, 1);      // frame count
    pad(b, 32);
    pi32(b, 1);      // layer count
    pi32(b, 1); pi32(b, 2); pi32(b, 0); pi32(b, 0);  // x,y,spr,mirror
    pu8(b, 10); pu8(b, 20); pu8(b, 30); pu8(b, 255);  // rgba
    pf32(b, 1.5f);   // scaleX (scaleY mirrors it for v < 2.4)
    pi32(b, 0);      // rotation
    pi32(b, 1);      // spr type
    pi32(b, -1);     // event id
    // no anchors (v < 2.3), no events (v < 2.1), no delays (v < 2.2)

    auto act = Action::parse(b);
    CHECK(act.has_value());
    if (!act) return;
    CHECK_EQ(act->version(), 0x200u);
    const ActLayer& ly = act->actions()[0].frames[0].layers[0];
    CHECK_NEAR(ly.scaleX, 1.5, 1e-5);
    CHECK_NEAR(ly.scaleY, 1.5, 1e-5);  // mirrors scaleX
    CHECK_EQ(ly.width, 0);             // not present in 2.0
    CHECK_EQ(ly.sprType, 1);
    CHECK(act->events().empty());
}

TEST_CASE(act_rejects_bad_signature) {
    B b{'X', 'X'};
    pad(b, 20);
    CHECK(!Action::parse(b).has_value());
}
