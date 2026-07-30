#include "formats/Rsw.hpp"

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

TEST_CASE(rsw_parses_v21) {
    std::vector<u8> b{'G', 'R', 'S', 'W'};
    pu8(b, 2);
    pu8(b, 1);  // version 2.1
    pname(b, "", 40);          // ini
    pname(b, "test.gnd", 40);  // gnd
    pname(b, "test.gat", 40);  // gat
    pname(b, "test.rsw", 40);  // scr
    // water (v >= 1.3, animSpeed v >= 1.8)
    pf32(b, 23.0f); pi32(b, 0); pf32(b, 1.0f); pf32(b, 2.0f); pf32(b, 50.0f); pi32(b, 3);
    // light
    pi32(b, 45); pi32(b, 45);
    pf32(b, 1); pf32(b, 1); pf32(b, 1);          // diffuse
    pf32(b, 0.5f); pf32(b, 0.5f); pf32(b, 0.5f);  // ambient
    pf32(b, 0.5f);                                 // opacity (v >= 1.7)
    pi32(b, -100); pi32(b, 100); pi32(b, -100); pi32(b, 100);  // ground box (v >= 1.6)
    pi32(b, 1);    // object count
    // one model object
    pi32(b, 1);    // type = model
    pname(b, "obj1", 40);
    pi32(b, 0);            // animType
    pf32(b, 1.0f);         // animSpeed
    pi32(b, 0);            // blockType
    pname(b, "build.rsm", 80);
    pname(b, "node", 80);
    pf32(b, 10); pf32(b, 20); pf32(b, 30);  // pos
    pf32(b, 0); pf32(b, 0); pf32(b, 0);     // rot
    pf32(b, 1); pf32(b, 1); pf32(b, 1);     // scale

    auto w = Rsw::parse(b);
    CHECK(w.has_value());
    if (!w) return;
    CHECK_EQ(w->version(), 0x201u);
    CHECK(w->gndFile() == "test.gnd");
    CHECK(w->gatFile() == "test.gat");
    CHECK_NEAR(w->water().level, 23.0, 1e-4);
    CHECK_EQ(w->water().animSpeed, 3);
    CHECK_EQ(w->light().longitude, 45);
    CHECK_NEAR(w->light().ambient[0], 0.5, 1e-5);
    CHECK_EQ(w->objectCount(), 1);
    CHECK_EQ(w->models().size(), static_cast<usize>(1));
    if (!w->models().empty()) {
        CHECK(w->models()[0].filename == "build.rsm");
        CHECK_NEAR(w->models()[0].pos[0], 10.0, 1e-5);
    }
    CHECK_EQ(w->trailing(), static_cast<usize>(0));
}

TEST_CASE(rsw_rejects_bad_signature) {
    std::vector<u8> b(200, 0);
    b[0] = 'X';
    CHECK(!Rsw::parse(b).has_value());
}
