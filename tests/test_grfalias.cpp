#include "microtest.hpp"
#include "resource/GrfAlias.hpp"

#include <string>

using namespace uaro;

TEST_CASE(grfalias_pure_ascii_unchanged) {
    const std::string p = "data\\sprite\\npc\\poring.spr";
    CHECK(aliasKoreanPath(p) == p);  // fast path: no Korean -> identity
}

TEST_CASE(grfalias_body_path_translates_folders_and_job_sex) {
    // data\sprite\인간족\몸통\남\초보자_남.spr -> .../human/body/male/novice_male.spr
    const std::string kr =
        "data\\sprite\\\xc0\xce\xb0\xa3\xc1\xb7\\\xb8\xf6\xc5\xeb\\\xb3\xb2\\\xc3\xca\xba\xb8\xc0\xda_\xb3\xb2.spr";
    CHECK(aliasKoreanPath(kr) == "data\\sprite\\human\\body\\male\\novice_male.spr");
}

TEST_CASE(grfalias_whole_component_with_underscore) {
    // 페코페코_기사 is a single aliased folder (contains '_') -> pecopeco_knight, matched whole.
    const std::string kr = "a\\\xc6\xe4\xc4\xda\xc6\xe4\xc4\xda_\xb1\xe2\xbb\xe7\\b.act";
    CHECK(aliasKoreanPath(kr) == "a\\pecopeco_knight\\b.act");
}

TEST_CASE(grfalias_unaliased_korean_token_kept) {
    // A made-up Korean byte sequence not in the table stays as-is (no false translation).
    const std::string kr = "x\\\xb1\xe2\xbb\xe7\\\xff\xfe.spr";  // 기사(knight) + an unknown token
    CHECK(aliasKoreanPath(kr) == "x\\knight\\\xff\xfe.spr");
}

TEST_CASE(grfalias_ground_tile_token_with_trailing_digits) {
    // data\texture\필드바닥\prt_도시01.png -> .../field_ground/prt_city01.png so a content maker
    // can drop a fully-ASCII loose ground override. The digit run "01" is preserved (도시01->city01).
    const std::string kr =
        "data\\texture\\\xc7\xca\xb5\xe5\xb9\xd9\xb4\xda\\prt_\xb5\xb5\xbd\xc3\x30\x31.png";
    CHECK(aliasKoreanPath(kr) == "data\\texture\\field_ground\\prt_city01.png");
    // 초원(grass) + 흙(dirt) variants too.
    const std::string kr2 =
        "data\\texture\\\xc7\xca\xb5\xe5\xb9\xd9\xb4\xda\\prt_\xc3\xca\xbf\xf8\x30\x39.bmp";
    CHECK(aliasKoreanPath(kr2) == "data\\texture\\field_ground\\prt_grass09.bmp");
    const std::string kr3 =
        "data\\texture\\\xc7\xca\xb5\xe5\xb9\xd9\xb4\xda\\prt_\xc8\xeb\x30\x33.bmp";
    CHECK(aliasKoreanPath(kr3) == "data\\texture\\field_ground\\prt_dirt03.bmp");
}
