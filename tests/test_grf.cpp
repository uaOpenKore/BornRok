#include "resource/Grf.hpp"
#include "resource/GrfDecrypt.hpp"

#include <cstring>
#include <filesystem>
#include <string>

#include "grf_builder.hpp"
#include "microtest.hpp"

using namespace uaro;

TEST_CASE(grf_open_reads_header_and_entries) {
    auto bytes = testgrf::build_grf({
        {"data\\Hello.txt", "Hello, GRF!"},
        {"data\\sub\\x.bin", std::string("\x01\x02\x03\x04", 4)},
    });
    std::string path = testgrf::write_temp(bytes, "uaro_test_grf_a.grf");

    GrfArchive grf;
    CHECK(grf.open(path));
    CHECK_EQ(grf.version(), 0x200u);
    CHECK_EQ(grf.entries().size(), static_cast<usize>(2));
    CHECK(grf.contains("data/hello.txt"));

    std::filesystem::remove(path);
}

TEST_CASE(grf_v300_reads_int64_offsets) {
    // GRF Editor's v0x300 large-GRF format (int64 table + entry offsets). Built small, but the
    // reader must take the v300 branch: 64-bit header offset, 4-byte table pad, int64 entry offsets.
    auto bytes = testgrf::build_grf_v300({
        {"data\\Hello.txt", "Hello, v300!"},
        {"data\\sub\\y.bin", std::string("\x0a\x0b\x0c\x0d\x0e", 5)},
    });
    std::string path = testgrf::write_temp(bytes, "uaro_test_grf_v300.grf");

    GrfArchive grf;
    CHECK(grf.open(path));
    CHECK_EQ(grf.version(), 0x300u);
    CHECK_EQ(grf.entries().size(), static_cast<usize>(2));
    auto a = grf.read("data/hello.txt");
    CHECK(a.has_value());
    if (a) CHECK(std::string(a->begin(), a->end()) == "Hello, v300!");
    auto b = grf.read("data/sub/y.bin");
    CHECK(b.has_value());
    if (b) CHECK_EQ(b->size(), static_cast<usize>(5));

    std::filesystem::remove(path);
}

TEST_CASE(grf_reads_and_decompresses_file) {
    auto bytes = testgrf::build_grf({{"data\\Hello.txt", "Hello, GRF!"}});
    std::string path = testgrf::write_temp(bytes, "uaro_test_grf_b.grf");

    GrfArchive grf;
    CHECK(grf.open(path));
    auto data = grf.read("data/hello.txt");
    CHECK(data.has_value());
    if (data) CHECK(std::string(data->begin(), data->end()) == "Hello, GRF!");

    std::filesystem::remove(path);
}

TEST_CASE(grf_normalizes_lookup_path) {
    auto bytes = testgrf::build_grf({{"data\\Hello.txt", "Hello, GRF!"}});
    std::string path = testgrf::write_temp(bytes, "uaro_test_grf_c.grf");

    GrfArchive grf;
    CHECK(grf.open(path));
    // Backslashes + mixed case must resolve to the same normalized entry.
    auto a = grf.read("DATA\\HELLO.TXT");
    CHECK(a.has_value());
    auto b = grf.read("data/hello.txt");
    CHECK(b.has_value());

    std::filesystem::remove(path);
}

TEST_CASE(grf_missing_file_returns_nullopt) {
    auto bytes = testgrf::build_grf({{"data\\Hello.txt", "Hello, GRF!"}});
    std::string path = testgrf::write_temp(bytes, "uaro_test_grf_d.grf");

    GrfArchive grf;
    CHECK(grf.open(path));
    CHECK(!grf.read("data/nope.txt").has_value());

    std::filesystem::remove(path);
}

TEST_CASE(grf_reads_binary_payload) {
    std::string payload("\x00\xff\x10\x20\x7f\x80", 6);
    auto bytes = testgrf::build_grf({{"data\\blob.bin", payload}});
    std::string path = testgrf::write_temp(bytes, "uaro_test_grf_e.grf");

    GrfArchive grf;
    CHECK(grf.open(path));
    auto data = grf.read("data/blob.bin");
    CHECK(data.has_value());
    if (data) {
        CHECK_EQ(data->size(), static_cast<usize>(6));
        CHECK(std::string(data->begin(), data->end()) == payload);
    }

    std::filesystem::remove(path);
}

TEST_CASE(zlib_inflate_roundtrip_for_emblem) {
    // A guild emblem usually arrives zlib-deflated; zlibInflate must restore the raw bytes so
    // setGuildEmblem can BMP-decode it (else the emblem is dropped and only the name shows — S.).
    std::string raw = "BM";
    raw.append(300, '\xA5');  // stand-in for a small BMP payload
    const std::vector<u8> comp = testgrf::zdeflate(raw);
    CHECK(comp.size() >= 2u);
    if (comp.size() >= 2u) CHECK_EQ(comp[0], 0x78);  // zlib header byte
    const std::vector<u8> expected(raw.begin(), raw.end());
    auto out = zlibInflate(comp.data(), comp.size(), 64u * 1024u);
    CHECK(out.has_value());
    if (out) CHECK(*out == expected);
    // A ceiling smaller than the output fails cleanly (no overflow / truncation).
    CHECK(!zlibInflate(comp.data(), comp.size(), 8u).has_value());
}

TEST_CASE(grf_v1_legacy_archive_roundtrip) {
    // #106: a synthetic v0x102 archive — DES'd filenames, magic-masked size fields, mixed
    // DES+shuffle data — built with the reader's own encode inverses (eAthena layout).
    // A big payload (>20 DES blocks) exercises the cycle + shuffle region, and a .gat
    // exercises the header-only-DES path.
    std::string big;
    for (int i = 0; i < 3000; ++i) big += static_cast<char>('A' + (i * 7) % 26);
    const std::string gat = "GRAT-like payload for the header-only DES path";
    auto bytes = testgrf::build_grf_v1({
        {"data\\texture\\big.bmp", big},
        {"data\\prontera.gat", gat},
        {"data\\small.txt", "hello"},
    });
    std::string path = testgrf::write_temp(bytes, "uaro_test_grf_v1.grf");

    GrfArchive grf;
    CHECK(grf.open(path));
    CHECK_EQ(grf.version(), 0x102u);
    CHECK_EQ(grf.entries().size(), static_cast<usize>(3));

    auto b = grf.read("data/texture/big.bmp");
    CHECK(b.has_value());
    if (b) CHECK(std::string(b->begin(), b->end()) == big);

    auto g = grf.read("data/prontera.gat");
    CHECK(g.has_value());
    if (g) CHECK(std::string(g->begin(), g->end()) == gat);

    auto s = grf.read("data/small.txt");
    CHECK(s.has_value());
    if (s) CHECK(std::string(s->begin(), s->end()) == std::string("hello"));

    std::filesystem::remove(path);
}

TEST_CASE(grf_v1_des_primitives_roundtrip) {
    // The filename and data codecs must be exact inverses for any byte pattern.
    u8 name[16];
    for (int i = 0; i < 16; ++i) name[i] = static_cast<u8>(i * 37 + 11);
    u8 orig[16];
    std::memcpy(orig, name, 16);
    grf_v1_filename_encode(name, 16);
    CHECK(std::memcmp(orig, name, 16) != 0);  // actually scrambled
    grf_v1_filename_decode(name, 16);
    CHECK(std::memcmp(orig, name, 16) == 0);

    std::vector<u8> data(8 * 64);
    for (usize i = 0; i < data.size(); ++i) data[i] = static_cast<u8>(i * 13 + 7);
    std::vector<u8> ref = data;
    grf_v1_data_encode(data.data(), static_cast<u32>(data.size()), 4);  // mixed scheme
    CHECK(data != ref);
    grf_v1_data_decode(data.data(), static_cast<u32>(data.size()), 4);
    CHECK(data == ref);
    grf_v1_data_encode(data.data(), static_cast<u32>(data.size()), 0);  // header-only
    grf_v1_data_decode(data.data(), static_cast<u32>(data.size()), 0);
    CHECK(data == ref);
}
