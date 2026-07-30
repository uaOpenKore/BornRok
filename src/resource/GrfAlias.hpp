#pragma once
#include <string>
#include <unordered_map>

#include "core/Types.hpp"

namespace uaro {

// Korean -> English path-component aliases for the GRF (#: "english-first lookup"). The classic RO
// GRF names its sprite folders + many files in Korean (EUC-KR). To let the content team migrate to
// English gradually, the VFS tries an English-translated path FIRST, then the Korean original. This
// table seeds the common sprite folders, job names, body parts and town/map folders; a content
// maker uses the English names below (see Client/docs/grf-korean-names.md) and extends as needed.
// Keys are the raw EUC-KR bytes as they appear in GRF paths; values are lowercase ASCII.
inline const std::unordered_map<std::string, std::string>& koreanAliasMap() {
    static const std::unordered_map<std::string, std::string> m = {
    {"\xc0\xce\xb0\xa3\xc1\xb7", "human"},  // 인간족
    {"\xb8\xf6\xc5\xeb", "body"},  // 몸통
    {"\xb8\xd3\xb8\xae\xc5\xeb", "head"},  // 머리통
    {"\xb8\xd3\xb8\xae", "hair"},  // 머리
    {"\xb3\xb2", "male"},  // 남
    {"\xbf\xa9", "female"},  // 여
    {"\xb8\xf6", "body_pal"},  // 몸
    {"\xbe\xc7\xbc\xbc\xbb\xe7\xb8\xae", "accessory"},  // 악세사리
    {"\xb7\xce\xba\xea", "robe"},  // 로브
    {"\xb9\xe6\xc6\xd0", "shield"},  // 방패
    {"\xb8\xf3\xbd\xba\xc5\xcd", "monster"},  // 몬스터
    {"\xc0\xcc\xc6\xd1\xc6\xae", "effect"},  // 이팩트
    {"\xbf\xeb\xba\xb4", "mercenary"},  // 용병
    {"\xbe\xc6\xc0\xcc\xc5\xdb", "item"},  // 아이템
    {"\xc0\xaf\xc0\xfa\xc0\xce\xc5\xcd\xc6\xe4\xc0\xcc\xbd\xba", "userinterface"},  // 유저인터페이스
    {"\xc6\xe4\xc4\xda\xc6\xe4\xc4\xda\x5f\xb1\xe2\xbb\xe7", "pecopeco_knight"},  // 페코페코_기사
    {"\xbd\xc5\xc6\xe4\xc4\xda\xc5\xa9\xb7\xe7\xbc\xbc\xc0\xcc\xb4\xf5", "newpeco_crusader"},  // 신페코크루세이더
    {"\xb1\xb8\xc6\xe4\xc4\xda\xc5\xa9\xb7\xe7\xbc\xbc\xc0\xcc\xb4\xf5", "oldpeco_crusader"},  // 구페코크루세이더
    {"\xc3\xb5\xbb\xe7\xb3\xaf\xb0\xb3", "angel_wings"},  // 천사날개
    {"\xc5\xb8\xb6\xf4\xc3\xb5\xbb\xe7\xc0\xc7\xb3\xaf\xb0\xb3", "fallen_angel_wings"},  // 타락천사의날개
    {"\xb8\xf0\xc7\xe8\xb0\xa1\xb9\xe8\xb3\xb6", "adventurer_backpack"},  // 모험가배낭
    {"\xc3\xca\xba\xb8\xc0\xda", "novice"},  // 초보자
    {"\xb0\xcb\xbb\xe7", "swordman"},  // 검사
    {"\xb8\xb6\xb9\xfd\xbb\xe7", "mage"},  // 마법사
    {"\xb1\xc3\xbc\xf6", "archer"},  // 궁수
    {"\xbc\xba\xc1\xf7\xc0\xda", "acolyte"},  // 성직자
    {"\xbb\xf3\xc0\xce", "merchant"},  // 상인
    {"\xb5\xb5\xb5\xcf", "thief"},  // 도둑
    {"\xb1\xe2\xbb\xe7", "knight"},  // 기사
    {"\xc7\xc1\xb8\xae\xbd\xba\xc6\xae", "priest"},  // 프리스트
    {"\xc0\xa7\xc0\xfa\xb5\xe5", "wizard"},  // 위저드
    {"\xc1\xa6\xc3\xb6\xb0\xf8", "blacksmith"},  // 제철공
    {"\xc7\xe5\xc5\xcd", "hunter"},  // 헌터
    {"\xbe\xee\xbc\xbc\xbd\xc5", "assassin"},  // 어세신
    {"\xc5\xa9\xb7\xe7\xbc\xbc\xc0\xcc\xb4\xf5", "crusader"},  // 크루세이더
    {"\xb8\xf9\xc5\xa9", "monk"},  // 몽크
    {"\xbc\xbc\xc0\xcc\xc1\xf6", "sage"},  // 세이지
    {"\xb7\xce\xb1\xd7", "rogue"},  // 로그
    {"\xbf\xac\xb1\xdd\xbc\xfa\xbb\xe7", "alchemist"},  // 연금술사
    {"\xb9\xd9\xb5\xe5", "bard"},  // 바드
    {"\xb9\xab\xc8\xf1", "dancer"},  // 무희
    {"\xbd\xb4\xc6\xdb\xb3\xeb\xba\xf1\xbd\xba", "supernovice"},  // 슈퍼노비스
    {"\xb0\xc7\xb3\xca", "gunslinger"},  // 건너
    {"\xb4\xd1\xc0\xda", "ninja"},  // 닌자
    {"\xc5\xc2\xb1\xc7\xbc\xd2\xb3\xe2", "taekwon"},  // 태권소년
    {"\xb1\xc7\xbc\xba", "star_gladiator"},  // 권성
    {"\xbc\xd2\xbf\xef\xb8\xb5\xc4\xbf", "soul_linker"},  // 소울링커
    {"\xb7\xce\xb5\xe5\xb3\xaa\xc0\xcc\xc6\xae", "lord_knight"},  // 로드나이트
    {"\xc7\xcf\xc0\xcc\xc7\xc1\xb8\xae", "high_priest"},  // 하이프리
    {"\xc7\xcf\xc0\xcc\xc0\xa7\xc0\xfa\xb5\xe5", "high_wizard"},  // 하이위저드
    {"\xc8\xad\xc0\xcc\xc6\xae\xbd\xba\xb9\xcc\xbd\xba", "whitesmith"},  // 화이트스미스
    {"\xbd\xba\xb3\xaa\xc0\xcc\xc6\xdb", "sniper"},  // 스나이퍼
    {"\xbe\xee\xbd\xd8\xbd\xc5\xc5\xa9\xb7\xce\xbd\xba", "assassin_cross"},  // 어쌔신크로스
    {"\xc6\xc8\xb6\xf3\xb5\xf2", "paladin"},  // 팔라딘
    {"\xc3\xa8\xc7\xc7\xbf\xc2", "champion"},  // 챔피온
    {"\xc7\xc1\xb7\xce\xc6\xe4\xbc\xad", "professor"},  // 프로페서
    {"\xbd\xba\xc5\xe4\xc4\xbf", "stalker"},  // 스토커
    {"\xc5\xa9\xb8\xae\xbf\xa1\xc0\xcc\xc5\xcd", "creator"},  // 크리에이터
    {"\xc5\xac\xb6\xf3\xbf\xee", "clown"},  // 클라운
    {"\xc1\xfd\xbd\xc3", "gypsy"},  // 집시
    {"\xb7\xe9\xb3\xaa\xc0\xcc\xc6\xae", "rune_knight"},  // 룬나이트
    {"\xbf\xf6\xb7\xcf", "warlock"},  // 워록
    {"\xb7\xb9\xc0\xce\xc1\xae", "ranger"},  // 레인져
    {"\xbe\xc6\xc5\xa9\xba\xf1\xbc\xf3", "arch_bishop"},  // 아크비숍
    {"\xb9\xcc\xc4\xc9\xb4\xd0", "mechanic"},  // 미케닉
    {"\xb1\xe6\xb7\xce\xc6\xbe\xc5\xa9\xb7\xce\xbd\xba", "guillotine_cross"},  // 길로틴크로스
    {"\xb7\xce\xbe\xe2\xb0\xa1\xb5\xe5", "royal_guard"},  // 로얄가드
    {"\xbd\xb4\xb6\xf3", "sura"},  // 슈라
    {"\xbc\xd2\xbc\xad\xb7\xaf", "sorcerer"},  // 소서러
    {"\xb9\xce\xbd\xba\xc6\xae\xb7\xb2", "minstrel"},  // 민스트럴
    {"\xc1\xa6\xb3\xd7\xb8\xaf", "geneticist"},  // 제네릭
    {"\xbd\xa6\xb5\xb5\xbf\xec\xc3\xbc\xc0\xcc\xbc\xad", "shadow_chaser"},  // 쉐도우체이서
    {"\xb8\xb6\xb5\xb5\xb1\xe2\xbe\xee", "mado_gear"},  // 마도기어
    {"\xbf\xee\xbf\xb5\xc0\xda", "gm"},  // 운영자
    {"\xbf\xee\xbf\xb5\xc0\xda\x32", "gm2"},  // 운영자2
    {"\xbb\xea\xc5\xb8", "santa"},  // 산타
    {"\xb0\xe1\xc8\xa5", "wedding"},  // 결혼
    {"\xc7\xd1\xba\xb9", "hanbok"},  // 한복
    {"\xc5\xce\xbd\xc3\xb5\xb5", "tuxedo"},  // 턱시도
    {"\xb1\xe2\xc5\xb8\xb8\xb6\xc0\xbb", "etc_town"},  // 기타마을
    {"\xb1\xe2\xc5\xb8\xb8\xb6\xc0\xbb\xb3\xbb\xba\xce", "etc_town_indoor"},  // 기타마을내부
    {"\xc7\xca\xb5\xe5\xb9\xd9\xb4\xda", "field_ground"},  // 필드바닥
    {"\xb3\xbb\xba\xce\xbc\xd2\xc7\xb0", "indoor_props"},  // 내부소품
    {"\xbf\xdc\xba\xce\xbc\xd2\xc7\xb0", "outdoor_props"},  // 외부소품
    {"\xb3\xaa\xb9\xab\xc0\xe2\xc3\xca\xb2\xc9", "tree_grass_flower"},  // 나무잡초꽃
    {"\xbf\xf6\xc5\xcd", "water"},  // 워터
    // Ground/terrain texture FILENAME tokens (not just folders) so a content maker can drop a
    // fully-ASCII loose PNG override, e.g. prt_도시01.bmp <-> field_ground\prt_city01.png. Matched
    // per '_'-token with a trailing digit run tolerated ("도시01" -> "city01"). (S. ground overrides)
    {"\xb5\xb5\xbd\xc3", "city"},   // 도시
    {"\xc3\xca\xbf\xf8", "grass"},  // 초원
    {"\xc8\xeb", "dirt"},           // 흙
    {"\xb8\xf0\xb7\xa1", "sand"},   // 모래
    {"\xb4\xab", "snow"},           // 눈
    {"\xc0\xdc\xb5\xf0", "lawn"},   // 잔디
    {"\xb9\xd9\xb4\xda", "floor"},  // 바닥
    {"\xba\xae", "wall"},           // 벽
    {"\xb1\xe6", "road"},           // 길
    {"\xb9\xb0", "water_tile"},     // 물 (distinct from the 워터 water-folder alias above)
    {"\xb4\xf8\xc0\xfc", "dungeon"},  // 던전
    {"\xc0\xfc\xc0\xe5", "battlefield"},  // 전장
    {"\xb1\xe6\xb5\xe5\xc0\xfc", "woe"},  // 길드전
    {"\xc1\xf6\xc7\xcf\xb0\xa8\xbf\xc1", "prison"},  // 지하감옥
    {"\xc1\xf6\xc7\xcf\xb9\xa6\xc1\xf6", "catacombs"},  // 지하묘지
    {"\xbf\xeb\xbe\xcf\xb5\xbf\xb1\xbc", "lava_cave"},  // 용암동굴
    {"\xc8\xe6\xb8\xb6\xb9\xfd\xbb\xe7\xb9\xe6", "warlock_room"},  // 흑마법사방
    {"\xbf\xf6\xc7\xc1\xb4\xeb\xb1\xe2\xbd\xc7\xb3\xbb\xba\xce", "warp_room"},  // 워프대기실내부
    {"\xc7\xc1\xb7\xd0\xc5\xd7\xb6\xf3", "prontera"},  // 프론테라
    {"\xc7\xc1\xb7\xd0\xc5\xd7\xb6\xf3\xb3\xbb\xba\xce", "prontera_indoor"},  // 프론테라내부
    {"\xc6\xe4\xc0\xcc\xbf\xe6", "payon"},  // 페이욘
    {"\xc6\xe4\xc0\xcc\xbf\xe6\xb3\xbb\xba\xce", "payon_indoor"},  // 페이욘내부
    {"\xb0\xd4\xc6\xe4\xb4\xcf\xbe\xc6", "geffenia"},  // 게페니아
    {"\xb0\xd4\xc6\xe6\xb3\xbb\xba\xce", "geffen_indoor"},  // 게펜내부
    {"\xb8\xf0\xb7\xce\xc4\xda", "morocc"},  // 모로코
    {"\xb8\xf0\xb7\xce\xc4\xda\xb3\xbb\xba\xce", "morocc_indoor"},  // 모로코내부
    {"\xbe\xc6\xc0\xce\xba\xea\xb7\xce\xc5\xa9", "einbroch"},  // 아인브로크
    {"\xb6\xf3\xc7\xef", "rachel"},  // 라헬
    {"\xb8\xae\xc8\xf7\xc5\xb8\xb8\xa3\xc1\xa8", "lighthalzen"},  // 리히타르젠
    {"\xc8\xd6\xb0\xd6", "hugel"},  // 휘겔
    {"\xb1\xdb\xb7\xa1\xbd\xba\xc6\xae", "glastheim"},  // 글래스트
    {"\xb1\xdb\xb7\xa1\xc1\xf6\xc7\xcf\xbc\xf6\xb7\xce", "glast_underwater"},  // 글래지하수로
    {"\xc0\xaf\xb3\xeb", "juno"},  // 유노
    {"\xc0\xaf\xb3\xeb\xc3\xdf\xb0\xa1", "juno_ext"},  // 유노추가
    {"\xbe\xcb\xb5\xa5\xb9\xd9\xb6\xf5", "aldebaran"},  // 알데바란
    {"\xbe\xcb\xba\xa3\xb8\xa3\xc5\xb8", "alberta"},  // 알베르타
    {"\xbe\xcb\xba\xa3\xb8\xa3\xc5\xb8\xb3\xbb\xba\xce", "alberta_indoor"},  // 알베르타내부
    {"\xb4\xcf\xc7\xc3\xc7\xec\xc0\xd3", "niflheim"},  // 니플헤임
    {"\xc5\xb8\xb3\xaa\xc5\xe4\xbd\xba", "thanatos"},  // 타나토스
    {"\xc5\xe4\xb8\xa3\xc8\xad\xbb\xea", "thor_volcano"},  // 토르화산
    {"\xbe\xee\xba\xf1\xbd\xba", "abyss"},  // 어비스
    {"\xc0\xaf\xc6\xe4\xb7\xce\xbd\xba", "yuferos"},  // 유페로스
    {"\xbe\xc6\xbf\xe4\xc5\xb8\xbe\xdf", "ayothaya"},  // 아요타야
    {"\xbf\xf2\xb9\xdf\xb6\xf3", "umbala"},  // 움발라
    {"\xc0\xda\xbf\xcd\xc0\xcc", "jawaii"},  // 자와이
    {"\xb5\xee\xb4\xeb\xbc\xb6", "lighthouse_island"},  // 등대섬
    {"\xb0\xc5\xba\xcf\xc0\xcc\xbc\xb6", "turtle_island"},  // 거북이섬
    {"\xb9\xab\xb8\xed\xbc\xb6", "nameless_island"},  // 무명섬
    {"\xc7\xd8\xba\xaf\xb8\xb6\xc0\xbb", "comodo"},  // 해변마을
    {"\xbb\xe7\xb8\xb7\xb5\xb5\xbd\xc3", "morocc_desert"},  // 사막도시
    {"\xc5\xa9\xb8\xae\xbd\xba\xb8\xb6\xbd\xba\xb8\xb6\xc0\xbb", "xmas"},  // 크리스마스마을
    {"\xb5\xbf\xb1\xbc\xb8\xb6\xc0\xbb", "cave_town"},  // 동굴마을
    {"\xc1\xfd\xbd\xc3\xb8\xb6\xc0\xbb", "gypsy_town"},  // 집시마을
    {"\xc8\xf7\xb3\xaa\xb8\xb6\xc2\xea\xb8\xae", "hinamatsuri"},  // 히나마쯔리
    {"\xc1\xdf\xb1\xb9", "china"},  // 중국
    {"\xc0\xcf\xba\xbb", "japan"},  // 일본
    {"\xb7\xaf\xbd\xc3\xbe\xc6", "russia"},  // 러시아
    {"\xb4\xeb\xb8\xb8", "taiwan"},  // 대만
    {"\xba\xea\xb6\xf3\xc1\xfa\x20\xb8\xf3\xbd\xba\xc5\xcd", "brazil_monster"},  // 브라질 몬스터
    {"\xc0\xce\xb4\xf8\x30\x31", "indoor_dungeon01"},  // 인던01
    {"\xc0\xce\xb4\xf8\x30\x32", "indoor_dungeon02"},  // 인던02
    };
    return m;
}

// Translate the Korean components of a (normalized) GRF path to their English aliases, so the VFS
// can try the English name first. Splits on '\\' and '/' into components; each component is matched
// whole, else split on '_' and matched per sub-token (handles "<job>_<sex>" filenames), keeping the
// file extension. Pure-ASCII paths are returned unchanged (fast path); a path with no aliasable
// Korean token is also returned unchanged.
inline std::string aliasKoreanPath(const std::string& path) {
    bool hasKr = false;
    for (unsigned char c : path)
        if (c >= 0x80) { hasKr = true; break; }
    if (!hasKr) return path;  // already English / ASCII -> nothing to alias

    const auto& M = koreanAliasMap();
    bool changed = false;
    auto lookup = [&](const std::string& t) -> const std::string* {
        const auto it = M.find(t);
        return it != M.end() ? &it->second : nullptr;
    };
    // Alias one '_'-delimited sub-token, tolerating a trailing ASCII digit run so terrain tiles
    // resolve: "도시01" -> strip "01" -> alias "도시"=city -> "city01". Returns the token unchanged
    // when neither it nor its digit-stripped stem is aliasable.
    auto transToken = [&](const std::string& sub) -> std::string {
        if (const std::string* p = lookup(sub)) return *p;
        usize e = sub.size();
        while (e > 0 && sub[e - 1] >= '0' && sub[e - 1] <= '9') --e;
        if (e > 0 && e < sub.size())
            if (const std::string* p = lookup(sub.substr(0, e))) return *p + sub.substr(e);
        return sub;
    };
    auto transComp = [&](const std::string& comp) -> std::string {
        std::string stem = comp, ext;
        const usize dot = comp.rfind('.');
        if (dot != std::string::npos) { stem = comp.substr(0, dot); ext = comp.substr(dot); }
        if (const std::string* p = lookup(stem)) { changed = true; return *p + ext; }
        // split the stem on '_' and translate each sub-token (e.g. <job>_<sex>, prt_도시01)
        std::string r;
        bool any = false;
        usize s = 0;
        for (usize k = 0; k <= stem.size(); ++k) {
            if (k == stem.size() || stem[k] == '_') {
                const std::string sub = stem.substr(s, k - s);
                const std::string t = transToken(sub);
                if (t != sub) any = true;
                r += t;
                if (k < stem.size()) r += '_';
                s = k + 1;
            }
        }
        if (any) { changed = true; return r + ext; }
        return comp;
    };
    std::string out;
    usize start = 0;
    const usize n = path.size();
    for (usize k = 0; k <= n; ++k) {
        if (k == n || path[k] == '\\' || path[k] == '/') {
            out += transComp(path.substr(start, k - start));
            if (k < n) out += path[k];
            start = k + 1;
        }
    }
    return changed ? out : path;
}

} // namespace uaro
