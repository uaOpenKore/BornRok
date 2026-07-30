#include "formats/Gat.hpp"

#include <cstring>
#include <vector>

#include "microtest.hpp"

using namespace uaro;

namespace {
void pu8(std::vector<u8>& v, u8 x) { v.push_back(x); }
void pu32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>((x >> (i * 8)) & 0xff));
}
void pf32(std::vector<u8>& v, float f) {
    u32 u;
    std::memcpy(&u, &f, 4);
    pu32(v, u);
}
} // namespace

TEST_CASE(gat_parses_grid) {
    std::vector<u8> b{'G', 'R', 'A', 'T'};
    pu8(b, 1);
    pu8(b, 2);       // version 1.2
    pu32(b, 2);      // width
    pu32(b, 1);      // height
    pf32(b, 1.0f); pf32(b, 2.0f); pf32(b, 3.0f); pf32(b, 4.0f); pu32(b, 0);  // cell 0, walkable
    pf32(b, 5.0f); pf32(b, 6.0f); pf32(b, 7.0f); pf32(b, 8.0f); pu32(b, 1);  // cell 1, blocked

    auto g = Gat::parse(b);
    CHECK(g.has_value());
    if (!g) return;
    CHECK_EQ(g->width(), 2u);
    CHECK_EQ(g->height(), 1u);
    CHECK_EQ(g->cells().size(), static_cast<usize>(2));
    CHECK_NEAR(g->at(0, 0).h[0], 1.0, 1e-5);
    CHECK_NEAR(g->at(1, 0).h[3], 8.0, 1e-5);
    CHECK(g->at(0, 0).walkable());
    CHECK(!g->at(1, 0).walkable());
}

TEST_CASE(gat_rejects_bad_signature) {
    std::vector<u8> b{'X', 'X', 'X', 'X', 1, 2, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!Gat::parse(b).has_value());
}
