#include "formats/UnityFs.hpp"

#include <string>
#include <vector>

#include "core/compress/Lz4.hpp"
#include "microtest.hpp"

using namespace uaro;

namespace {

void putU32be(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x >> 24));
    v.push_back(static_cast<u8>(x >> 16));
    v.push_back(static_cast<u8>(x >> 8));
    v.push_back(static_cast<u8>(x));
}
void putU64be(std::vector<u8>& v, u64 x) {
    putU32be(v, static_cast<u32>(x >> 32));
    putU32be(v, static_cast<u32>(x));
}
void putCstr(std::vector<u8>& v, const char* s) {
    while (*s) v.push_back(static_cast<u8>(*s++));
    v.push_back(0);
}

// A minimal UnityFS v8 bundle with UNCOMPRESSED BlocksInfo + one uncompressed data block
// holding two nodes.
std::vector<u8> buildBundle(const std::string& nodeA, const std::string& nodeB) {
    std::vector<u8> info;
    for (int i = 0; i < 16; ++i) info.push_back(0);  // data hash
    putU32be(info, 1);                               // block count
    putU32be(info, static_cast<u32>(nodeA.size() + nodeB.size()));  // uncomp
    putU32be(info, static_cast<u32>(nodeA.size() + nodeB.size()));  // comp
    info.push_back(0);
    info.push_back(0);  // block flags: method 0
    putU32be(info, 2);  // node count
    putU64be(info, 0);
    putU64be(info, nodeA.size());
    putU32be(info, 4);  // SerializedFile flag
    putCstr(info, "CAB-test");
    putU64be(info, nodeA.size());
    putU64be(info, nodeB.size());
    putU32be(info, 0);
    putCstr(info, "CAB-test.resS");

    std::vector<u8> b;
    putCstr(b, "UnityFS");
    putU32be(b, 8);
    putCstr(b, "5.x.x");
    putCstr(b, "2021.3.21f1");
    putU64be(b, 0);  // total size (unused by the reader)
    putU32be(b, static_cast<u32>(info.size()));
    putU32be(b, static_cast<u32>(info.size()));
    putU32be(b, 0x200);  // method 0 + pad-data flag
    while (b.size() % 16) b.push_back(0);  // v7+ header alignment
    b.insert(b.end(), info.begin(), info.end());
    while (b.size() % 16) b.push_back(0);  // pad-data alignment
    for (char c : nodeA) b.push_back(static_cast<u8>(c));
    for (char c : nodeB) b.push_back(static_cast<u8>(c));
    return b;
}

}  // namespace

TEST_CASE(lz4_block_roundtripish) {
    // Hand-crafted LZ4 stream: 19 zeros as "1 literal + match(off 1, len 18)", then a tail
    // of plain literals — the exact shape a UnityFS BlocksInfo starts with.
    const std::vector<u8> stream = {0x1e, 0x00, 0x01, 0x00,              // 1 lit + 18 @1
                                    0x50, 'H', 'E', 'L', 'L', 'O'};      // 5 literals
    auto out = lz4BlockDecompress(stream.data(), stream.size(), 24);
    CHECK(out.has_value());
    if (out) {
        for (int i = 0; i < 19; ++i) CHECK_EQ((*out)[i], 0);
        CHECK_EQ((*out)[19], static_cast<u8>('H'));
        CHECK_EQ((*out)[23], static_cast<u8>('O'));
    }
    // Wrong declared size / truncated stream fail cleanly.
    CHECK(!lz4BlockDecompress(stream.data(), stream.size(), 25).has_value());
    CHECK(!lz4BlockDecompress(stream.data(), 3, 24).has_value());
}

TEST_CASE(unityfs_container_nodes) {
    const std::string a = "serialized-file-bytes", b = "resource-stream";
    auto bytes = buildBundle(a, b);
    UnityFsBundle u;
    CHECK(u.parse(bytes));
    CHECK_EQ(u.unityVersion(), std::string("2021.3.21f1"));
    CHECK_EQ(u.nodes().size(), static_cast<usize>(2));
    if (u.nodes().size() == 2) {
        CHECK_EQ(u.nodes()[0].path, std::string("CAB-test"));
        CHECK_EQ(u.nodes()[0].flags, 4u);
        CHECK_EQ(u.nodes()[1].path, std::string("CAB-test.resS"));
        auto d0 = u.nodeData(0);
        auto d1 = u.nodeData(1);
        CHECK(d0.has_value());
        CHECK(d1.has_value());
        if (d0) CHECK(std::string(d0->begin(), d0->end()) == a);
        if (d1) CHECK(std::string(d1->begin(), d1->end()) == b);
    }
    // Not a bundle -> clean reject.
    std::vector<u8> junk = {'G', 'R', 'F', 0};
    UnityFsBundle bad;
    CHECK(!bad.parse(junk));
}
