#include "resource/Vfs.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "grf_builder.hpp"
#include "microtest.hpp"

using namespace uaro;
namespace stdfs = std::filesystem;

namespace {
std::string to_str(const std::vector<u8>& v) { return std::string(v.begin(), v.end()); }

void write_text(const stdfs::path& p, const std::string& s) {
    stdfs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    o << s;
}
} // namespace

TEST_CASE(vfs_loose_dir_overrides_grf) {
    auto grfBytes = testgrf::build_grf({{"data\\a.txt", "from_grf"}, {"data\\b.txt", "grf_b"}});
    stdfs::path tmp = stdfs::temp_directory_path() / "uaro_vfs_override";
    stdfs::remove_all(tmp);
    stdfs::create_directories(tmp);
    std::string grfPath = (tmp / "g.grf").string();
    testgrf::write_file(grfPath, grfBytes);
    write_text(tmp / "loose" / "data" / "a.txt", "from_loose");

    Vfs vfs;
    CHECK(vfs.mountGrf(grfPath));
    vfs.mountDir((tmp / "loose").string());

    auto a = vfs.read("data/a.txt");
    CHECK(a.has_value());
    if (a) CHECK(to_str(*a) == "from_loose");  // loose wins
    auto b = vfs.read("data/b.txt");
    CHECK(b.has_value());
    if (b) CHECK(to_str(*b) == "grf_b");  // only in GRF
    CHECK(!vfs.read("data/missing").has_value());

    stdfs::remove_all(tmp);
}

TEST_CASE(vfs_data_prefix_optional) {
    // A content archive/folder may drop the mandatory "data/" prefix (S.: "texture.zip без data").
    // A request for "data/texture/foo" must also find an entry stored as just "texture/foo", while
    // the canonical "data/"-prefixed entry still wins when both exist.
    auto grf = testgrf::build_grf({{"texture\\loose_leaf.png", "no_data_prefix"},
                                   {"data\\texture\\both.png", "with_data"},
                                   {"texture\\both.png", "without_data"}});
    stdfs::path tmp = stdfs::temp_directory_path() / "uaro_vfs_dataless";
    stdfs::remove_all(tmp);
    stdfs::create_directories(tmp);
    const std::string grfPath = (tmp / "g.grf").string();
    testgrf::write_file(grfPath, grf);

    Vfs vfs;
    CHECK(vfs.mountGrf(grfPath));

    auto leaf = vfs.read("data/texture/loose_leaf.png");  // requested with data/, stored without
    CHECK(leaf.has_value());
    if (leaf) CHECK(to_str(*leaf) == "no_data_prefix");

    auto both = vfs.read("data/texture/both.png");  // both forms exist -> the data/ one wins
    CHECK(both.has_value());
    if (both) CHECK(to_str(*both) == "with_data");

    stdfs::remove_all(tmp);
}

TEST_CASE(vfs_first_mounted_grf_wins) {
    auto g0 = testgrf::build_grf({{"data\\x", "from_g0"}});
    auto g1 = testgrf::build_grf({{"data\\x", "from_g1"}});
    stdfs::path tmp = stdfs::temp_directory_path() / "uaro_vfs_order";
    stdfs::remove_all(tmp);
    stdfs::create_directories(tmp);
    testgrf::write_file((tmp / "g0.grf").string(), g0);
    testgrf::write_file((tmp / "g1.grf").string(), g1);

    Vfs vfs;
    vfs.mountGrf((tmp / "g0.grf").string());
    vfs.mountGrf((tmp / "g1.grf").string());

    auto x = vfs.read("data/x");
    CHECK(x.has_value());
    if (x) CHECK(to_str(*x) == "from_g0");  // first mounted wins

    stdfs::remove_all(tmp);
}

TEST_CASE(vfs_mount_data_ini_respects_order) {
    auto gNew = testgrf::build_grf({{"data\\common", "new"}});
    auto gData = testgrf::build_grf({{"data\\common", "old"}, {"data\\only", "x"}});
    stdfs::path tmp = stdfs::temp_directory_path() / "uaro_vfs_ini";
    stdfs::remove_all(tmp);
    stdfs::create_directories(tmp);
    testgrf::write_file((tmp / "new.grf").string(), gNew);
    testgrf::write_file((tmp / "data.grf").string(), gData);

    const char* ini = "[Data]\n0=new.grf\n2=data.grf\n";
    Vfs vfs;
    int mounted = vfs.mountDataIni(ini, tmp.string());
    CHECK_EQ(mounted, 2);

    auto common = vfs.read("data/common");
    CHECK(common.has_value());
    if (common) CHECK(to_str(*common) == "new");  // new.grf (index 0) overrides data.grf
    CHECK(vfs.read("data/only").has_value());

    stdfs::remove_all(tmp);
}
