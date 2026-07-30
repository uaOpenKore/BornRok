#include "patcher/ContentQuality.hpp"
#include "patcher/GDriveUrl.hpp"
#include "patcher/PatchManifest.hpp"
#include "patcher/PatcherConfig.hpp"
#include "patcher/PlatformId.hpp"

#include "microtest.hpp"

using namespace uaro;

TEST_CASE(manifest_parse_basic) {
    const std::string txt =
        "# comment\n"
        "// also comment\n"
        "\n"
        "platforms/win-x64/uaro_client.exe abcdef0123 https://drive/uc?id=1\n"
        "root/data.grf 99AABB https://drive/uc?id=2\n"
        "root/BGM/1.mp3 cafe\n";  // mirror optional
    auto e = PatchManifest::parse(txt);
    CHECK(e.size() == 3);

    CHECK(e[0].platform == "win-x64");
    CHECK(e[0].dest == "uaro_client.exe");
    CHECK(e[0].sha512 == "abcdef0123");
    CHECK(e[0].mirror == "https://drive/uc?id=1");

    CHECK(e[1].platform == "");          // root/ = any platform
    CHECK(e[1].dest == "data.grf");
    CHECK(e[1].sha512 == "99aabb");      // lowercased

    CHECK(e[2].dest == "BGM/1.mp3");
    CHECK(e[2].mirror == "");            // no mirror field
}

TEST_CASE(manifest_skips_malformed) {
    const std::string txt =
        "garbage/path/x sha url\n"   // unknown prefix -> skipped
        "root/onlyonefield\n"        // < 2 fields -> skipped
        "root/ok deadbeef url\n";
    auto e = PatchManifest::parse(txt);
    CHECK(e.size() == 1);
    CHECK(e[0].dest == "ok");
}

TEST_CASE(manifest_for_platform_filters) {
    const std::string txt =
        "platforms/win-x64/a.exe h1 u1\n"
        "platforms/ubuntu-x64/a h2 u2\n"
        "root/shared h3 u3\n";
    auto all = PatchManifest::parse(txt);

    auto win = PatchManifest::forPlatform(all, "win-x64");
    CHECK(win.size() == 2);  // win-x64 + root
    CHECK(win[0].platform == "win-x64");
    CHECK(win[1].platform == "");

    auto unknown = PatchManifest::forPlatform(all, "");  // unknown OS -> root only
    CHECK(unknown.size() == 1);
    CHECK(unknown[0].dest == "shared");
}

TEST_CASE(gdrive_url_id) {
    CHECK(gdriveFileId("https://drive.google.com/uc?export=download&id=18t4sQjNBOWPONZ") ==
          "18t4sQjNBOWPONZ");
    CHECK(gdriveFileId("https://drive.google.com/file/d/1z4yNmI1EQZA6n/view?usp=sharing") ==
          "1z4yNmI1EQZA6n");
    CHECK(gdriveFileId("https://drive.usercontent.google.com/download?id=ABC-_123&export=download") ==
          "ABC-_123");
    CHECK(gdriveFileId("https://drive.google.com/open?id=XyZ9") == "XyZ9");
    CHECK(gdriveFileId("https://example.com/no-id-here") == "");
    CHECK(gdriveDirectUrl("ABC123") ==
          "https://drive.usercontent.google.com/download?id=ABC123&export=download&confirm=t");
    CHECK(gdriveDirectUrl("") == "");
}

TEST_CASE(patcher_cfg_parse) {
    auto cfg = PatcherConfig::parse(
        "# comment\n"
        "  http://a/downloads.list  \n"
        "\n"
        "https://b/downloads.list\n");
    CHECK(cfg.manifestUrls.size() == 2);
    CHECK(cfg.manifestUrls[0] == "http://a/downloads.list");
    CHECK(cfg.manifestUrls[1] == "https://b/downloads.list");

    auto def = PatcherConfig::defaults();
    CHECK(def.manifestUrls.size() == 3);  // GDrive primary + http + https patcher.bornrok.com (2026-07-08)
    // Round-trip: serialize -> parse keeps the URLs.
    CHECK(PatcherConfig::parse(def.serialize()).manifestUrls == def.manifestUrls);
}

TEST_CASE(manifest_server_fallback_url) {
    CHECK(serverFileUrl("http://patcher.bornrok.com/downloads.list", "root/data.grf") ==
          "http://patcher.bornrok.com/root/data.grf");
    CHECK(serverFileUrl("https://host/sub/downloads.list", "platforms/win-x64/uaro_client.exe") ==
          "https://host/sub/platforms/win-x64/uaro_client.exe");
}

TEST_CASE(content_quality_defaults) {
    // 4k tier.
    CHECK(defaultQuality("windows-x64") == "4k");
    CHECK(defaultQuality("linux-x64") == "4k");
    CHECK(defaultQuality("macos-x64") == "4k");
    CHECK(defaultQuality("macos-arm") == "4k");
    CHECK(defaultQuality("xbox") == "4k");
    CHECK(defaultQuality("ps5") == "4k");
    // 1k tier (incl. the former 2k platforms and steamdeck/switch/mobile).
    CHECK(defaultQuality("windows-arm") == "1k");
    CHECK(defaultQuality("linux-arm") == "1k");
    CHECK(defaultQuality("android-x64") == "1k");
    CHECK(defaultQuality("android-arm") == "1k");
    CHECK(defaultQuality("steamdeck") == "1k");
    CHECK(defaultQuality("switch") == "1k");
    CHECK(defaultQuality("iphone") == "1k");
    CHECK(defaultQuality("") == "1k");        // unknown -> 1k
}

TEST_CASE(content_quality_bundled) {
    CHECK(isBundledPlatform("xbox"));
    CHECK(isBundledPlatform("ps4"));
    CHECK(isBundledPlatform("ps5"));
    CHECK(isBundledPlatform("switch"));
    CHECK(!isBundledPlatform("iphone"));       // iOS DOES download content (root + 4k/2k/1k) — S. 2026-07-24
    CHECK(!isBundledPlatform("ipad"));
    CHECK(!isBundledPlatform("android-arm"));  // Android DOES download content
    CHECK(!isBundledPlatform("android-x64"));
    CHECK(!isBundledPlatform("windows-x64"));
    CHECK(!isBundledPlatform("steamdeck"));   // Deck is NOT bundled (downloads content)
    CHECK(!isBundledPlatform("macos-arm"));   // desktop mac downloads content (not a store bundle)
    CHECK(!isBundledPlatform(""));
}

TEST_CASE(content_quality_resolve) {
    CHECK(resolveQuality("4k", "linux-arm") == "4k");  // explicit config wins over the 1k default
    CHECK(resolveQuality("1k", "windows-x64") == "1k");
    CHECK(resolveQuality("", "windows-x64") == "4k");  // empty -> platform default
    CHECK(resolveQuality("2k", "windows-x64") == "4k");  // legacy 2k ignored -> default
    CHECK(resolveQuality("garbage", "switch") == "1k");
}

TEST_CASE(manifest_quality_and_events_categories) {
    const std::string txt =
        "4k/texture.zip aa mirror4t\n"
        "4k/sprite.zip bb mirror4s\n"
        "1k/texture.zip cc mirror1t\n"
        "1k/sprite.zip dd mirror1s\n"
        "events/addons.zip ee mirrorE\n"
        "root/data.zip ff mirrorR\n"
        "platforms/win-x64/UaRO.exe 11 mirrorW\n";
    auto all = PatchManifest::parse(txt);
    CHECK(all.size() == 7);
    // Quality entries: dest drops the quality dir, kind derived from the name.
    CHECK(all[0].category == PatchCat::Quality);
    CHECK(all[0].quality == "4k");
    CHECK(all[0].kind == "texture");
    CHECK(all[0].dest == "texture.zip");   // 1k & 4k share this local file
    CHECK(all[1].kind == "sprite");
    CHECK(all[4].category == PatchCat::Events);
    CHECK(all[4].dest == "events/addons.zip");
    CHECK(all[5].category == PatchCat::Root);
    CHECK(all[6].category == PatchCat::Platform);
}

TEST_CASE(manifest_select_desktop_mixed_quality) {
    const std::string txt =
        "4k/texture.zip aa m\n4k/sprite.zip bb m\n1k/texture.zip cc m\n1k/sprite.zip dd m\n"
        "events/addons.zip ee m\nevents/uaro.zip ef m\n"
        "root/data.zip ff m\nplatforms/win-x64/UaRO.exe 11 m\nplatforms/linux-x64/UaRO 22 m\n";
    auto all = PatchManifest::parse(txt);
    // Desktop win-x64, 4k texture + 1k sprite (sprites not ready in 4k yet).
    ContentSelection sel{"win-x64", false, false, "4k", "1k"};
    auto got = PatchManifest::selectForContent(all, sel);
    // root + win-x64 platform + 2 events + 4k texture + 1k sprite = 6; linux binary + the other two
    // quality packs excluded.
    CHECK(got.size() == 6);
    int tex = 0, spr = 0, evt = 0, plat = 0, root = 0;
    for (auto& e : got) {
        if (e.kind == "texture") { ++tex; CHECK(e.quality == "4k"); }
        if (e.kind == "sprite") { ++spr; CHECK(e.quality == "1k"); }
        if (e.category == PatchCat::Events) ++evt;
        if (e.category == PatchCat::Platform) { ++plat; CHECK(e.platform == "win-x64"); }
        if (e.category == PatchCat::Root) ++root;
    }
    CHECK(tex == 1); CHECK(spr == 1); CHECK(evt == 2); CHECK(plat == 1); CHECK(root == 1);
}

TEST_CASE(manifest_select_bundled_events_only) {
    const std::string txt =
        "4k/texture.zip aa m\n4k/sprite.zip bb m\n1k/texture.zip cc m\n1k/sprite.zip dd m\n"
        "events/addons.zip ee m\nevents/uaro.zip ef m\n"
        "root/data.zip ff m\nplatforms/xbox-x64/pkg.zip 11 m\n";
    auto all = PatchManifest::parse(txt);
    // A bundled console (base content baked, quality irrelevant) -> ONLY the two event packs.
    ContentSelection sel{"", true, false, "4k", "4k"};
    auto got = PatchManifest::selectForContent(all, sel);
    CHECK(got.size() == 2);
    for (auto& e : got) CHECK(e.category == PatchCat::Events);
}

TEST_CASE(manifest_select_mobile_skips_platform_keeps_content) {
    // iOS/Android: the binary ships in the store package -> skip platforms/, but STILL download
    // root/ + the chosen quality packs + events (S. 2026-07-24).
    const std::string txt =
        "4k/texture.zip aa m\n1k/texture.zip cc m\n1k/sprite.zip dd m\n"
        "events/addons.zip ee m\n"
        "root/data.zip ff m\nplatforms/android-arm/libmain.so 11 m\n";
    auto all = PatchManifest::parse(txt);
    ContentSelection sel{"android-arm", /*bundled*/ false, /*skipPlatformBinary*/ true, "1k", "1k"};
    auto got = PatchManifest::selectForContent(all, sel);
    // root + 1k texture + 1k sprite + 1 event = 4; the android platform binary is NOT included.
    CHECK(got.size() == 4);
    int plat = 0, root = 0, evt = 0, q = 0;
    for (auto& e : got) {
        if (e.category == PatchCat::Platform) ++plat;
        if (e.category == PatchCat::Root) ++root;
        if (e.category == PatchCat::Events) ++evt;
        if (e.category == PatchCat::Quality) { ++q; CHECK(e.quality == "1k"); }
    }
    CHECK(plat == 0); CHECK(root == 1); CHECK(evt == 1); CHECK(q == 2);
}

TEST_CASE(content_quality_mobile_store) {
    CHECK(isMobileStorePlatform("iphone"));
    CHECK(isMobileStorePlatform("ipad"));
    CHECK(isMobileStorePlatform("android-arm"));
    CHECK(isMobileStorePlatform("android-x64"));
    CHECK(!isMobileStorePlatform("windows-x64"));
    CHECK(!isMobileStorePlatform("macos-arm"));
    CHECK(!isMobileStorePlatform("xbox"));   // console = bundled, not the mobile-store category
    CHECK(!isMobileStorePlatform(""));
}

TEST_CASE(platform_osrelease_parse) {
    CHECK(linuxDistroFromOsRelease("NAME=\"Ubuntu\"\nID=ubuntu\nVERSION_ID=\"24.04\"\n") == "ubuntu");
    CHECK(linuxDistroFromOsRelease("ID=debian\n") == "debian");
    CHECK(linuxDistroFromOsRelease("ID='fedora'\n") == "fedora");
    CHECK(linuxDistroFromOsRelease("ID=steamos\nID_LIKE=arch\n") == "steamos");
    // Unknown distro but a known parent in ID_LIKE.
    CHECK(linuxDistroFromOsRelease("ID=linuxmint\nID_LIKE=\"ubuntu debian\"\n") == "ubuntu");
    // Truly unknown -> empty.
    CHECK(linuxDistroFromOsRelease("ID=arch\n") == "");
}
