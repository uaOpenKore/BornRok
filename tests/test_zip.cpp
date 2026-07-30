#include "resource/Zip.hpp"

#include <filesystem>
#include <string>

#include "grf_builder.hpp"  // testgrf::build_grf + write_file (for the mixed-mount test)
#include "microtest.hpp"
#include "resource/Vfs.hpp"
#include "zip_builder.hpp"

using namespace uaro;
namespace stdfs = std::filesystem;

namespace {
std::string to_str(const std::vector<u8>& v) { return std::string(v.begin(), v.end()); }
}  // namespace

TEST_CASE(zip_reads_stored_and_deflated) {
    // A stored entry, a deflated entry, and a subdir path with backslash-free names.
    auto bytes = testzip::build_zip({
        {"data/a.txt", "stored-content", false},
        {"data/sub/b.txt", std::string(500, 'x') + "deflate-me", true},
    });
    stdfs::path tmp = stdfs::temp_directory_path() / "uaro_zip_basic";
    stdfs::remove_all(tmp);
    stdfs::create_directories(tmp);
    const std::string zp = (tmp / "a.zip").string();
    testgrf::write_file(zp, bytes);

    ZipArchive z;
    CHECK(z.open(zp));
    CHECK_EQ(z.entries().size(), 2u);

    auto a = z.read("data/a.txt");
    CHECK(a.has_value());
    if (a) CHECK(to_str(*a) == "stored-content");

    auto b = z.read("data/sub/b.txt");
    CHECK(b.has_value());
    if (b) CHECK(to_str(*b) == std::string(500, 'x') + "deflate-me");  // inflated correctly

    // Case-insensitive lookup (paths are normalized to lowercase like GRF vpaths).
    CHECK(z.read("DATA/A.TXT").has_value());
    CHECK(!z.read("data/missing").has_value());

    stdfs::remove_all(tmp);
}

TEST_CASE(vfs_zip_overrides_grf_but_loose_wins) {
    // GRF has a+b; zip overrides a and adds c; loose dir overrides a again.
    auto grf = testgrf::build_grf({{"data\\a", "from_grf"}, {"data\\b", "grf_b"}});
    auto zip = testzip::build_zip({{"data/a", "from_zip", true}, {"data/c", "zip_c", false}});
    stdfs::path tmp = stdfs::temp_directory_path() / "uaro_vfs_zip";
    stdfs::remove_all(tmp);
    stdfs::create_directories(tmp);
    testgrf::write_file((tmp / "g.grf").string(), grf);
    testgrf::write_file((tmp / "p.zip").string(), zip);

    Vfs vfs;
    CHECK(vfs.mountGrf((tmp / "g.grf").string()));
    CHECK(vfs.mountZip((tmp / "p.zip").string()));
    CHECK_EQ(vfs.zipCount(), 1u);

    auto a = vfs.read("data/a");
    CHECK(a.has_value());
    if (a) CHECK(to_str(*a) == "from_zip");  // zip shadows the GRF
    auto b = vfs.read("data/b");
    CHECK(b.has_value());
    if (b) CHECK(to_str(*b) == "grf_b");     // GRF-only survives
    auto c = vfs.read("data/c");
    CHECK(c.has_value());
    if (c) CHECK(to_str(*c) == "zip_c");     // zip-only visible

    // Loose dir still outranks the zip.
    stdfs::create_directories(tmp / "loose" / "data");
    testgrf::write_file((tmp / "loose" / "data" / "a").string(),
                        {'l', 'o', 'o', 's', 'e'});
    vfs.mountDir((tmp / "loose").string());
    auto a2 = vfs.read("data/a");
    CHECK(a2.has_value());
    if (a2) CHECK(to_str(*a2) == "loose");

    stdfs::remove_all(tmp);
}

TEST_CASE(zip_reads_zip64_structures) {
    // Zip64 EOCD + locator + per-entry Zip64 extra field (the >4 GiB / >65535-entry format
    // the multi-GB RoM pack needs). Built small but with the Zip64 records forced on.
    auto bytes = testzip::build_zip64({
        {"data/z64a.txt", "zip64-entry-a"},
        {"data/deep/z64b.txt", "zip64-entry-b-content"},
    });
    stdfs::path tmp = stdfs::temp_directory_path() / "uaro_zip64";
    stdfs::remove_all(tmp);
    stdfs::create_directories(tmp);
    const std::string zp = (tmp / "z64.zip").string();
    testgrf::write_file(zp, bytes);

    ZipArchive z;
    CHECK(z.open(zp));  // must follow the EOCD64 chain, not choke on the sentinels
    CHECK_EQ(z.entries().size(), 2u);
    auto a = z.read("data/z64a.txt");
    CHECK(a.has_value());
    if (a) CHECK(to_str(*a) == "zip64-entry-a");  // resolved the 64-bit local offset from extra
    auto b = z.read("data/deep/z64b.txt");
    CHECK(b.has_value());
    if (b) CHECK(to_str(*b) == "zip64-entry-b-content");

    stdfs::remove_all(tmp);
}

TEST_CASE(zip_rejects_non_zip) {
    stdfs::path tmp = stdfs::temp_directory_path() / "uaro_zip_bad";
    stdfs::remove_all(tmp);
    stdfs::create_directories(tmp);
    const std::string bp = (tmp / "bad.zip").string();
    testgrf::write_file(bp, {'n', 'o', 't', 'a', 'z', 'i', 'p'});
    ZipArchive z;
    CHECK(!z.open(bp));  // no EOCD -> open fails, no crash
    stdfs::remove_all(tmp);
}
