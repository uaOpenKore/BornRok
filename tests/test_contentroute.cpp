#include "resource/ContentRoute.hpp"
#include "resource/Vfs.hpp"

#include <filesystem>
#include <string>

#include "grf_builder.hpp"
#include "microtest.hpp"

using namespace uaro;

TEST_CASE(contentroute_categorize) {
    // Korean folder names as stored in the GRF (EUC-KR) + their English aliases.
    CHECK(categorizeVpath("data/wav/attack.wav") == ContentCategory::Sfx);
    CHECK(categorizeVpath("bgm/01.mp3") == ContentCategory::Bgm);
    CHECK(categorizeVpath("data/sprite/\xb8\xf3\xbd\xba\xc5\xcd/poring.spr") ==
          ContentCategory::Mobs);
    CHECK(categorizeVpath("data/sprite/monster/poring.spr") == ContentCategory::Mobs);
    CHECK(categorizeVpath("data/sprite/homun/lif.spr") == ContentCategory::Mobs);
    CHECK(categorizeVpath("data/sprite/npc/kafra_01.spr") == ContentCategory::Npc);
    CHECK(categorizeVpath("data/sprite/\xc0\xce\xb0\xa3\xc1\xb7/\xb8\xf6\xc5\xeb/x.spr") ==
          ContentCategory::Chars);
    CHECK(categorizeVpath("data/sprite/human/body/x.spr") == ContentCategory::Chars);
    CHECK(categorizeVpath("data/sprite/accessory/man/hat.spr") == ContentCategory::Chars);
    CHECK(categorizeVpath("data/palette/\xb8\xf6/job_m_1.pal") == ContentCategory::Chars);
    CHECK(categorizeVpath("data/sprite/\xc0\xcc\xc6\xd1\xc6\xae/torch_01.spr") ==
          ContentCategory::Effects);
    CHECK(categorizeVpath("data/texture/effect/magnum.str") == ContentCategory::Effects);
    CHECK(categorizeVpath("data/texture/effect/i_concentration.tga") ==
          ContentCategory::Statuses);
    CHECK(categorizeVpath(
              "data/texture/\xc0\xaf\xc0\xfa\xc0\xce\xc5\xcd\xc6\xe4\xc0\xcc\xbd\xba/item/"
              "mg_firebolt.bmp") == ContentCategory::Skills);
    CHECK(categorizeVpath("data/prontera.rsw") == ContentCategory::Other);
    CHECK(categorizeVpath("data/texture/ground/gray.bmp") == ContentCategory::Other);
}

TEST_CASE(contentroute_chains) {
    std::array<ContentSource, 3> c{};
    CHECK_EQ(sourceChain(ContentSource::Gro, c), static_cast<usize>(1));
    CHECK(c[0] == ContentSource::Gro);
    CHECK_EQ(sourceChain(ContentSource::Uaro, c), static_cast<usize>(2));
    CHECK(c[0] == ContentSource::Uaro);
    CHECK(c[1] == ContentSource::Gro);
    CHECK_EQ(sourceChain(ContentSource::Rom, c), static_cast<usize>(3));
    CHECK(c[0] == ContentSource::Rom);
    CHECK(c[1] == ContentSource::Uaro);
    CHECK(c[2] == ContentSource::Gro);
}

TEST_CASE(contentroute_vfs_routing) {
    // Two archives carrying the SAME mob path with different payloads: "uaro" mounted first
    // (higher raw priority), "gro" second. Category modes must beat mount order.
    const std::string mob = "data\\sprite\\monster\\poring.spr";
    auto uaroBytes = testgrf::build_grf({{mob, "UARO-PORING"}, {"data\\uaro_only.txt", "u"}});
    auto groBytes = testgrf::build_grf({{mob, "GRO-PORING"}, {"data\\gro_only.txt", "g"}});
    auto p1 = testgrf::write_temp(uaroBytes, "uaro_test_cs_uaro.grf");
    auto p2 = testgrf::write_temp(groBytes, "uaro_test_cs_gro.grf");

    Vfs vfs;
    CHECK(vfs.mountGrf(p1, ContentSource::Uaro));
    CHECK(vfs.mountGrf(p2, ContentSource::Gro));
    CHECK(vfs.hasSource(ContentSource::Uaro));
    CHECK(vfs.hasSource(ContentSource::Gro));
    CHECK(!vfs.hasSource(ContentSource::Rom));

    // Default (UaRO mode): our archive wins, GRO fills the gaps.
    auto d = vfs.read("data/sprite/monster/poring.spr");
    CHECK(d.has_value());
    if (d) CHECK(std::string(d->begin(), d->end()) == "UARO-PORING");
    CHECK(vfs.read("data/gro_only.txt").has_value());  // GRO fallback works

    // Mobs switched to GRO: the official file now wins for the mob path only.
    vfs.setContentMode(ContentCategory::Mobs, ContentSource::Gro);
    d = vfs.read("data/sprite/monster/poring.spr");
    CHECK(d.has_value());
    if (d) CHECK(std::string(d->begin(), d->end()) == "GRO-PORING");
    // Non-mob paths still resolve through UaRO first.
    CHECK(vfs.read("data/uaro_only.txt").has_value());

    // GRO mode does NOT fall back to UaRO: a mob file only present in UaRO disappears.
    vfs.setContentMode(ContentCategory::Mobs, ContentSource::Gro);
    Vfs solo;
    CHECK(solo.mountGrf(p1, ContentSource::Uaro));
    solo.setContentMode(ContentCategory::Mobs, ContentSource::Gro);
    CHECK(!solo.read("data/sprite/monster/poring.spr").has_value());

    // ROM mode without a RoM archive falls through to UaRO then GRO.
    vfs.setContentMode(ContentCategory::Mobs, ContentSource::Rom);
    d = vfs.read("data/sprite/monster/poring.spr");
    CHECK(d.has_value());
    if (d) CHECK(std::string(d->begin(), d->end()) == "UARO-PORING");

    std::filesystem::remove(p1);
    std::filesystem::remove(p2);
}
