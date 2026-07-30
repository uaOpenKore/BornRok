#include "formats/Rsm.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "microtest.hpp"

using namespace uaro;

namespace {
void pu8(std::vector<u8>& v, u8 x) { v.push_back(x); }
void pu16(std::vector<u8>& v, u16 x) {
    v.push_back(static_cast<u8>(x & 0xff));
    v.push_back(static_cast<u8>((x >> 8) & 0xff));
}
void pu32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>((x >> (i * 8)) & 0xff));
}
void pi32(std::vector<u8>& v, i32 x) { pu32(v, static_cast<u32>(x)); }
void pf32(std::vector<u8>& v, float f) {
    u32 u;
    std::memcpy(&u, &f, 4);
    pu32(v, u);
}
void pad(std::vector<u8>& v, int n) {
    for (int i = 0; i < n; ++i) v.push_back(0);
}
void pname(std::vector<u8>& v, const std::string& s, int len) {
    for (char c : s) v.push_back(static_cast<u8>(c));
    for (int i = static_cast<int>(s.size()); i < len; ++i) v.push_back(0);
}
} // namespace

TEST_CASE(rsm_parses_v14) {
    std::vector<u8> b{'G', 'R', 'S', 'M'};
    pu8(b, 1);
    pu8(b, 4);          // version 1.4
    pi32(b, 0);         // anim length
    pi32(b, 2);         // shade type
    pu8(b, 255);        // alpha (v >= 1.4)
    pad(b, 16);         // reserved
    pi32(b, 1);         // texture count
    pname(b, "t.bmp", 40);
    pname(b, "root", 40);  // main node
    pi32(b, 1);         // node count
    // --- node ---
    pname(b, "root", 40);
    pname(b, "", 40);   // parent
    pi32(b, 1);         // node texture count
    pi32(b, 0);         // texture index
    for (float x : {1, 0, 0, 0, 1, 0, 0, 0, 1}) pf32(b, x);  // mat3
    for (int i = 0; i < 3; ++i) pf32(b, 0);  // offset
    for (int i = 0; i < 3; ++i) pf32(b, 0);  // pos
    pf32(b, 0);                              // rot angle
    for (int i = 0; i < 3; ++i) pf32(b, 0);  // rot axis
    for (int i = 0; i < 3; ++i) pf32(b, 1);  // scale
    pi32(b, 3);         // vertex count
    pf32(b, 0); pf32(b, 0); pf32(b, 0);
    pf32(b, 1); pf32(b, 0); pf32(b, 0);
    pf32(b, 0); pf32(b, 1); pf32(b, 0);
    pi32(b, 3);         // tvertex count (color + u + v, v >= 1.2)
    pu32(b, 0xffffffff); pf32(b, 0); pf32(b, 0);
    pu32(b, 0xffffffff); pf32(b, 1); pf32(b, 0);
    pu32(b, 0xffffffff); pf32(b, 0); pf32(b, 1);
    pi32(b, 1);         // face count
    pu16(b, 0); pu16(b, 1); pu16(b, 2);  // vert idx
    pu16(b, 0); pu16(b, 1); pu16(b, 2);  // tvert idx
    pu16(b, 0);         // tex id
    pu16(b, 0);         // padding
    pi32(b, 0);         // two side
    pi32(b, 0);         // smooth group (v >= 1.2)
    // no position keyframes (v < 1.5)
    pi32(b, 0);         // rotation keyframe count

    auto m = Rsm::parse(b);
    CHECK(m.has_value());
    if (!m) return;
    CHECK_EQ(m->version(), 0x104u);
    CHECK_EQ(m->alpha(), 255);
    CHECK_EQ(m->textures().size(), static_cast<usize>(1));
    if (!m->textures().empty()) CHECK(m->textures()[0] == "t.bmp");
    CHECK(m->mainNode() == "root");
    CHECK_EQ(m->nodes().size(), static_cast<usize>(1));
    if (!m->nodes().empty()) {
        const RsmNode& n = m->nodes()[0];
        CHECK(n.name == "root");
        CHECK_EQ(n.vertices.size(), static_cast<usize>(3));
        CHECK_EQ(n.tvertices.size(), static_cast<usize>(3));
        CHECK_EQ(n.faces.size(), static_cast<usize>(1));
        if (!n.faces.empty()) CHECK_EQ(n.faces[0].vertIdx[2], 2);
    }
}

TEST_CASE(rsm_rejects_bad_signature) {
    std::vector<u8> b(64, 0);
    b[0] = 'X';
    CHECK(!Rsm::parse(b).has_value());
}
