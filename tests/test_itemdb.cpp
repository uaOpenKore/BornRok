// Unit tests for ItemDb::parseTable — the pure parser behind the shop's item names
// and icon resnames (idnum2itemdisplaynametable.txt / idnum2itemresnametable.txt).
#include "microtest.hpp"

#include <string>
#include <vector>

#include "resource/ItemDb.hpp"

using namespace uaro;

static std::vector<u8> bytes(const std::string& s) { return {s.begin(), s.end()}; }

TEST_CASE(itemdb_parse_basic_crlf_and_underscore) {
    // Real table shape: a '//' header then "id#Name#" rows with CRLF endings; the
    // display parser turns '_' into spaces.
    auto m = ItemDb::parseTable(bytes("//file start\r\n501#Red_Potion#\r\n1201#Knife#\r\n"), true);
    CHECK_EQ(m.size(), 2u);
    CHECK_EQ(m[501], std::string("Red Potion"));
    CHECK_EQ(m[1201], std::string("Knife"));
}

TEST_CASE(itemdb_parse_resnames_preserve_bytes) {
    // Resource names keep their raw bytes (no underscore substitution) so EUC-KR icon
    // paths survive verbatim.
    auto m = ItemDb::parseTable(bytes("501#a_b\xb1\xcd#\n"), false);
    CHECK_EQ(m[501], std::string("a_b\xb1\xcd"));
}

TEST_CASE(itemdb_parse_skips_malformed_and_comments) {
    auto m = ItemDb::parseTable(bytes("// comment\n\nnotanumber#x#\n777#Good#\n888#noclose\n"), true);
    CHECK_EQ(m.size(), 1u);
    CHECK_EQ(m[777], std::string("Good"));
    CHECK(m.find(888) == m.end());  // missing second '#': dropped
    CHECK(m.find(0) == m.end());    // "notanumber" line: dropped
}

TEST_CASE(itemdb_parse_last_line_without_newline) {
    auto m = ItemDb::parseTable(bytes("12#Apple#"), true);
    CHECK_EQ(m.size(), 1u);
    CHECK_EQ(m[12], std::string("Apple"));
}

TEST_CASE(itemdb_parse_empty_value) {
    // An empty name ("id##") is stored as empty; ItemDb::name() then falls back to "#id".
    auto m = ItemDb::parseTable(bytes("5##\n"), true);
    CHECK_EQ(m.size(), 1u);
    CHECK_EQ(m[5], std::string(""));
}

TEST_CASE(itemdb_parse_desc_table) {
    // Multi-line blocks: "<id>#", description lines, then a "#" line. CRLF + a leading
    // comment are tolerated; lines join with '\n' and colour codes are kept verbatim.
    auto m = ItemDb::parseDescTable(
        bytes("//file start\r\n501#\r\nRed Potion\r\n^000088heals 45 HP^000000\r\n#\r\n"
              "502#\r\nOrange Potion\r\n#\r\n"));
    CHECK_EQ(m.size(), 2u);
    CHECK_EQ(m[501], std::string("Red Potion\n^000088heals 45 HP^000000"));
    CHECK_EQ(m[502], std::string("Orange Potion"));
}
