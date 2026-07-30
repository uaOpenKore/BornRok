#include "world/ModelMesh.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "formats/Rsm.hpp"
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

// Minimal v1.4 RSM: one node, three vertices/tvertices, one face.
std::vector<u8> makeRsm() {
    std::vector<u8> b{'G', 'R', 'S', 'M'};
    pu8(b, 1); pu8(b, 4);
    pi32(b, 0); pi32(b, 2);  // anim length, shade type
    pu8(b, 255);             // alpha
    pad(b, 16);
    pi32(b, 1);              // texture count
    pname(b, "t.bmp", 40);
    pname(b, "root", 40);    // main node
    pi32(b, 1);              // node count
    pname(b, "root", 40);
    pname(b, "", 40);
    pi32(b, 1); pi32(b, 0);  // node texture count, index 0
    for (float x : {1, 0, 0, 0, 1, 0, 0, 0, 1}) pf32(b, x);
    for (int i = 0; i < 3; ++i) pf32(b, 0);  // offset
    for (int i = 0; i < 3; ++i) pf32(b, 0);  // pos
    pf32(b, 0);
    for (int i = 0; i < 3; ++i) pf32(b, 0);  // rot axis
    for (int i = 0; i < 3; ++i) pf32(b, 1);  // scale
    pi32(b, 3);              // vertices
    pf32(b, 0); pf32(b, 0); pf32(b, 0);
    pf32(b, 1); pf32(b, 0); pf32(b, 0);
    pf32(b, 0); pf32(b, 1); pf32(b, 0);
    pi32(b, 3);              // tvertices (color + u + v)
    pu32(b, 0xffffffff); pf32(b, 0); pf32(b, 0);
    pu32(b, 0xffffffff); pf32(b, 1); pf32(b, 0);
    pu32(b, 0xffffffff); pf32(b, 0); pf32(b, 1);
    pi32(b, 1);              // faces
    pu16(b, 0); pu16(b, 1); pu16(b, 2);
    pu16(b, 0); pu16(b, 1); pu16(b, 2);
    pu16(b, 0); pu16(b, 0);
    pi32(b, 0); pi32(b, 0);  // two side, smooth group
    pi32(b, 0);              // rotation keyframes
    return b;
}
} // namespace

TEST_CASE(modelmesh_triangulates_node) {
    auto bytes = makeRsm();
    auto rsm = Rsm::parse(bytes);
    CHECK(rsm.has_value());
    if (!rsm) return;

    ModelMesh mesh = ModelMesh::build(*rsm);
    CHECK_EQ(mesh.nodes.size(), static_cast<usize>(1));
    if (mesh.nodes.empty()) return;
    const ModelNodeMesh& nm = mesh.nodes[0];
    CHECK_EQ(nm.nodeIndex, 0u);
    CHECK_EQ(nm.vertices.size(), static_cast<usize>(3));
    CHECK_EQ(nm.indices.size(), static_cast<usize>(3));
    CHECK_EQ(mesh.totalIndices(), static_cast<usize>(3));
    CHECK_EQ(nm.batches.size(), static_cast<usize>(1));
    if (!nm.batches.empty()) {
        CHECK_EQ(nm.batches[0].textureId, 0);  // node texture index 0
        CHECK_EQ(nm.batches[0].indexCount, 3u);
    }
    // second vertex sits at (1,0,0) with uv (1,0)
    CHECK_NEAR(nm.vertices[1].x, 1.0, 1e-5);
    CHECK_NEAR(nm.vertices[1].u, 1.0, 1e-5);
    CHECK_NEAR(nm.vertices[2].v, 1.0, 1e-5);
}
