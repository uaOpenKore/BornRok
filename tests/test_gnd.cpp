#include "formats/Gnd.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "microtest.hpp"

using namespace uaro;

namespace {
void pu8(std::vector<u8>& v, u8 x) { v.push_back(x); }
void pu32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>((x >> (i * 8)) & 0xff));
}
void pi32(std::vector<u8>& v, i32 x) { pu32(v, static_cast<u32>(x)); }
void pi16(std::vector<u8>& v, i16 x) {
    auto u = static_cast<u16>(x);
    v.push_back(static_cast<u8>(u & 0xff));
    v.push_back(static_cast<u8>((u >> 8) & 0xff));
}
void pf32(std::vector<u8>& v, float f) {
    u32 u;
    std::memcpy(&u, &f, 4);
    pu32(v, u);
}
void pname(std::vector<u8>& v, const std::string& s, int len) {
    for (char c : s) v.push_back(static_cast<u8>(c));
    for (int i = static_cast<int>(s.size()); i < len; ++i) v.push_back(0);
}
} // namespace

TEST_CASE(gnd_parses_v17) {
    std::vector<u8> b{'G', 'R', 'G', 'N'};
    pu8(b, 1);
    pu8(b, 7);          // version 1.7
    pu32(b, 1);         // width
    pu32(b, 1);         // height
    pf32(b, 10.0f);     // zoom
    pu32(b, 1);         // texture count
    pu32(b, 8);         // texture name length
    pname(b, "tex.bmp", 8);
    // lightmaps
    pi32(b, 1);         // lightmap count
    pi32(b, 8);         // per cell x
    pi32(b, 8);         // per cell y
    pi32(b, 1);         // size/mipmap
    for (int i = 0; i < 8 * 8 * 4; ++i) pu8(b, 0);  // lightmap pixel data
    // surfaces
    pu32(b, 1);         // surface count
    pf32(b, 0); pf32(b, 1); pf32(b, 0); pf32(b, 1);  // u[4]
    pf32(b, 0); pf32(b, 0); pf32(b, 1); pf32(b, 1);  // v[4]
    pi16(b, 2);         // texture id
    pi16(b, 0);         // lightmap id
    pu8(b, 10); pu8(b, 20); pu8(b, 30); pu8(b, 255);  // color BGRA
    // cubes (width*height = 1)
    pf32(b, 1); pf32(b, 2); pf32(b, 3); pf32(b, 4);  // heights
    pi32(b, 0);         // tile up
    pi32(b, -1);        // tile front
    pi32(b, -1);        // tile right

    auto g = Gnd::parse(b);
    CHECK(g.has_value());
    if (!g) return;
    CHECK_EQ(g->width(), 1u);
    CHECK_EQ(g->height(), 1u);
    CHECK_NEAR(g->zoom(), 10.0, 1e-5);
    CHECK_EQ(g->textures().size(), static_cast<usize>(1));
    if (!g->textures().empty()) CHECK(g->textures()[0] == "tex.bmp");
    CHECK_EQ(g->lightmapCount(), 1);
    CHECK_EQ(g->surfaces().size(), static_cast<usize>(1));
    CHECK_EQ(g->cubes().size(), static_cast<usize>(1));
    if (!g->surfaces().empty()) {
        CHECK_EQ(g->surfaces()[0].textureId, 2);
        CHECK_EQ(g->surfaces()[0].color[0], 10);
    }
    if (!g->cubes().empty()) {
        CHECK_EQ(g->cubes()[0].tileUp, 0);
        CHECK_EQ(g->cubes()[0].tileFront, -1);
        CHECK_NEAR(g->cubes()[0].height[3], 4.0, 1e-5);
    }
}

TEST_CASE(gnd_rejects_bad_signature) {
    std::vector<u8> b(32, 0);
    b[0] = 'X';
    CHECK(!Gnd::parse(b).has_value());
}
