#include "core/io/ByteBuffer.hpp"

#include <stdexcept>

#include "microtest.hpp"

using namespace uaro;

TEST_CASE(bytereader_reads_little_endian) {
    const u8 data[] = {0x01, 0x00, 0x00, 0x00, 0x34, 0x12};
    ByteReader r(data, sizeof(data));
    CHECK_EQ(r.u32le(), 1u);
    CHECK_EQ(r.u16le(), 0x1234u);
    CHECK(r.eof());
    CHECK_EQ(r.remaining(), 0u);
}

TEST_CASE(bytereader_big_endian_swap) {
    const u8 data[] = {0x12, 0x34, 0x56, 0x78};
    ByteReader r(data, sizeof(data));
    CHECK_EQ(r.read<u32>(Endian::Big), 0x12345678u);
}

TEST_CASE(bytereader_cstring_stops_at_nul) {
    const u8 data[] = {'A', 'B', 'C', 0, 0, 0};
    ByteReader r(data, sizeof(data));
    CHECK(r.read_cstring(6) == "ABC");
    CHECK_EQ(r.pos(), 6u);  // advances full field width
    CHECK(r.eof());
}

TEST_CASE(bytereader_overflow_throws) {
    const u8 data[] = {0x01, 0x02};
    ByteReader r(data, sizeof(data));
    bool threw = false;
    try {
        r.u32le();
    } catch (const std::out_of_range&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE(bytewriter_reader_roundtrip) {
    ByteWriter w;
    w.u32le(0xDEADBEEF);
    w.u16le(0x0102);
    w.write_string("hi");
    CHECK_EQ(w.size(), 8u);

    ByteReader r(w.data());
    CHECK_EQ(r.u32le(), 0xDEADBEEFu);
    CHECK_EQ(r.u16le(), 0x0102u);
    CHECK(r.read_string(2) == "hi");
}

TEST_CASE(bytereader_seek_skip) {
    const u8 data[] = {0, 1, 2, 3, 4, 5};
    ByteReader r(data, sizeof(data));
    r.skip(2);
    CHECK_EQ(r.u8v(), 2);
    r.seek(5);
    CHECK_EQ(r.u8v(), 5);
}
