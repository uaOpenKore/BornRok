#include "game/CharacterActor.hpp"

#include <algorithm>
#include <functional>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>

#include "core/Log.hpp"
#include "formats/ImageIO.hpp"
#include "formats/PngSprite.hpp"
#include "game/AccessorySprites.hpp"
#include "render/SamplerFilter.hpp"
#include "render/SpriteBatch.hpp"
#include "resource/Vfs.hpp"
#include "world/SpriteComposer.hpp"

namespace uaro {

namespace {
// optional<T> (from Sprite::parse / Action::parse) -> shared_ptr<T> (null on empty), + the
// process-wide parsed-asset cache for mob/NPC .spr/.act keyed by asset path. Declared HERE, ahead of
// the loaders, so their parse sites (line ~288+) can see them (MSVC needs the decl before use).
template <class T>
std::shared_ptr<T> mk(std::optional<T>&& o) {
    return o ? std::make_shared<T>(std::move(*o)) : nullptr;
}
std::unordered_map<std::string, std::shared_ptr<Sprite>> s_sprCache;
std::unordered_map<std::string, std::shared_ptr<Action>> s_actCache;
// Player whole-appearance cache: appearanceKey_ -> {armed?, idleMotion} render state; the parts live
// in s_spr/actCache under "<key>#p<idx>". Presence here means the full look was composed before.
// {armed?, idleMotion, attackMotion}. attackMotion MUST be cached too: it is detected per weapon
// (the first of ATTACK1/2/3 whose frames actually carry the weapon) and daggers put their frames in
// a different attack motion than swords/spears. Omitting it made a cache hit fall back to the default
// motion 5 -> a dagger (frames in 10/11) drew nothing on the swing while a sword (frames in 5) still
// worked (S.: "cutter не видно при атаке, сабер и ланце есть").
std::unordered_map<std::string, std::tuple<int, int, int>> s_metaCache;

// GRF path components in EUC-KR (the encoding the archive index stores). The
// archive's normalize() lowercases ASCII only, so these high bytes pass through
// unchanged and match. Verified against the real data.grf.
const std::string kHuman = "\xc0\xce\xb0\xa3\xc1\xb7";       // 인간족
const std::string kBody = "\xb8\xf6\xc5\xeb";               // 몸통
const std::string kHead = "\xb8\xd3\xb8\xae\xc5\xeb";       // 머리통
const std::string kHairPal = "\xb8\xd3\xb8\xae";            // 머리 (hair-dye palette folder/prefix, #86)
const std::string kBodyPal = "\xb8\xf6";                    // 몸 (clothes-dye palette folder, #86)

// Parsed dye-palette cache (#150, S.: "кеш на палитры покрасок ... как текстуры чаров"). Each
// data/palette/....pal is read + parsed to its 256-colour array ONCE and reused, so a composition
// cache-miss doesn't re-parse the palette. Read lazily on first use (no more map-entry preload,
// S. 2026-07-16) and cached session-lifetime like the composed-sprite cache. An absent/unparsable
// path is negative-cached (nullopt) so it isn't retried.
const std::array<SprPalColor, 256>* cachedPal(const Vfs& vfs, const std::string& palPath) {
    static std::unordered_map<std::string, std::optional<std::array<SprPalColor, 256>>> cache;
    auto it = cache.find(palPath);
    if (it == cache.end()) {
        std::optional<std::array<SprPalColor, 256>> parsed;
        if (auto pb = vfs.read(palPath)) parsed = parsePal(*pb);  // bytes are preloaded -> cheap read
        it = cache.emplace(palPath, std::move(parsed)).first;
    }
    return it->second ? &*it->second : nullptr;
}

// A character requested a dye colour but its .pal is missing from the content -> the part shows its
// default colour. Log it ONCE per palette so the content team knows exactly which file to add
// (S.: "вывод ошибки в лог, если не хватает палеттов для покраски"). Deduped so it never spams.
void warnMissingDyePal(const char* kind, int color, const std::string& palPath) {
    static std::unordered_map<std::string, bool> warned;
    if (warned.emplace(palPath, true).second)
        log::warn("dye: {} colour {} has no palette '{}' — part shows default colour (add this .pal)",
                  kind, color, palPath);
}
const std::string kMale = "\xb3\xb2";                       // 남
const std::string kFemale = "\xbf\xa9";                     // 여
const std::string kShield = "\xb9\xe6\xc6\xd0";             // 방패 (shield sprite folder)

// Sprite -> world sizing. A RO body composes to ~92px tall; map that to a fixed
// world height (2.0 GND cells) so the character is correctly proportioned at any
// zoom. Equipment/headgear added later extends above this body reference.
constexpr float kRefBodyPx = 92.0f;
constexpr float kBodyHeightCells = 1.0f;  // ~one cell / half a prontera column (tunable)
constexpr float kWorldPerPx = kBodyHeightCells / kRefBodyPx;  // world units per sprite pixel

// GRF-driven job->body-folder fallback, set once by Application after the GRF lua tables load. Lets
// classes NOT in the hardcoded switch below (3rd/4th jobs: Rune Knight, Arch Bishop, Royal Guard,
// Genetic, Shadow Chaser, Hyper Novice, ...) resolve their body sprite from data/luafiles.../jobname
// instead of collapsing to novice (S. 2026-07-26 Korean body-name list). Absent/empty -> novice.
namespace { std::function<std::string(u16)> s_jobResolver; }
// setJobSpriteResolver is DEFINED at the end of the file (outside this anonymous namespace) so it has
// EXTERNAL linkage — Application.cpp calls it. It still reaches s_jobResolver (anon members inject into
// the enclosing uaro namespace). Defining it here would give it internal linkage -> LNK2019.

// class id -> body sprite name (EUC-KR). Every entry was verified present in the
// real data.grf. The rebirth "high" 1st jobs (4001-4007) reuse their normal
// sprites; classes absent here fall back to the GRF jobname table (s_jobResolver),
// then to the novice sprite (logged) if the GRF has no entry either.
std::string jobSpriteName(u16 classId) {
    if (classId >= 4001 && classId <= 4007)  // high novice/swordman/.../thief
        return jobSpriteName(static_cast<u16>(classId - 4001));
    // Baby classes use the SAME body sprite as their base job (S.: "теже спрайты"), just drawn at
    // 0.8x (that scale is applied in GameScene). The baby id order matches the base job order, so
    // 4023..4044 -> base job 0..21, and 4045 (Super Baby) -> Super Novice (23). Without this they
    // fell through to the novice default (every baby looked like a novice). Verified vs roBrowser.
    if (classId >= 4023 && classId <= 4044)
        return jobSpriteName(static_cast<u16>(classId - 4023));
    if (classId == 4045)
        return jobSpriteName(23);
    switch (classId) {
        case 0:    return "\xc3\xca\xba\xb8\xc0\xda";                    // 초보자 novice
        case 1:    return "\xb0\xcb\xbb\xe7";                            // 검사 swordman
        case 2:    return "\xb8\xb6\xb9\xfd\xbb\xe7";                    // 마법사 magician
        case 3:    return "\xb1\xc3\xbc\xf6";                            // 궁수 archer
        case 4:    return "\xbc\xba\xc1\xf7\xc0\xda";                    // 성직자 acolyte
        case 5:    return "\xbb\xf3\xc0\xce";                            // 상인 merchant
        case 6:    return "\xb5\xb5\xb5\xcf";                            // 도둑 thief
        case 7:    return "\xb1\xe2\xbb\xe7";                            // 기사 knight
        case 8:    return "\xc7\xc1\xb8\xae\xbd\xba\xc6\xae";            // 프리스트 priest
        case 9:    return "\xc0\xa7\xc0\xfa\xb5\xe5";                    // 위저드 wizard
        case 10:   return "\xc1\xa6\xc3\xb6\xb0\xf8";                    // 제철공 blacksmith
        case 11:   return "\xc7\xe5\xc5\xcd";                            // 헌터 hunter
        case 12:   return "\xbe\xee\xbc\xbc\xbd\xc5";                    // 어세신 assassin
        case 14:   return "\xc5\xa9\xb7\xe7\xbc\xbc\xc0\xcc\xb4\xf5";    // 크루세이더 crusader
        case 15:   return "\xb8\xf9\xc5\xa9";                            // 몽크 monk
        case 16:   return "\xbc\xbc\xc0\xcc\xc1\xf6";                    // 세이지 sage
        case 17:   return "\xb7\xce\xb1\xd7";                            // 로그 rogue
        case 18:   return "\xbf\xac\xb1\xdd\xbc\xfa\xbb\xe7";            // 연금술사 alchemist
        case 19:   return "\xb9\xd9\xb5\xe5";                            // 바드 bard
        case 20:   return "\xb9\xab\xc8\xf1";                            // 무희 dancer
        case 23:   return "\xbd\xb4\xc6\xdb\xb3\xeb\xba\xf1\xbd\xba";    // 슈퍼노비스 super novice
        case 24:   return "\xb0\xc7\xb3\xca";                            // 건너 gunslinger (showed novice, S.)
        case 25:   return "\xb4\xd1\xc0\xda";                            // 닌자 ninja
        case 22:   return "\xb0\xe1\xc8\xa5";                            // 결혼 wedding (S.: was novice)
        case 26:   return "\xbb\xea\xc5\xb8";                            // 산타 christmas/santa (S.: was novice)
        case 27:   return "\xbf\xa9\xb8\xa7";                            // 여름 summer (S.: was novice)
        case 4046: return "\xc5\xc2\xb1\xc7\xbc\xd2\xb3\xe2";           // 태권소년 taekwon
        case 4047: return "\xb1\xc7\xbc\xba";                           // 권성 star gladiator
        case 4048: return "\xb1\xc7\xbc\xba\xc0\xb6\xc7\xd5";           // 권성융합 star gladiator (union/flying, S.: was novice)
        case 4049: return "\xbc\xd2\xbf\xef\xb8\xb5\xc4\xbf";           // 소울링커 soul linker
        case 4008: return "\xb7\xce\xb5\xe5\xb3\xaa\xc0\xcc\xc6\xae";    // 로드나이트 lord knight
        case 4009: return "\xc7\xcf\xc0\xcc\xc7\xc1\xb8\xae";            // 하이프리 high priest
        case 4010: return "\xc7\xcf\xc0\xcc\xc0\xa7\xc0\xfa\xb5\xe5";    // 하이위저드 high wizard
        case 4011: return "\xc8\xad\xc0\xcc\xc6\xae\xbd\xba\xb9\xcc\xbd\xba";  // 화이트스미스 whitesmith
        case 4012: return "\xbd\xba\xb3\xaa\xc0\xcc\xc6\xdb";            // 스나이퍼 sniper
        case 4013: return "\xbe\xee\xbd\xd8\xbd\xc5\xc5\xa9\xb7\xce\xbd\xba";  // 어쌔신크로스 assassin cross
        case 4015: return "\xc6\xc8\xb6\xf3\xb5\xf2";                    // 팔라딘 paladin
        case 4016: return "\xc3\xa8\xc7\xc7\xbf\xc2";                    // 챔피온 champion
        case 4017: return "\xc7\xc1\xb7\xce\xc6\xe4\xbc\xad";            // 프로페서 professor
        case 4018: return "\xbd\xba\xc5\xe4\xc4\xbf";                    // 스토커 stalker
        case 4019: return "\xc5\xa9\xb8\xae\xbf\xa1\xc0\xcc\xc5\xcd";    // 크리에이터 creator
        case 4020: return "\xc5\xac\xb6\xf3\xbf\xee";                    // 클라운 clown
        case 4021: return "\xc1\xfd\xbd\xc3";                            // 집시 gypsy
        // Peco-mounted job ids (JOB_*2): the body sprite is the SAME as the un-mounted class (the peco
        // body + mounted weapon come from the riding path). load() remaps these before calling, but
        // other callers (asset preload) pass the raw id -- without these they fell to novice. (S. #4014)
        case 13:   return jobSpriteName(7);     // JOB_KNIGHT2      -> Knight
        case 21:   return jobSpriteName(14);    // JOB_CRUSADER2    -> Crusader
        case 4014: return jobSpriteName(4008);  // JOB_LORD_KNIGHT2 -> Lord Knight
        case 4022: return jobSpriteName(4015);  // JOB_PALADIN2     -> Paladin
        default:
            // Not in the hardcoded table -> ask the GRF's jobname lua (covers 3rd/4th jobs etc.).
            if (s_jobResolver) {
                std::string s = s_jobResolver(classId);
                if (!s.empty()) return s;
            }
            log::warn("CharacterActor: no sprite name for class {} (using novice)", classId);
            return "\xc3\xca\xba\xb8\xc0\xda";
    }
}

// Peco-mounted body sprite name for the classes that ride one (OPTION_RIDING),
// or nullptr if the class uses its normal body. Verified present in data.grf.
const char* ridingBodyName(u16 classId) {
    switch (classId) {
        case 7:    return "\xc6\xe4\xc4\xda\xc6\xe4\xc4\xda_\xb1\xe2\xbb\xe7";  // 페코페코_기사 (knight)
        case 14:   return "\xbd\xc5\xc6\xe4\xc4\xda\xc5\xa9\xb7\xe7\xbc\xbc\xc0\xcc\xb4\xf5";  // 신페코크루세이더
        case 4008: return "\xb7\xce\xb5\xe5\xc6\xe4\xc4\xda";                  // 로드페코 (lord knight)
        case 4015: return "\xc6\xe4\xc4\xda\xc6\xc8\xb6\xf3\xb5\xf2";          // 페코팔라딘 (paladin)
        default:   return nullptr;
    }
}

// Weapon-folder name for a riding class. roBrowser builds the mounted weapon path from
// WeaponJobTable[MountTable[job]] — the REGULAR peco class's folder, which for the 2nd jobs
// is NOT the 2nd-job body folder: lord knight's body is 로드페코 (ships no weapons in the GRF)
// but its mounted weapon lives in the knight's 페코페코_기사; paladin's in the crusader's
// 신페코크루세이더. So the weapon folder differs from ridingBodyName for 4008/4015. (S. report.)
const char* weaponRidingName(u16 classId) {
    switch (classId) {
        case 7:
        case 4008: return "\xc6\xe4\xc4\xda\xc6\xe4\xc4\xda_\xb1\xe2\xbb\xe7";  // 페코페코_기사 (knight / lord knight)
        case 14:
        case 4015: return "\xbd\xc5\xc6\xe4\xc4\xda\xc5\xa9\xb7\xe7\xbc\xbc\xc0\xcc\xb4\xf5";  // 신페코크루세이더 (crusader / paladin)
        default:   return nullptr;
    }
}

// Transcended 2nd jobs ship a BODY but no WEAPON sprites of their own — they reuse the BASE job's
// weapon folder (e.g. Assassin Cross 어쌔신크로스 has no weapons; its katar is the base Assassin's
// 어세신_남_카타르_카타르, verified absent under the cross folder). Map the transcended class to the
// base job's sprite name, tried as a fallback weapon folder (S.: "у ассасин-кросса катары не рисуются").
std::string weaponBaseJob(u16 classId) {
    switch (classId) {
        case 4008:
        case 4014: return jobSpriteName(7);   // Lord Knight -> Knight
        case 4009: return jobSpriteName(8);   // High Priest -> Priest
        case 4010: return jobSpriteName(9);   // High Wizard -> Wizard
        case 4011: return jobSpriteName(10);  // Whitesmith -> Blacksmith
        case 4012: return jobSpriteName(11);  // Sniper -> Hunter
        case 4013: return jobSpriteName(12);  // Assassin Cross -> Assassin
        case 4015: return jobSpriteName(14);  // Paladin -> Crusader
        case 4016: return jobSpriteName(15);  // Champion -> Monk
        case 4017: return jobSpriteName(16);  // Professor -> Sage
        case 4018: return jobSpriteName(17);  // Stalker -> Rogue
        case 4019: return jobSpriteName(18);  // Creator -> Alchemist (S.: "у креатора не рисуется оружие")
        case 4020: return jobSpriteName(19);  // Clown -> Bard
        case 4021: return jobSpriteName(20);  // Gypsy -> Dancer
        default:   return {};                 // non-transcended -> no base-job fallback
    }
}

// Weapon-look class -> sprite-name suffix (EUC-KR), all verified present in data.grf.
// The server sends the item_db "View" value: a small look class (Knife=1, Sword=2,
// ...) for ordinary weapons, or a custom sprite NUMBER for special ones — so a known
// small look uses the class-name sprite, an unknown (large) value the numbered sprite.
const char* weaponLookName(u16 look) {
    switch (look) {
        case 1:  return "\xb4\xdc\xb0\xcb";                  // 단검 dagger
        case 2:  return "\xb0\xcb";                          // 검 sword (1H)
        case 3:  return "\xbe\xe7\xbc\xd5\xb0\xcb";          // 양손검 two-handed sword
        case 4:  return "\xc3\xa2";                          // 창 spear (1H)
        case 5:  return "\xbe\xe7\xbc\xd5\xc3\xa2";          // 양손창 two-handed spear
        case 6:  return "\xb5\xb5\xb3\xa2";                  // 도끼 axe (1H)
        case 7:  return "\xbe\xe7\xbc\xd5\xb5\xb5\xb3\xa2";  // 양손도끼 two-handed axe
        case 8:  return "\xc5\xac\xb7\xb4";                  // 클럽 mace / club
        case 10: return "\xb7\xd4\xb5\xe5";                  // 롯드 rod / staff
        case 11: return "\xc8\xb0";                          // 활 bow
        case 12: return "\xb3\xca\xc5\xac";                  // 너클 knuckle
        case 13: return "\xbe\xc7\xb1\xe2";                  // 악기 instrument
        case 14: return "\xc3\xa4\xc2\xef";                  // 채찍 whip
        case 15: return "\xc3\xa5";                          // 책 book
        case 16: return "\xc4\xab\xc5\xb8\xb8\xa3_\xc4\xab\xc5\xb8\xb8\xa3";  // 카타르_카타르 katar (S.:
                 // the assassin katar sprite is the class name DOUBLED, e.g.
                 // 어세신_남_카타르_카타르 — a single 카타르 has no sprite, so the katar never drew)
        case 23: return "\xb7\xd4\xb5\xe5";                  // 롯드 two-handed rod (roBrowser TWOHANDROD)
        default: return nullptr;
    }
}

// Fallback suffix for two-handed weapons: the stock GRF ships NO 양손* sprite for most
// jobs — the two-handed sword/spear/axe reuse the ONE-HANDED sprite name (roBrowser
// WeaponTable maps TWOHANDSWORD->검, TWOHANDSPEAR->창, TWOHANDAXE->도끼). So try the 양손
// name first (a job may carry a dedicated 2H art), then this 1H name — else a two-handed
// weapon composed 기사_남_양손검, which doesn't exist, and drew nothing. (S.: двуручные не рисуются)
const char* weaponLookAlt(u16 look) {
    switch (look) {
        case 3: return "\xb0\xcb";          // 양손검 -> 검  (1H sword sprite)
        case 5: return "\xc3\xa2";          // 양손창 -> 창  (1H spear sprite)
        case 7: return "\xb5\xb5\xb3\xa2";  // 양손도끼 -> 도끼 (1H axe sprite)
        default: return nullptr;
    }
}

// Shield-look value -> sprite-name suffix (EUC-KR), mirroring roBrowser's ShieldTable.
// The server's LOOK_SHIELD carries a small shield VIEW id (1..4, the item_db "View"):
// 1 Guard, 2 Buckler, 3 Shield, 4 Mirror. All four sprite names verified present in
// data.grf. Unknown/no-shield -> nullptr (the shield part is then not loaded).
const char* shieldLookName(u16 look) {
    switch (look) {
        case 1: return "\xb0\xa1\xb5\xe5";                  // 가드 Guard
        case 2: return "\xb9\xf6\xc5\xac\xb7\xaf";          // 버클러 Buckler
        case 3: return "\xbd\xaf\xb5\xe5";                  // 쉴드 Shield
        case 4: return "\xb9\xcc\xb7\xaf\xbd\xaf\xb5\xe5";  // 미러쉴드 Mirror Shield
        default: return nullptr;
    }
}

// The server's ZC_SPRITE_CHANGE sends the equipped shield as the ITEM NAMEID (e.g. Guard = 2101),
// NOT the small view id — exactly like the weapon (which weaponViewFromValue maps). Map the shield
// nameid to its item_db "Look" view (1..4); a value already < 5 is a view (e.g. from char-data).
u16 shieldViewFromValue(u16 v) {
    if (v == 0) return 0;     // no shield
    if (v < 5) return v;      // already a view (1 Guard .. 4 Mirror) — e.g. the char-list field
    switch (v) {              // item nameid -> Look (verified in db/item_db.txt)
        case 2101: case 2102: return 1;            // Guard
        case 2103: case 2104: return 2;            // Buckler
        case 2105: case 2106: case 2110: return 3; // Shield / Holy Guard
        case 2107: case 2108: return 4;            // Mirror Shield
        case 2109: return 0;                       // Memory Book — no shield sprite
        default:   return 2;                       // unknown shield -> a generic Buckler sprite
    }
}

// Resolve a weapon "look" value from the server to a small weapon view class
// (1..30, see weaponLookName). eAthena's clif_get_weapon_view sends the equipped
// item's *nameid* whenever the item has no view_id alias — e.g. a Knife arrives as
// 1201 (its item id) rather than view class 1. Mirror roBrowser's DB.getWeaponViewID:
// values below WeaponType.MAX (31) are already a view class; larger values are item
// nameids mapped to a view class by their id range. Unknown/non-weapon ids -> 0.
u16 weaponViewFromValue(u16 v) {
    if (v == 0 || v < 31) return v;  // 0 = unarmed; 1..30 = already a view class
    if (v < 1100) return 0;
    // Gravity's irregular sub-ranges that sit inside other ranges.
    if (v >= 1116 && v <= 1118) return 3;   // two-handed sword
    if (v >= 1314 && v <= 1315) return 7;   // two-handed axe
    if (v >= 1410 && v <= 1412) return 5;   // two-handed spear
    if (v >= 1472 && v <= 1473) return 10;  // rod
    if (v == 1599) return 8;                // mace
    // Regular nameid ranges.
    if (v < 1150) return 2;   // sword (1H)
    if (v < 1200) return 3;   // two-handed sword
    if (v < 1250) return 1;   // dagger / short sword (Knife 1201 -> 1)
    if (v < 1300) return 16;  // katar
    if (v < 1350) return 6;   // axe (1H)
    if (v < 1400) return 7;   // two-handed axe
    if (v < 1450) return 4;   // spear (1H)
    if (v < 1500) return 5;   // two-handed spear
    if (v < 1550) return 8;   // mace
    if (v < 1600) return 15;  // book
    if (v < 1650) return 10;  // rod / staff
    if (v < 1700) return 0;
    if (v < 1750) return 11;  // bow
    if (v < 1800) return 0;
    if (v < 1850) return 12;  // knuckle
    if (v < 1900) return 0;
    if (v < 1950) return 13;  // instrument
    if (v < 2000) return 14;  // whip
    if (v < 2050) return 23;  // two-handed rod
    return 0;                 // guns / shuriken / etc.: no body-weapon suffix yet
}

// Some HD sprite frames were re-exported to WebP as OPAQUE RGB with a solid BLACK background instead
// of a transparent alpha channel (S. 2026-08-04: "все спрайты с чёрным фоном которые загружаются с
// webp"). WebPDecodeRGBA then gives alpha=255 everywhere, so the black box renders solid. Recover the
// transparency by flood-filling near-black pixels REACHABLE FROM THE BORDER to alpha 0: this clears the
// background but leaves the sprite's INTERIOR black (outlines, dark cloth) opaque, since those are
// enclosed by non-black sprite pixels the fill can't cross. No-op when the frame already carries real
// alpha (a border pixel with alpha 0 fails the opaque test) or when no black touches the edge. tol is
// generous vs (0,0,0) because WebP compression jitters the flat background a few levels off pure black.
void keyBlackBackground(std::vector<u8>& px, int w, int h, int tol = 16) {
    if (w <= 0 || h <= 0 || px.size() < static_cast<usize>(w) * h * 4) return;
    // Content moving to .webp-with-alpha: a frame that already has real transparency must not be
    // black-keyed (it would eat the sprite's own dark interior). Only flattened opaque webp/PNG-sprite
    // frames (no alpha==0 anywhere) need this. (S. 2026-08-09.)
    if (hasTransparentPixels(px)) return;
    auto isBg = [&](int x, int y) {
        const u8* p = &px[(static_cast<usize>(y) * w + x) * 4];
        return p[3] > 0 && p[0] <= tol && p[1] <= tol && p[2] <= tol;
    };
    std::vector<u8> seen(static_cast<usize>(w) * h, 0);
    std::vector<int> stack;
    auto push = [&](int x, int y) {
        const usize i = static_cast<usize>(y) * w + x;
        if (!seen[i] && isBg(x, y)) { seen[i] = 1; stack.push_back(static_cast<int>(i)); }
    };
    for (int x = 0; x < w; ++x) { push(x, 0); push(x, h - 1); }
    for (int y = 0; y < h; ++y) { push(0, y); push(w - 1, y); }
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const int x = i % w, y = i / w;
        px[static_cast<usize>(i) * 4 + 3] = 0;  // background texel -> transparent
        if (x > 0) push(x - 1, y);
        if (x < w - 1) push(x + 1, y);
        if (y > 0) push(x, y - 1);
        if (y < h - 1) push(x, y + 1);
    }
}

// Dilate opaque colour into transparent texels so bilinear filtering near the
// edges interpolates real colours instead of the (0,0,0,0) transparent ones —
// otherwise smoothed sprites get a dark halo. Alpha is left untouched (still the
// hard 0/255 cutout the shader keys on); only the RGB of transparent texels is
// filled from opaque neighbours, propagated a couple of pixels out.
void bleedEdges(std::vector<u8>& px, int w, int h) {
    if (w <= 0 || h <= 0 || px.size() < static_cast<usize>(w) * h * 4) return;
    auto hasColor = [&](const std::vector<u8>& s, int x, int y) {
        const u8* p = &s[(static_cast<usize>(y) * w + x) * 4];
        return p[3] != 0 || p[0] || p[1] || p[2];  // opaque, or already filled
    };
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<u8> src = px;  // read from a snapshot so a pass doesn't cascade
        bool any = false;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                u8* o = &px[(static_cast<usize>(y) * w + x) * 4];
                if (o[3] != 0 || (o[0] | o[1] | o[2])) continue;  // not an empty transparent texel
                int r = 0, g = 0, b = 0, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        if (!hasColor(src, nx, ny)) continue;
                        const u8* s = &src[(static_cast<usize>(ny) * w + nx) * 4];
                        r += s[0]; g += s[1]; b += s[2]; ++n;
                    }
                if (n > 0) {
                    o[0] = static_cast<u8>(r / n);
                    o[1] = static_cast<u8>(g / n);
                    o[2] = static_cast<u8>(b / n);  // alpha stays 0
                    any = true;
                }
            }
        }
        if (!any) break;
    }
}

// RO's transparent key for TRUECOLOR (RGBA) sprite frames is magenta (0xFF00FF) -- indexed frames
// use palette index 0, but some peco/mount composites carry raw magenta in truecolor frames that,
// left opaque, render as a PINK silhouette (S.: "розовый пеко магентой"). Zero near-magenta texels
// AND despill the antialiased rim: HD/resampled frames blend the magenta key into the sprite edge,
// leaving semi-magenta texels the hard key misses that read as a lilac-purple outline (S.: "опять
// сиренево-фиолетовое пеко"). Neutralise that magenta excess and fade the rim.
void keyMagentaRgba(std::vector<u8>& px) {
    for (usize p = 0; p + 3 < px.size(); p += 4) {
        const int r = px[p], g = px[p + 1], b = px[p + 2], a = px[p + 3];
        if (a == 0) continue;
        if (r >= 200 && g <= 60 && b >= 200) {         // hard key: solid magenta -> fully transparent
            px[p] = px[p + 1] = px[p + 2] = px[p + 3] = 0;
            continue;
        }
        // Despill: a magenta-TINTED texel has both r and b clearly above g and roughly balanced (a
        // magenta hue, not a legit red r>>b or blue b>>r). Pull r,b down to g (toward neutral) and drop
        // alpha by how magenta it was, so the rim fades instead of glowing purple. Guards keep legit
        // pinks/reds/blues on the sprite intact.
        const int mn = r < b ? r : b;
        const int spill = mn - g;
        const int dRB = r > b ? r - b : b - r;
        if (spill > 40 && dRB < 60) {
            px[p] = static_cast<u8>(r - spill < 0 ? 0 : r - spill);
            px[p + 2] = static_cast<u8>(b - spill < 0 ? 0 : b - spill);
            const int drop = spill > 160 ? 160 : spill;  // cap so semi-transparent pinks aren't nuked
            px[p + 3] = static_cast<u8>(a * (255 - drop) / 255);
        }
    }
}
} // namespace

std::vector<std::string> CharacterActor::playerPartPaths() {
    std::vector<std::string> out;
    const std::string base = "data/sprite/" + kHuman + "/";
    // Known job class ids (base + first + 2nd + trans); jobSpriteName collapses dupes, deduped below.
    static const u16 kJobs[] = {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 14, 15, 16, 17, 18, 19, 20, 23, 24, 25,
        4008, 4009, 4010, 4011, 4012, 4013, 4014, 4015, 4016, 4017, 4018, 4019, 4020, 4021,
        4046, 4047, 4049};
    constexpr int kMaxDye = 30;  // dye-colour range to warm (missing ones no-op on read) (S.)
    const std::string pal = "data/palette/";
    for (const std::string& sx : {kMale, kFemale}) {
        for (u16 job : kJobs) {
            const std::string jn = jobSpriteName(job);
            const std::string b = base + kBody + "/" + sx + "/" + jn + "_" + sx;
            out.push_back(b + ".spr");
            out.push_back(b + ".act");
            // Clothes-dye palettes: data/palette/몸/<job>_<sex>_<color>.pal (S.: texture покрасок)
            for (int c = 1; c <= kMaxDye; ++c)
                out.push_back(pal + kBodyPal + "/" + jn + "_" + sx + "_" + std::to_string(c) + ".pal");
        }
        for (u16 h = 1; h <= 30; ++h) {  // hairstyle heads 1..30
            const std::string b = base + kHead + "/" + sx + "/" + std::to_string(h) + "_" + sx;
            out.push_back(b + ".spr");
            out.push_back(b + ".act");
            // Hair-dye palettes: data/palette/머리/머리<style>_<sex>_<color>.pal (S.: цвета причёсок)
            for (int c = 1; c <= kMaxDye; ++c)
                out.push_back(pal + kHairPal + "/" + kHairPal + std::to_string(h) + "_" + sx + "_" +
                              std::to_string(c) + ".pal");
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool CharacterActor::loadHeadgear(const Vfs& vfs, int part, u16 viewId, const std::string& sx) {
    spr_[part].reset();
    act_[part].reset();
    if (viewId == 0) return false;
    const char* nm = accessoryName(viewId);
    if (!nm) return false;
    const std::string acc = "\xbe\xc7\xbc\xbc\xbb\xe7\xb8\xae";  // 악세사리 (accessory)
    const std::string base = "data/sprite/" + acc + "/" + sx + "/" + sx + nm;
    auto s = vfs.read(base + ".spr");
    auto a = vfs.read(base + ".act");
    if (s && a) {
        spr_[part] = mk(Sprite::parse(*s));
        act_[part] = mk(Action::parse(*a));
        if (spr_[part] && act_[part]) {
            // Every character part attaches to the body's neck anchor via offset = body.anchor -
            // own.anchor (composePart) -- exactly roBrowser (all of head + accessory1/2/3 use the
            // body anchor; only the body updates the reference). A headgear whose ACT frames carry
            // NO anchor point is treated as own.anchor = (0,0), so composePart now places its sprite
            // origin ON the neck anchor (not the feet) -- see composePart. Just note the missing-anchor
            // case (S.: "очки и маски были ниже чаров" / "усы уехали вниз") for diagnostics.
            bool hasAnchor = false;
            for (const auto& ac : act_[part]->actions()) {
                for (const auto& fr : ac.frames)
                    if (!fr.anchors.empty()) { hasAnchor = true; break; }
                if (hasAnchor) break;
            }
            if (!hasAnchor)
                log::info("CharacterActor: headgear '{}' (view {}) has no ACT anchor points -> "
                          "attaching its sprite origin to the body neck anchor (composePart)", base, viewId);
            loadPngOverrides(vfs, part, base);  // #109: loose PNG frame replacements
            return true;
        }
        spr_[part].reset();
        act_[part].reset();
    }
    return false;
}

bool CharacterActor::load(const Vfs& vfs, u16 classId, u8 sex, u16 hair, u16 headBottom,
                          u16 headMid, u16 headTop, bool riding, u16 weapon, u16 shield,
                          u16 hairColor, u16 clothColor) {
    destroy();
    // Shared-cache identity: every param that changes a composed pixel (job/sex/hair/headgear/mount/
    // weapon/shield + the two dye colours). Two PCs with an identical look share frame textures.
    appearanceKey_ = "P" + std::to_string(classId) + "_" + std::to_string(sex) + "_" +
                     std::to_string(hair) + "_" + std::to_string(headBottom) + "_" +
                     std::to_string(headMid) + "_" + std::to_string(headTop) + "_" +
                     std::to_string(riding ? 1 : 0) + "_" + std::to_string(weapon) + "_" +
                     std::to_string(shield) + "_" + std::to_string(hairColor) + "_" +
                     std::to_string(clothColor);
    isPlayer_ = true;  // composed PC body: attack uses motion 5 (ATTACK1)
    // This server sends a MOUNTED 2nd/trans job as a SEPARATE peco class id (JOB_*2) instead of the
    // base class + OPTION_RIDING -- jobSpriteName doesn't know those ids, so the rider drew as a
    // NOVICE (S.: other Lord Knight 4014 / Paladin 4022, confirmed in the live log). Normalise the
    // peco class to its base id + force riding so the peco body + mounted-weapon path renders it.
    switch (classId) {
        case 13:   classId = 7;    riding = true; break;  // JOB_KNIGHT2      -> Knight on peco
        case 21:   classId = 14;   riding = true; break;  // JOB_CRUSADER2    -> Crusader on peco
        case 4014: classId = 4008; riding = true; break;  // JOB_LORD_KNIGHT2 -> Lord Knight on peco
        case 4022: classId = 4015; riding = true; break;  // JOB_PALADIN2     -> Paladin on peco
        default:   break;
    }
    riding_ = riding;  // remember the mount so renderWorld can speed up the peco's walk (S.)
    // Whole-appearance cache (S.: players + all headgear too): appearanceKey_ captures every
    // pixel-affecting param, so if this exact look was composed before, reuse its already-parsed AND
    // dyed parts instead of re-reading + re-palettising them. Correct by construction -- the key
    // includes both dye colours, so no palette can leak between different-looking players.
    if (auto mi = s_metaCache.find(appearanceKey_); mi != s_metaCache.end()) {
        for (int i = 0; i < kParts; ++i) {
            spr_[i] = s_sprCache[appearanceKey_ + "#p" + std::to_string(i)];
            act_[i] = s_actCache[appearanceKey_ + "#p" + std::to_string(i)];
        }
        armed_ = std::get<0>(mi->second) != 0;
        idleMotion_ = std::get<1>(mi->second);
        attackMotion_ = std::get<2>(mi->second);  // restore the per-weapon swing motion (dagger != sword)
        return ready();
    }
    const std::string sx = (sex != 0) ? kMale : kFemale;
    const char* peco = riding ? ridingBodyName(classId) : nullptr;
    const std::string job = peco ? std::string(peco) : jobSpriteName(classId);
    const std::string base = "data/sprite/" + kHuman + "/";
    // The server's hair value is an INDEX into a per-sex lookup table that gives the real head
    // sprite + palette number; only the low styles are remapped, higher ids pass through. Without
    // this the wrong head .spr loaded and the per-colour .pal path was wrong, so hairstyles AND
    // hair colour/dye looked broken (S.: "причёски/цвет волос не работают"; verified vs roBrowser
    // DBManager.getHeadPath/HairIndexTable). Rows: [0]=female (여), [1]=male (남).
    static const u16 kHairIdx[2][13] = {
        {2, 2, 4, 7, 1, 5, 3, 6, 12, 10, 9, 11, 8},
        {2, 2, 1, 7, 5, 4, 3, 6, 8, 9, 10, 12, 11},
    };
    const int hairRow = (sex != 0) ? 1 : 0;
    const u16 rawHair = (hair == 0) ? 1 : hair;
    const u16 h = (rawHair < 13) ? kHairIdx[hairRow][rawHair] : rawHair;  // remapped sprite/palette number
    const std::string bodyBase = base + kBody + "/" + sx + "/" + job + "_" + sx;

    auto bs = vfs.read(bodyBase + ".spr");
    auto ba = vfs.read(bodyBase + ".act");
    if (bs && ba) {
        spr_[0] = mk(Sprite::parse(*bs));
        act_[0] = mk(Action::parse(*ba));
        // Clothes dye (#86): override the body palette with data/palette/몸/<job>_<sex>_<color>.pal
        // (roBrowser DBManager.getBodyPalPath). Colour 0 = default. Riding shares the body sprite, so
        // the peco body name `job` is used as-is.
        if (clothColor != 0 && spr_[0]) {
            const std::string palPath = "data/palette/" + kBodyPal + "/" + job + "_" + sx + "_" +
                                        std::to_string(clothColor) + ".pal";
            if (const auto* pal = cachedPal(vfs, palPath)) spr_[0]->setPalette(*pal);  // #150 cache
            else warnMissingDyePal("clothes", clothColor, palPath);                    // log the gap (S.)
        }
        loadPngOverrides(vfs, 0, bodyBase);  // #109: loose PNG frame replacements
        // Optional per-job .imf (data/imf/<job>_<sex>.imf): per-frame body layer priority + attach
        // offset. Parsed + cached here; the draw path reads imf_->at(layer,action,frame). readQuiet
        // because most jobs have no .imf (an absent one isn't a content fault). (S. asked for support.)
        imf_.reset();
        if (auto ib = vfs.readQuiet("data/imf/" + job + "_" + sx + ".imf"))
            if (auto parsed = Imf::parse(*ib))
                imf_ = std::make_shared<Imf>(std::move(*parsed));
    }
    // Head: try the requested hairstyle, falling back to style 1 if it is absent.
    for (u16 hh : {h, static_cast<u16>(1)}) {
        const std::string hb = base + kHead + "/" + sx + "/" + std::to_string(hh) + "_" + sx;
        auto hs = vfs.read(hb + ".spr");
        auto ha = vfs.read(hb + ".act");
        if (hs && ha) {
            spr_[1] = mk(Sprite::parse(*hs));
            act_[1] = mk(Action::parse(*ha));
            if (spr_[1] && act_[1]) {
                // Hair dye (#86): override the head sprite's palette with the per-colour .pal
                // (data/palette/머리/머리<style>_<sex>_<color>.pal) BEFORE any frame is uploaded, so
                // the hair shows the chosen colour. Colour 0 = default (keep the embedded palette).
                if (hairColor != 0) {
                    const std::string palPath = "data/palette/" + kHairPal + "/" + kHairPal +
                                                std::to_string(hh) + "_" + sx + "_" +
                                                std::to_string(hairColor) + ".pal";
                    if (const auto* pal = cachedPal(vfs, palPath)) spr_[1]->setPalette(*pal);  // #150
                    else warnMissingDyePal("hair", hairColor, palPath);                        // log the gap (S.)
                }
                loadPngOverrides(vfs, 1, hb);  // #109: loose PNG frame replacements
                break;
            }
        }
    }

    if (!ready()) {
        log::warn("CharacterActor: body sprite not found (class {}, sex {}) — slot shows text",
                  classId, sex);
        return false;
    }
    // Headgear: bottom / mid / top accessory slots (parts 2..4).
    loadHeadgear(vfs, 2, headBottom, sx);
    loadHeadgear(vfs, 3, headMid, sx);
    loadHeadgear(vfs, 4, headTop, sx);
    // Weapon (part 5): the equipped right-hand sprite at <wjob>/<wjob>_<sex>_<suffix>.
    // Mirrors roBrowser's getWeaponPath/getWeaponViewID exactly: the look value is
    // resolved to a weapon view class (weaponViewFromValue — the server may send the item
    // nameid, e.g. Knife as 1201, not the view class) and mapped to the class-name suffix
    // (단검/검/창/...). The weapon lives under the PLAIN job folder even when riding a peco
    // (roBrowser uses WeaponJobTable[job], the non-mounted name), so use jobSpriteName(),
    // NOT the peco `job`. Two attempts like roBrowser: the view-class sprite, then the raw
    // value as a numbered sprite (some special weapons ship a custom numbered .spr).
    if (weapon != 0) {
        const std::string wjob = jobSpriteName(classId);
        const u16 view = weaponViewFromValue(weapon);
        const char* cls = weaponLookName(view);
        // Ordered suffix attempts: the view-class sprite, the 1H fallback for two-handed
        // weapons (양손* is absent in the stock GRF), then the raw numeric value (some special
        // weapons ship a custom numbered .spr). First existing .spr/.act wins.
        std::vector<std::string> sufs;
        if (cls) sufs.push_back(cls);
        else     sufs.push_back(std::to_string(view));
        if (const char* alt = weaponLookAlt(view)) sufs.push_back(alt);
        sufs.push_back(std::to_string(weapon));
        // While RIDING, the weapon has its OWN sprite under the peco body's folder (e.g.
        // data/sprite/인간족/페코페코_기사/페코페코_기사_<sex>_창), drawn for the mounted pose. The
        // plain-job weapon is authored for the standing hand and lands in the wrong place on a
        // peco (S.: "нету копья на маунте"). Verified in the live GRF: the knight and crusader
        // peco folders carry the full mounted weapon set. Try the peco folder first when riding,
        // then fall back to the plain job folder — covers the standing case and the riding classes
        // whose GRF ships no dedicated mounted weapon (e.g. lord knight / paladin).
        // The mounted WEAPON folder is WeaponJobTable[mount job] = the REGULAR peco class folder,
        // NOT the 2nd-job body folder (lord knight's body 로드페코 ships no weapons; its spear is in
        // the knight's 페코페코_기사 — verified in the GRF). So resolve the weapon peco folder via
        // weaponRidingName, distinct from the body's ridingBodyName.
        const char* wpeco = riding ? weaponRidingName(classId) : nullptr;
        std::vector<std::string> wpres;
        if (wpeco)
            wpres.push_back(base + std::string(wpeco) + "/" + std::string(wpeco) + "_" + sx + "_");
        wpres.push_back(base + wjob + "/" + wjob + "_" + sx + "_");
        if (std::string wbase = weaponBaseJob(classId); !wbase.empty())  // transcended -> base job's folder
            wpres.push_back(base + wbase + "/" + wbase + "_" + sx + "_");
        std::optional<std::vector<u8>> ws, wa;
        std::string wb;
        for (const std::string& wpre : wpres) {
            for (const std::string& suf : sufs) {
                wb = wpre + suf;
                ws = vfs.read(wb + ".spr");
                wa = vfs.read(wb + ".act");
                if (!(ws && wa)) continue;
                // Reject a weapon sprite that ships ONLY the ~5 basic poses (idle/walk/sit/pickup/ready,
                // 40 actions) and NO attack motion -- some GRF 2H arts are like this (e.g. 양손창, verified
                // wpnActs=40 in S.'s log), so the swing drew nothing. Fall through to the next candidate:
                // the 1H fallback name (창) carries the full 104-action set with the attack frames.
                // (S.: копьё Lance/Javelin не рисуется при атаке.)
                if (auto chk = mk(Action::parse(*wa)); chk && chk->actions().size() <= 40) {
                    ws.reset(); wa.reset();
                    continue;
                }
                break;
            }
            if (ws && wa) break;
        }
        if (ws && wa) {
            spr_[5] = mk(Sprite::parse(*ws));
            act_[5] = mk(Action::parse(*wa));
            if (!(spr_[5] && act_[5])) {
                spr_[5].reset();
                act_[5].reset();
            } else {
                loadPngOverrides(vfs, 5, wb);  // #109: per-frame hi-res PNG override for the weapon (S.)
                // Weapon-trail overlay: RO ships a paired "<weapon>_검광" sprite (검광 = blade gleam) that
                // holds the swing-arc frames the original client draws OVER the weapon during the attack
                // motion — the real "кадры со следом оружия" S. asked for (never an invented particle).
                // Load it into slot 7; it self-gates (idle/walk frames are empty, so it shows only on the
                // swing, between the first and last attack frame). buildQuads composes it after the weapon.
                {
                    static const char* kTrailSuffix = "_\xb0\xcb\xb1\xa4";  // _검광 (cp949)
                    const std::string tb = wb + kTrailSuffix;
                    auto ts = vfs.read(tb + ".spr");
                    auto ta = vfs.read(tb + ".act");
                    if (ts && ta) {
                        spr_[7] = mk(Sprite::parse(*ts));
                        act_[7] = mk(Action::parse(*ta));
                        if (!(spr_[7] && act_[7])) { spr_[7].reset(); act_[7].reset(); }
                        else loadPngOverrides(vfs, 7, tb);
                    }
                }
                // The equipped-weapon sprite carries frames only in ONE attack variant
                // (motion 5/10/11) and the READYFIGHT pose (motion 4); idle/walk are empty
                // (sprIndex -1). roBrowser drives the weapon by the SAME action as the body,
                // so a standing PC is in the normal IDLE pose (the weapon just isn't drawn at
                // rest on this data) and only the swing shows the weapon — it must NOT stand
                // in the combat-ready stance. So keep idleMotion_ = 0 (idle) and detect only
                // the attack motion: the first of ATTACK1/2/3 whose frames carry the weapon.
                armed_ = true;
                auto motionHasWeapon = [&](int m) {
                    const int a = m * 8;  // direction 0 of that motion
                    if (a >= static_cast<int>(act_[5]->actions().size())) return false;
                    for (const auto& fr : act_[5]->actions()[a].frames)
                        for (const ActLayer& L : fr.layers)
                            if (L.sprIndex >= 0) return true;
                    return false;
                };
                idleMotion_ = 0;  // stand in the normal idle stance, not READYFIGHT
                attackMotion_ = -1;
                for (int m : {5, 10, 11})
                    if (motionHasWeapon(m)) {
                        attackMotion_ = m;
                        break;
                    }
                // Fallback: some weapons (certain dagger arts) carry the swing frames in an attack
                // motion outside the usual {5,10,11}. Scan every action that isn't a known non-attack
                // pose (0 idle,1 walk,2 sit,3 pickup,4 ready,6 hurt,8 dead) for weapon frames so the
                // weapon still draws on the swing instead of vanishing (S.: "cutter не видно при атаке").
                if (attackMotion_ < 0) {
                    const int nMotion = static_cast<int>(act_[5]->actions().size()) / 8;
                    for (int m = 0; m < nMotion; ++m) {
                        if (m == 0 || m == 1 || m == 2 || m == 3 || m == 4 || m == 6 || m == 8) continue;
                        if (motionHasWeapon(m)) { attackMotion_ = m; break; }
                    }
                }
                if (attackMotion_ < 0) attackMotion_ = 5;  // give up -> default swing
                // Diagnostic (S.: sword floats above the hand on a peco during combat). Does the loaded
                // weapon's ATTACK frame carry its own anchor? If yes, the OLD code offset it by ba-pa and
                // lifted it; the fix draws weapon/shield at the origin regardless. Logs which sprite loaded
                // (riding folder vs standing fallback) + anchor presence so we can see the real cause.
                bool atkAnchor = false;
                if (attackMotion_ >= 0) {
                    const int aa = attackMotion_ * 8;  // dir 0 of the attack motion
                    if (aa < static_cast<int>(act_[5]->actions().size()))
                        for (const auto& fr : act_[5]->actions()[aa].frames)
                            if (!fr.anchors.empty()) { atkAnchor = true; break; }
                }
                // Also log the BODY vs WEAPON action counts: if the peco body has FEWER actions than the
                // attack motion needs (attackMotion*8+dir), buildQuads CLAMPS the body to its last action
                // while the weapon (composePart) uses MODULO -> body + weapon play different frames ->
                // sword detaches on the diagonal attack (S.). bodyActs < (attackMotion+1)*8 pins this.
                const int bodyActs = act_[0] ? static_cast<int>(act_[0]->actions().size()) : 0;
                const int wpnActs = static_cast<int>(act_[5]->actions().size());
                log::info("CharacterActor: weapon '{}' attackMotion={} atkFrameAnchor={} riding={} "
                          "bodyActs={} wpnActs={} needAction>={}",
                          wb, attackMotion_, atkAnchor ? 1 : 0, riding_ ? 1 : 0, bodyActs, wpnActs,
                          attackMotion_ * 8);
            }
        }
    }
    // Shield (part 6): the equipped left-hand sprite. roBrowser DB.getShieldPath builds it
    // under the dedicated 방패 (shield) folder — NOT the 인간족 body folder — as
    // data/sprite/방패/<wjob>/<wjob>_<sex>_<shieldname>, where wjob is the SAME job-sprite
    // name the weapon uses (the plain job, jobSpriteName — not the peco/riding name) and
    // sex the same male/female suffix. shieldname comes from ShieldTable keyed by the
    // server's small shield VIEW id (1 Guard..4 Mirror, via shieldLookName). Loaded into
    // part 6 and composed at the body origin like the weapon (is_main), so it rides the
    // body frame; buildQuads draws it behind or in front of the body by facing.
    if (const u16 sview = shieldViewFromValue(shield)) {  // nameid (2101) or view (1) -> view
        if (const char* sname = shieldLookName(sview)) {
            // Like the weapon: a transcended 2nd job ships no shield sprites of its own and reuses
            // the BASE job's folder (S.: "на креаторе guard не виден" — Creator -> Alchemist). Try the
            // job's own 방패 folder first, then weaponBaseJob's.
            std::vector<std::string> sjobs;
            sjobs.push_back(jobSpriteName(classId));
            if (std::string sbase = weaponBaseJob(classId); !sbase.empty()) sjobs.push_back(sbase);
            std::optional<std::vector<u8>> ss, sa;
            std::string shbase;
            for (const std::string& sjob : sjobs) {
                shbase = "data/sprite/" + kShield + "/" + sjob + "/" + sjob + "_" + sx + "_" + sname;
                ss = vfs.read(shbase + ".spr");
                sa = vfs.read(shbase + ".act");
                if (ss && sa) break;
            }
            if (ss && sa) {
                spr_[6] = mk(Sprite::parse(*ss));
                act_[6] = mk(Action::parse(*sa));
                if (!(spr_[6] && act_[6])) {
                    spr_[6].reset();
                    act_[6].reset();
                } else {
                    loadPngOverrides(vfs, 6, shbase);  // #109: per-frame hi-res PNG override for the shield (S.)
                }
            }
        }
    }
    log::debug("CharacterActor: class {} hair {} loaded (head {}, gear {}/{}/{})", classId, hair,
               spr_[1] ? "ok" : "missing", headBottom, headMid, headTop);
    // Cache this composed look so the next identical player reuses the parsed+dyed parts. Skip a look
    // that pulled loose PNG frame overrides (#109) -- those are per-actor and can't be shared.
    if (pngOverride_.empty()) {
        for (int i = 0; i < kParts; ++i) {
            s_sprCache[appearanceKey_ + "#p" + std::to_string(i)] = spr_[i];
            s_actCache[appearanceKey_ + "#p" + std::to_string(i)] = act_[i];
        }
        s_metaCache[appearanceKey_] = {armed_ ? 1 : 0, idleMotion_, attackMotion_};
    }
    return true;
}

bool CharacterActor::loadActor(const Vfs& vfs, const std::string& name, int classId,
                               const std::string& actNameIn) {
    destroy();
    // Shared-cache identity for a single sprite: its name + folder-selecting classId + optional act
    // name fully determine the pixels, so all same-class mobs (a crowd of porings) share one upload.
    appearanceKey_ = "M:" + name + ":" + std::to_string(classId) + ":" + actNameIn;
    isPlayer_ = false;  // single mob/NPC sprite: attack uses motion 2
    // Almost every asset shares one base name for .spr and .act. A few GRF sprites split
    // them: the falcon ships as ht_falcon.spr + h_falcon.act (mismatched prefix in this
    // GRF — verified via grfinfo), so callers may pass a distinct actName. The .spr never
    // loaded before because the loader looked for ht_falcon.act, which does not exist, so
    // the bird never rendered. Empty actName => act shares the spr name (the common case).
    const std::string& actName = actNameIn.empty() ? name : actNameIn;
    // PNG sprite (#109): data/pngsprite/<name>/manifest.cfg beats every .spr/.act — brand-new
    // content authored as loose PNG frames (docs/png-sprites.md). The parser synthesizes the
    // same Sprite+Action objects, so composition/playback below is unchanged.
    {
        const std::string pngDir = "data/pngsprite/" + name + "/";
        if (auto mf = vfs.read(pngDir + "manifest.cfg")) {
            std::string err;
            auto res = parsePngSprite(std::string(mf->begin(), mf->end()),
                                      [&](const std::string& f) { return vfs.read(pngDir + f); },
                                      &err);
            if (res) {
                spr_[0] = std::make_shared<Sprite>(std::move(res->sprite));
                act_[0] = std::make_shared<Action>(std::move(res->action));
                if (ready()) {
                    log::info("CharacterActor: PNG sprite '{}' loaded (scale {})", name, res->scale);
                    return true;
                }
                spr_[0].reset();
                act_[0].reset();
                err = "no drawable frames";
            }
            log::warn("CharacterActor: PNG sprite '{}' rejected: {}", name, err);
        }
    }
    const std::string mon = "\xb8\xf3\xbd\xba\xc5\xcd";   // 몬스터 (monster sprite folder)
    const std::string item = "\xbe\xc6\xc0\xcc\xc5\xdb";  // 아이템 (item/effect folder, e.g. al_warp)
    const std::string eff = "\xc0\xcc\xc6\xd1\xc6\xae";   // 이펙트 (effect sprites, e.g. torch_01)
    const std::string npcD = "data/sprite/npc/";
    const std::string monD = "data/sprite/" + mon + "/";
    const std::string itemD = "data/sprite/" + item + "/";
    const std::string effD = "data/sprite/" + eff + "/";
    const std::string rootD = "data/sprite/";
    const std::string homunD = "data/sprite/homun/";
    // Folder priority per roBrowser DB.getBodyPath: a MONSTER (1000-3999) is the 몬스터
    // folder (npc/ has stale placeholder copies, e.g. npc/drainliar = blue familiar — so
    // it must come LAST for mobs). An NPC (46-999) is npc/. A HOMUNCULUS or MERCENARY
    // (id >= 6000, roBrowser DBManager.js:210) is data/sprite/homun/: both the homunculi
    // (lif/amistr/filir/vanilmirth + their evolved/S forms) AND the mercenaries (mer_eira,
    // mer_bayeri, ...) live in that single folder, which the loader never searched — so
    // every homunculus and mercenary fell through to the novice body. Verified against the
    // live server GRF (21 such sprites present under data/sprite/homun/). Effects/cursor
    // (classId < 0) keep the legacy order.
    std::vector<std::string> dirs;
    if (classId >= 6000)
        dirs = {homunD, monD, npcD, itemD, effD, rootD};  // homunculus / mercenary: homun first
    else if (classId >= 1000 && classId < 4000)
        dirs = {monD, npcD, itemD, effD, rootD};   // monster: 몬스터 first
    else
        dirs = {npcD, monD, itemD, effD, rootD};   // npc / effect / cursor: npc first
    // The portal mutates its Sprite (ARGB R/B swap) so it can NOT be shared; everything else is a plain
    // parsed mob/NPC sprite -> share it across identical actors via the process-wide cache (RAM).
    const bool shareable = (name != "portal");
    for (const std::string& dir : dirs) {
        const std::string sprKey = dir + name, actKey = dir + actName;
        if (shareable) {  // reuse an already-parsed copy if this exact asset path was loaded before
            auto si = s_sprCache.find(sprKey);
            auto ai = s_actCache.find(actKey);
            if (si != s_sprCache.end() && si->second && ai != s_actCache.end() && ai->second) {
                spr_[0] = si->second;
                act_[0] = ai->second;
                loadPngOverrides(vfs, 0, dir + name);  // per-actor PNG overrides stay private
                return true;
            }
        }
        auto s = vfs.read(dir + name + ".spr");
        auto a = vfs.read(dir + actName + ".act");
        if (s && a) {
            spr_[0] = mk(Sprite::parse(*s));
            // The warp portal (npc/portal) stores its truecolor frames as ARGB, not the usual
            // ABGR, so the standard decode renders the blue ripple RED (S.; proven by rendering the
            // frames both ways). Swap R/B for it only -- the brazier flame (torch_01) IS ABGR and
            // must stay orange, so this can't be a global decode change.
            if (spr_[0] && name == "portal") {
                spr_[0]->swapTruecolorRedBlue();
                spr_[0]->dropIndexedFrames();  // keep only the blue ripple, drop the stone-ring base
            }
            act_[0] = mk(Action::parse(*a));
            if (ready()) {
                if (shareable) {  // cache the parsed pair for the next identical actor
                    s_sprCache[sprKey] = spr_[0];
                    s_actCache[actKey] = act_[0];
                }
                loadPngOverrides(vfs, 0, dir + name);  // #109: loose PNG frame replacements
                return true;  // single sprite, no head composition
            }
            spr_[0].reset();
            act_[0].reset();
        }
    }
    return false;
}

void CharacterActor::destroy() {
    // Frame textures live in the process-wide shared cache (s_frameTex/s_nrmTex), NOT per-actor, so an
    // actor's teardown must NOT free them -- other actors with the same appearanceKey_ still use them.
    // They're released once via clearSharedCache() at shutdown (see below).
    pngOverride_.clear();
    contentBottomCache_.clear();  // peco content-bottom depends on part-0 pixels, which reload here
    ridingGroundCache_.clear();
    for (int i = 0; i < kParts; ++i) {
        spr_[i].reset();
        act_[i].reset();
    }
    armed_ = false;
    idleMotion_ = 0;
    attackMotion_ = 5;
}

// Probe "<base>.png.d/<i>.png" for every indexed frame (#109 level 1). A found PNG replaces the
// frame's pixels at TEXTURE level only: the quad keeps the .spr logical size (composeFrame reads
// SprFrame dims), the texture just gets denser — same trick as the hi-res map textures (#108).
void CharacterActor::loadPngOverrides(const Vfs& vfs, int part, const std::string& base) {
    if (part < 0 || part >= kParts || !spr_[part]) return;
    const std::string dir = base + ".png.d/";
    const auto& frames = spr_[part]->indexedFrames();
    int n = 0;
    for (usize i = 0; i < frames.size(); ++i) {
        // Optional #109 override -> quiet. Accept either <i>.png or <i>.webp (S.: converted-to-WebP
        // frames). decodeImage sniffs the actual format by magic bytes, so the extension is only used
        // to LOCATE the file; a .png that in fact holds WebP bytes also decodes.
        auto bytes = vfs.readQuiet(dir + std::to_string(i) + ".png");
        if (!bytes) bytes = vfs.readQuiet(dir + std::to_string(i) + ".webp");
        if (!bytes) continue;
        auto img = decodeImage(*bytes);
        if (!img || !img->valid()) {
            log::warn("CharacterActor: bad PNG/WebP override {}{}.(png|webp)", dir, i);
            continue;
        }
        pngOverride_[part * 100000 + static_cast<int>(i)] = std::move(*img);
        ++n;
    }
    if (n) log::info("CharacterActor: {} PNG frame override(s) from {}", n, dir);
}

// Process-wide shared frame-texture caches (S.): keyed by appearanceKey_ + local frame key, so N
// identical actors share ONE upload instead of N. Texture has no destructor, so these static maps
// don't free handles at program exit (bgfx::shutdown sweeps them); clearSharedCache() releases them
// cleanly at teardown. unordered_map keeps element references stable across rehash, so returning a
// Texture& into these is safe for the render path.
namespace {
std::unordered_map<std::string, Texture> s_frameTex;
std::unordered_map<std::string, Texture> s_nrmTex;
}  // namespace

void CharacterActor::clearSharedCache() {
    for (auto& [k, t] : s_frameTex) t.destroy();
    s_frameTex.clear();
    for (auto& [k, t] : s_nrmTex) t.destroy();
    s_nrmTex.clear();
    s_sprCache.clear();  // drops the cache's ref; actors still holding a shared_ptr free on their dtor
    s_actCache.clear();
    s_metaCache.clear();
}

void CharacterActor::clearActorCache() {
    // Erase every mob/NPC entry (key prefix "M:"), keeping player looks ("P..."). loadActor keys its
    // appearanceKey as "M:<name>:<class>:<act>"; frameTex/nrmTex prefix skey with that appearanceKey, so
    // the same "M:" test frees their textures too. Sprites+animations+textures all drop together, so a
    // map's mobs/NPCs cache fresh and the previous map's are released (S.: "кеш анимаций/текстур/спрайтов
    // мобов чистить при смене локации, наполнять теми, что на локации").
    auto isMob = [](const std::string& k) { return k.rfind("M:", 0) == 0; };
    for (auto it = s_frameTex.begin(); it != s_frameTex.end();) {
        if (isMob(it->first)) { it->second.destroy(); it = s_frameTex.erase(it); } else ++it;
    }
    for (auto it = s_nrmTex.begin(); it != s_nrmTex.end();) {
        if (isMob(it->first)) { it->second.destroy(); it = s_nrmTex.erase(it); } else ++it;
    }
    for (auto it = s_sprCache.begin(); it != s_sprCache.end();) {
        if (isMob(it->first)) it = s_sprCache.erase(it); else ++it;
    }
    for (auto it = s_actCache.begin(); it != s_actCache.end();) {
        if (isMob(it->first)) it = s_actCache.erase(it); else ++it;
    }
    for (auto it = s_metaCache.begin(); it != s_metaCache.end();) {
        if (isMob(it->first)) it = s_metaCache.erase(it); else ++it;
    }
}

Texture& CharacterActor::frameTex(int part, int idx, bool indexed) {
    const int key = part * 100000 + idx;  // pngOverride_ (per-actor) is still int-keyed
    const std::string skey = appearanceKey_ + (indexed ? "#i" : "#r") + std::to_string(key);
    auto it = s_frameTex.find(skey);
    if (it != s_frameTex.end()) return it->second;

    Sprite* sp = (part >= 0 && part < kParts && spr_[part]) ? &*spr_[part] : nullptr;
    Texture t;
    bool uploaded = false;  // a real pixel upload was attempted (vs. no sprite / empty frame)
    if (indexed) {
        // PNG frame override (#109): replaces the palette expansion for this frame. Truecolor,
        // so .pal dyes don't recolour it (documented CM limitation).
        if (auto ov = pngOverride_.find(key); ov != pngOverride_.end()) {
            std::vector<u8> rgba = ov->second.rgba;
            keyBlackBackground(rgba, static_cast<int>(ov->second.width),
                               static_cast<int>(ov->second.height));  // webp exported w/ opaque black bg -> transparent
            bleedEdges(rgba, static_cast<int>(ov->second.width),
                       static_cast<int>(ov->second.height));
            t.create(static_cast<u16>(ov->second.width), static_cast<u16>(ov->second.height),
                     rgba.data(), /*smooth=*/true);
            uploaded = true;
        }
    }
    if (sp && !uploaded) {
        const auto& frames = indexed ? sp->indexedFrames() : sp->rgbaFrames();
        if (static_cast<usize>(idx) < frames.size()) {
            const SprFrame& fr = frames[idx];
            std::vector<u8> rgba = indexed ? sp->indexedToRgba(idx) : fr.pixels;
            if (!indexed) {
                keyMagentaRgba(rgba);  // truecolor peco/mount frames: 0xFF00FF -> transparent
                // PNG-sprite (#109) / webp-synthesized frames may be flattened opaque RGB with a solid
                // BLACK background instead of alpha (S. 2026-08-07: black box around NPCs). Key it out.
                // Safe on magenta sprites: keyMagentaRgba already zeroed their corner, so the alpha==255
                // guard in keyBlackBackground skips them; only a truly opaque black-cornered frame is cut.
                keyBlackBackground(rgba, static_cast<int>(fr.width), static_cast<int>(fr.height));
            }
            if (fr.width > 0 && fr.height > 0 && !rgba.empty()) {
                bleedEdges(rgba, static_cast<int>(fr.width), static_cast<int>(fr.height));
                t.create(static_cast<u16>(fr.width), static_cast<u16>(fr.height), rgba.data(),
                         /*smooth=*/true);
                uploaded = true;
            }
        }
    }
    // A real upload the GPU rejected (transient out-of-memory / handle pressure during the heavy
    // map-entry burst) must NOT be cached: the render skips invalid-texture quads, so a cached dead
    // handle left a player who spawned during map entry permanently invisible until they left and
    // re-entered view (S.). Return an uncached invalid placeholder so the next frame retries the
    // upload; once it succeeds the valid texture is cached and the retry stops.
    if (uploaded && !t.valid())
        return failedTex_;
    return s_frameTex.emplace(skey, t).first->second;
}

Texture& CharacterActor::frameNrmTex(int part, int idx, bool indexed) {
    const int key = part * 100000 + idx;  // pngOverride_ (per-actor) is still int-keyed
    const std::string skey = appearanceKey_ + (indexed ? "#ni" : "#nr") + std::to_string(key);
    auto it = s_nrmTex.find(skey);
    if (it != s_nrmTex.end()) return it->second;

    // Rebuild the frame's RGBA exactly like frameTex (PNG override first, then the palette
    // expansion) and derive the normal map from its luminance (S.: sprite relief).
    Image src;
    if (indexed) {
        if (auto ov = pngOverride_.find(key); ov != pngOverride_.end()) src = ov->second;
    }
    Sprite* sp = (part >= 0 && part < kParts && spr_[part]) ? &*spr_[part] : nullptr;
    if (!src.valid() && sp) {
        const auto& frames = indexed ? sp->indexedFrames() : sp->rgbaFrames();
        if (static_cast<usize>(idx) < frames.size()) {
            const SprFrame& fr = frames[idx];
            if (fr.width > 0 && fr.height > 0) {
                src.width = fr.width;
                src.height = fr.height;
                src.rgba = indexed ? sp->indexedToRgba(idx) : fr.pixels;
            }
        }
    }
    const Image gen = src.valid() ? normalFromLuminance(src, 4.0f) : Image{};  // x2 base; Normals toggle scales the mix
    Texture t;
    bool uploaded = false;
    if (gen.valid()) {
        t.create(static_cast<u16>(gen.width), static_cast<u16>(gen.height), gen.rgba.data(),
                 /*smooth=*/true);
        uploaded = true;
    }
    if (uploaded && !t.valid()) return failedTex_;  // GPU pressure: retry next frame (see frameTex)
    return s_nrmTex.emplace(skey, t).first->second;
}

// Align an optional part (head / headgear) so its anchor coincides with the body
// anchor `ba`, then append its layers. Same rule for the head and accessories: the
// artists draw each so it sits correctly when its anchor is at the body's.
void CharacterActor::composePart(int part, int action, int frameSeed,
                                 const std::array<i32, 2>& ba, std::vector<ComposedQuad>& out,
                                 int frameSeedNext, float t) {
    if (part < 0 || part >= kParts || !spr_[part] || !act_[part] || act_[part]->actions().empty())
        return;
    const int n = static_cast<int>(act_[part]->actions().size());
    // roBrowser (EntityRender renderElement) wraps the action by the PART's own action count
    // with MODULO, not clamp: actions[(motion*8 + dir) % count]. A head/headgear sprite has
    // FEWER motions than the body (no ATTACK2/ATTACK3, motions 10/11, which two-handed weapons
    // use) — modulo by a multiple of 8 maps it back to an equivalent lower motion at the SAME
    // direction (e.g. attack10 -> readyfight4), so the head stays anchored + faces the right way.
    // clamp instead pinned it to the last action (wrong direction + anchor) so the head slid off
    // or vanished on two-handed weapons. (S. #140)
    int ai = ((action % n) + n) % n;
    // The SHIELD sprite carries visible frames only in the READYFIGHT stance (motion 4) — its attack/
    // hurt/walk frames are empty (sprIndex -1), so the shield VANISHED during combat and when the char
    // got hit (S.: "не видно щита ни в атаке, ни когда по чару бъют"). When the requested action has no
    // visible layer, fall back to READYFIGHT (motion 4) at the SAME direction so the shield keeps showing
    // (its guard pose). Weapon(5) is unaffected — its attack frames DO carry the swing.
    if (part == 6) {
        auto hasVis = [&](int a) {
            if (a < 0 || a >= n) return false;
            for (const auto& f : act_[part]->actions()[a].frames)
                for (const ActLayer& L : f.layers)
                    if (L.sprIndex >= 0) return true;
            return false;
        };
        if (!hasVis(ai)) {
            const int rf = 4 * 8 + (((action % 8) + 8) % 8);  // readyfight, same direction
            if (hasVis(rf)) ai = rf;
        }
    }
    const int fn = static_cast<int>(act_[part]->actions()[ai].frames.size());
    const int fr = fn > 0 ? (frameSeed % fn + fn) % fn : 0;
    const int frN = (frameSeedNext >= 0 && fn > 0) ? (frameSeedNext % fn + fn) % fn : -1;
    // Alignment mirrors roBrowser renderElement. The BODY and the WEAPON/shield are both is_main,
    // drawn at the SAME origin (0,0) — EntityRender.js:240,262-264,324-329: the weapon's layer.pos
    // already bakes in the hand position (identical for peco/non-peco; mounting only swaps the
    // sprite/act, no anchor branch). HEAD/headgear are non-main and attach to the body's neck:
    // offset = body.anchor - their OWN anchor. So a part WITH an anchor (head/hat) uses ba-pa; a
    // part with none (body/weapon/shield) stays at the origin. (Adding ba to the anchorless weapon
    // flung the dagger above the head — S.: "без маунта нож где-то над головой".)
    // roBrowser aligns a part by (bodyAnchor - partAnchor): the part's own anchor is placed at the body
    // frame's anchor. Head/headgear (parts 1-4) always attach this way. STANDING weapon/shield stay at
    // the origin (their layer.pos bakes the hand, and applying ba flung the dagger over the head — S.).
    // But the MOUNTED (peco) body's ATTACK frame carries a LARGE anchor (measured (8,-79)) while the peco
    // weapon frame has NONE, so drawing the weapon at the origin detaches the sword from the hand on the
    // diagonal attack (S.: "меч во время атаки с диагонали выше руки"; verified: bodyActs=wpnActs=104, no
    // desync -> it's the missing anchor alignment). So for RIDING only, attach the weapon/shield too, with
    // partAnchor = 0 when the frame has none (offset becomes the body anchor). Standing is untouched.
    // Head/headgear (parts 1-4) attach to the body anchor via ba-pa ONLY when the frame carries its own
    // anchor. Weapon(5)/shield(6) draw at the origin — their layer.pos bakes the hand. (Applying the body
    // anchor to the anchorless mounted weapon flung the sword VERY high — S.: "меч улетел очень высоко":
    // the peco body anchor is a red herring, the real mounted sword offset needs the .spr pixels to render
    // and measure the hand-vs-sword delta, which live in sprite.zip — still HD-converting, ~1 week.)
    const bool attaches = (part >= 1 && part <= 4);
    float ox = 0.0f, oy = 0.0f;
    if (attaches && fn > 0) {
        const ActFrame& pf = act_[part]->actions()[ai].frames[fr];
        // roBrowser aligns a non-main part by offset = bodyAnchor - partAnchor. A part WITH an anchor
        // (most hats) uses its own anchor; a headgear whose ACT frame carries NO anchor (some glasses/
        // masks/mustaches) is treated as partAnchor = (0,0) -> offset = bodyAnchor, so its sprite origin
        // lands on the body's NECK. Previously it was left at (0,0) = the body origin, which dropped the
        // accessory down to the FEET (S.: "очки и маски были ниже чаров" / "усы уехали вниз"). Attaching
        // to the neck matches how the anchored hats sit and how roBrowser places an anchorless part.
        float pax = 0.0f, pay = 0.0f;
        // An ANCHORLESS headgear frame is a FACIAL accessory (glasses / mask / mustache — the b003710c
        // case). Those have no rear frames authored to sit right and drop below the char on the
        // back-facing octants (S.: "усы падают под чара сзади в 3х проекциях"). A face accessory isn't
        // visible from behind anyway, so simply don't draw it on the 3 rear directions NW/N/NE
        // (dir 3/4/5 = exactly "в 3х проекциях"). Anchored hats are untouched (they stay visible from
        // behind); front/side views are unchanged.
        // Facial accessories live in the LOWER (part 2: masks / mustache / pipe) and MIDDLE (part 3:
        // glasses / sunglasses) slots. They aren't visible from behind, and their rear ACT frames often
        // carry no (or a degenerate) anchor, so they lose their attach point and slide down to the feet
        // (S.: "лицевые итемы съезжают вниз при ракурсе сзади, теряют точку привязки"). Hide the whole
        // face slot on the 3 rear octants NW/N/NE (dir 3/4/5 = exactly "в 3х положениях"); also keep the
        // old anchorless guard for any other slot. Hats (top slot, part 4) stay visible from behind.
        {
            const int dir = ((action % 8) + 8) % 8;
            if (dir >= 3 && dir <= 5 && (part == 2 || part == 3 || pf.anchors.empty()))
                return;
        }
        if (!pf.anchors.empty()) {
            // Interpolate the part's own attach anchor toward the next frame so the head/hat glides
            // with the body instead of snapping (ba is already the interpolated body anchor).
            pax = static_cast<float>(pf.anchors[0][0]);
            pay = static_cast<float>(pf.anchors[0][1]);
            if (frN >= 0 && frN != fr && t > 0.0f) {
                const ActFrame& pfN = act_[part]->actions()[ai].frames[frN];
                if (!pfN.anchors.empty()) {
                    pax = (1.0f - t) * pax + t * static_cast<float>(pfN.anchors[0][0]);
                    pay = (1.0f - t) * pay + t * static_cast<float>(pfN.anchors[0][1]);
                }
            }
        }
        ox = static_cast<float>(ba[0]) - pax;
        oy = static_cast<float>(ba[1]) - pay;
    }
    composeFrame(*act_[part], *spr_[part], ai, fr, ox, oy, part, out, frN, t);
}

// Compose one frame of `action` (motion*8 + dir): body, then the head and headgear
// anchored to the body. `frameSeed` selects the animation frame (wrapped per part).
void CharacterActor::buildQuads(int action, int frameSeed, std::vector<ComposedQuad>& out,
                                int frameSeedNext, float t) {
    const int ba_n = static_cast<int>(act_[0]->actions().size());
    // Wrap the BODY action by its own count with MODULO, exactly like composePart does for the head/
    // weapon (roBrowser EntityRender). When the requested action is valid (< ba_n) this is a no-op ==
    // clamp; it only differs when the body has FEWER actions than the motion needs — e.g. a peco body
    // with no ATTACK2 (motion 10) while the weapon sprite DOES: clamp pinned the body to its last action
    // (a wrong pose) while the weapon (modulo) played motion 10, so the two desynced and the sword
    // detached above the hand on the diagonal attack (S.: "меч во время атаки с диагонали выше руки").
    // Modulo keeps the body wrapping consistently with the parts. (S. #140 fixed the head the same way.)
    const int ba_i = ba_n > 0 ? ((action % ba_n) + ba_n) % ba_n : 0;
    const int bf_n = static_cast<int>(act_[0]->actions()[ba_i].frames.size());
    const int bframe = bf_n > 0 ? (frameSeed % bf_n + bf_n) % bf_n : 0;
    const int bframeN = (frameSeedNext >= 0 && bf_n > 0) ? (frameSeedNext % bf_n + bf_n) % bf_n : -1;
    // Interpolate the BODY anchor toward the next frame so the parts attached to it (head/hat/weapon)
    // glide with it. Non-interp (bframeN<0) keeps ba exactly = frameAnchor(bframe).
    auto ba = frameAnchor(*act_[0], ba_i, bframe);
    if (bframeN >= 0 && bframeN != bframe && t > 0.0f) {
        const auto baN = frameAnchor(*act_[0], ba_i, bframeN);
        ba[0] = static_cast<i32>((1.0f - t) * ba[0] + t * baN[0] + 0.5f);
        ba[1] = static_cast<i32>((1.0f - t) * ba[1] + t * baN[1] + 0.5f);
    }
    // Shield z-order depends on facing, exactly like roBrowser (Entity.js getEntityDepth /
    // renderElement): the left-hand shield draws BEHIND the body for the back/side-facing
    // octants and IN FRONT for the front-facing ones. dir = action % 8; behind when
    // dir ∈ {2,3,4,5} (roBrowser: direction > 1 && direction < 6). It is composed at the
    // body origin like the weapon (is_main), so it uses the same body anchor `ba`.
    const int dir = ((action % 8) + 8) % 8;
    const bool shieldBehind = (dir >= 2 && dir <= 5);
    if (shieldBehind) composePart(6, action, frameSeed, ba, out, frameSeedNext, t);  // shield behind
    composeFrame(*act_[0], *spr_[0], ba_i, bframe, 0, 0, 0, out, bframeN, t);
    composePart(5, action, frameSeed, ba, out, frameSeedNext, t);  // weapon (held in the body's hand anchor)
    composePart(7, action, frameSeed, ba, out, frameSeedNext, t);  // weapon trail (검광), over the weapon; empty off-swing
    composePart(1, action, frameSeed, ba, out, frameSeedNext, t);  // head
    composePart(2, action, frameSeed, ba, out, frameSeedNext, t);  // headgear bottom
    composePart(3, action, frameSeed, ba, out, frameSeedNext, t);  // headgear mid
    composePart(4, action, frameSeed, ba, out, frameSeedNext, t);  // headgear top
    if (!shieldBehind) composePart(6, action, frameSeed, ba, out, frameSeedNext, t);  // shield in front
}

float CharacterActor::bodyContentBottomPx(int action, int frame) {
    const int key = action * 10000 + frame;
    if (auto it = contentBottomCache_.find(key); it != contentBottomCache_.end()) return it->second;
    std::vector<ComposedQuad> tmp;
    buildQuads(action, frame, tmp);
    Sprite* sp = spr_[0] ? &*spr_[0] : nullptr;
    float bottom = -1e9f;
    if (sp) {
        for (const ComposedQuad& q : tmp) {
            if (q.part != 0) continue;
            const auto& frames = q.indexed ? sp->indexedFrames() : sp->rgbaFrames();
            if (static_cast<usize>(q.sprIndex) >= frames.size()) continue;
            const SprFrame& fr = frames[q.sprIndex];
            if (fr.width == 0 || fr.height == 0) continue;
            const std::vector<u8> rgba = q.indexed ? sp->indexedToRgba(q.sprIndex) : fr.pixels;
            const int W = fr.width, H = fr.height;
            if (static_cast<usize>(W) * H * 4 > rgba.size()) continue;
            int lowest = -1;  // lowest row (from top) that has any visible pixel
            for (int row = H - 1; row >= 0 && lowest < 0; --row) {
                const u8* p = rgba.data() + static_cast<usize>(row) * W * 4;
                for (int col = 0; col < W; ++col, p += 4) {
                    if (p[3] < 8) continue;  // transparent
                    if (p[0] > 160 && p[1] < 64 && p[2] > 160) continue;  // magenta key (truecolor peco)
                    lowest = row;
                    break;
                }
            }
            if (lowest >= 0) {
                const float scale = q.h / static_cast<float>(H);
                bottom = std::max(bottom, q.y + (lowest + 1) * scale);
            } else {
                bottom = std::max(bottom, q.y + q.h);
            }
        }
    }
    if (bottom <= -1e8f) bottom = 0.0f;
    contentBottomCache_[key] = bottom;
    return bottom;
}

float CharacterActor::ridingGroundPx(int refAction, int nframes) {
    if (auto it = ridingGroundCache_.find(refAction); it != ridingGroundCache_.end()) return it->second;
    float g = -1e9f;
    for (int f = 0; f < std::max(1, nframes); ++f) g = std::max(g, bodyContentBottomPx(refAction, f));
    if (g <= -1e8f) g = 0.0f;
    ridingGroundCache_[refAction] = g;
    return g;
}

void CharacterActor::drawQuads(SpriteBatch& sb, const std::vector<ComposedQuad>& quads, float x,
                               float y, float scale) {
    for (const ComposedQuad& q : quads) {
        Texture& t = frameTex(q.part, q.sprIndex, q.indexed);
        if (!t.valid()) continue;
        const u32 abgr = (static_cast<u32>(q.a) << 24) | (static_cast<u32>(q.b) << 16) |
                         (static_cast<u32>(q.g) << 8) | q.r;
        const float dx = x + q.x * scale, dy = y + q.y * scale;
        const float dw = q.w * scale, dh = q.h * scale;
        const float u0 = q.mirror ? 1.0f : 0.0f, u1 = q.mirror ? 0.0f : 1.0f;
        sb.draw(dx, dy, dw, dh, u0, 0.0f, u1, 1.0f, abgr, t);
    }
}

// 2D path (char-select slots): origin = the actor anchor at (x,y), plain scale,
// facing south (action 0) so the portrait looks at the player.
void CharacterActor::renderScaled(SpriteBatch& sb, float x, float y, float scale, double time) {
    if (!ready() || act_[0]->actions().empty()) return;
    (void)time;
    std::vector<ComposedQuad> quads;
    buildQuads(0, 0, quads);
    drawQuads(sb, quads, x, y, scale);
}

bool CharacterActor::hasRenderableBody() const {
    return spr_[0] && act_[0] &&
           (!spr_[0]->indexedFrames().empty() || !spr_[0]->rgbaFrames().empty());
}

void CharacterActor::renderActionFrame(SpriteBatch& sb, float x, float y, int action, int frame,
                                       float scale) {
    if (!ready() || act_[0]->actions().empty()) return;
    std::vector<ComposedQuad> quads;
    buildQuads(action, frame, quads);  // buildQuads wraps `frame` to the action's length
    drawQuads(sb, quads, x, y, scale);
}

void CharacterActor::render(SpriteBatch& sb, float x, float y, float pxPerWorldUnit, u8 dir,
                            double time) {
    if (!ready() || act_[0]->actions().empty()) return;
    (void)time;
    std::vector<ComposedQuad> quads;
    buildQuads(dir & 7, 0, quads);  // RO idle motion: action index == direction (0..7)

    // Size the ~92px body to a fixed world height (see kWorldPerPx) so the
    // character is correctly proportioned to the map at any zoom/resolution.
    const float scale = pxPerWorldUnit * kWorldPerPx;

    // Anchor the FEET (body bbox bottom) at (x,y) and centre on the body, so the
    // character stands on the projected ground cell regardless of head/equipment.
    float feet = 0.0f, left = 1e9f, right = -1e9f;
    for (const ComposedQuad& q : quads) {
        if (q.part != 0) continue;  // body only: head/equipment must not move the feet
        feet = std::max(feet, q.y + q.h);
        left = std::min(left, q.x);
        right = std::max(right, q.x + q.w);
    }
    const float cx = (left <= right) ? (left + right) * 0.5f : 0.0f;
    drawQuads(sb, quads, x - cx * scale, y - feet * scale, scale);
}

void CharacterActor::renderWorld(const WorldSpritePass& pass, const Vec3& feetWorld, u8 dir,
                                 Anim anim, double time, double animStart, float depthBias,
                                 float alpha, float sizeScale, bool originAnchor, bool additive,
                                 bool onTop, bool flat, float roll, const Vec3& envLight,
                                 bool interpolate) {
    if (!ready() || act_[0]->actions().empty() || !bgfx::isValid(pass.program) || !pass.layout)
        return;
    // RO action index = motion*8 + dir. Walk = motion 1 for every actor. A composed PC
    // body uses idleMotion_ at rest (4/READYFIGHT when armed, so the weapon shows; 0 when
    // bare-handed) and attackMotion_ to swing (the ATTACK1/2/3 variant the weapon draws);
    // a monster/NPC idles at 0 and attacks at motion 2. The head/headgear/weapon use the
    // same action+frame to stay anchored to the body.
    // Player body action indices (S.'s authoritative table for this sprite set): 0 idle, 1 walk, 2 sit,
    // 3 pickup, 4 ready/standby (held up to 5s after an attack), 6 hurt, 8 dead, 10 bare-hand attack,
    // 11 weapon attack, 12 non-attack cast. Monsters/NPCs keep their own (walk 1, attack 2, hurt/dead).
    const int motion = (anim == Anim::Walk)     ? 1
                       // Attack action is PER-WEAPON (roBrowser WeaponAction: index -> [ATTACK1 5, ATTACK2
                       // 10, ATTACK3 11]; unarmed->5, bow/sword->10, spear->11). attackMotion_ detects the
                       // right one from the weapon's ACT; a fixed 11 broke bow anims like Double Strafe (S.).
                       : (anim == Anim::Attack) ? (isPlayer_ ? attackMotion_ : 2)
                       : (anim == Anim::Cast)   ? (isPlayer_ ? 12 : 2)
                       : (anim == Anim::Ready)  ? (isPlayer_ ? 4 : idleMotion_)
                       : (anim == Anim::Hurt)   ? (isPlayer_ ? 6 : 3)
                       : (anim == Anim::Sit)    ? 2
                       : (anim == Anim::Dead)   ? (isPlayer_ ? 8 : 4)
                       : (anim == Anim::Idle)   ? (isPlayer_ ? 0 : idleMotion_)  // player idles relaxed (0)
                                                : 0;
    int action = motion * 8 + (dir & 7);
    const int nActions = static_cast<int>(act_[0]->actions().size());  // >=1 (checked above)
    if (action >= nActions)
        action = (dir & 7) % nActions;  // motion group absent -> idle dir, wrapped into range

    // Frame timing from the ACT per-action delay, mirroring roBrowser: the stored delay
    // float is milliseconds-per-frame / 25, so ms = delay*25 (default 150ms when a file
    // predates per-action delays). Idle and Sit hold frame 0 (RO's idle body is static
    // bar the head's doridori); Walk loops on a continuous clock so it never hitches
    // between move segments; Attack/Hurt/Dead play ONCE from `animStart` and clamp to
    // the last frame — so a swing runs front-to-back in step with the body (the weapon
    // layer rides the very same frame, no longer starting mid-swing on a stray frame)
    // and a corpse freezes on its final death frame.
    const ActAction& aa = act_[0]->actions()[action];  // current facing: frames/nf come from here
    // A looping MONSTER anim (falcon idle/flight, poring bounce, ...) must play at the SAME rate in every
    // facing (S.: "во всех ракурсах должно быть одинаково"). RO ACT stores a per-direction delay, so
    // different facings hitched / ran at different speeds. Take the delay from DIRECTION 0 of this motion
    // (action rounded down to a multiple of 8) for looping non-player anims, so all 8 facings are uniform.
    const bool monLoopAnim = !isPlayer_ && (anim == Anim::Walk || anim == Anim::Idle || anim == Anim::Effect);
    const float refDelay = monLoopAnim ? act_[0]->actions()[(action / 8) * 8].delay : aa.delay;
    float delayMs = (refDelay > 0.0f) ? refDelay * 25.0f : 150.0f;
    // The peco mount's walk plays a touch slow at the stored delay. 2x was too fast (S.); run it at
    // 1.5x instead.
    if (riding_ && anim == Anim::Walk) delayMs /= 1.5f;
    const int nf = static_cast<int>(aa.frames.size());
    int frame = 0;
    // A PC body and any Sit hold frame 0 (the RO idle body is static bar the head's
    // doridori), but a MONSTER animates its idle in place — poring bounces, a familiar
    // flaps its wings — so a mob's Idle loops like Walk/Effect (roBrowser plays the idle
    // action for non-PC entities). Attack/Hurt/Dead play once from animStart and clamp.
    // holdIdle_ (the merchant cart): a non-player companion that must FREEZE when the owner stands
    // still — RO spins the pushcart wheels only while walking (S.: "телега крутит колёсами стоя").
    const bool holds = anim == Anim::Sit || (anim == Anim::Idle && isPlayer_) ||
                       (anim == Anim::Ready && isPlayer_) ||
                       (anim == Anim::Idle && holdIdle_);   // cart idle = static
    const bool loops = anim == Anim::Walk || anim == Anim::Effect || anim == Anim::Cast ||
                       (anim == Anim::Idle && !isPlayer_ && !holdIdle_);  // mob idle animates; cart doesn't
    // Frame interpolation (S., opt-in): frac is the sub-frame progress toward frameNext; when off it
    // stays 0 (frameNext<0), so buildQuads reproduces the exact snap behaviour.
    const bool doInterp = interpolate || frameInterp_;
    float frac = 0.0f;
    int frameNext = -1;
    if (nf > 0 && !holds) {
        if (loops && monLoopAnim) {
            // Uniform loop for a monster: every facing completes its whole cycle in the SAME wall-clock
            // time -- the loop PERIOD is fixed from direction 0 (nf0 frames * dir-0 delay) and the current
            // facing's `nf` frames are spread evenly across it. Fixes facings that have MORE frames looking
            // slower even after the per-frame delay was unified (S.: "на диагональном виде сзади сокол
            // медленно крыльями машет"). Also keeps the loop-boundary wrap (%nf) so no hitch.
            const int nf0 = std::max(1, static_cast<int>(act_[0]->actions()[(action / 8) * 8].frames.size()));
            const double periodMs = std::max(1.0, static_cast<double>(nf0) * delayMs);
            const double loopPos = std::fmod(time * 1000.0, periodMs) / periodMs;  // 0..1 within the period
            const double fpos = loopPos * nf;                                       // 0..nf
            frame = static_cast<int>(fpos) % nf;
            if (doInterp && nf > 1) { frac = static_cast<float>(fpos - std::floor(fpos)); frameNext = (frame + 1) % nf; }
        } else if (loops) {  // continuous loop (players etc.): per-frame-delay clock
            const double phase = delayMs > 0.0f ? time * 1000.0 / delayMs : 0.0;
            frame = static_cast<int>(phase) % nf;
            // Wrap the interp target: after the LAST frame a continuous loop returns to frame 0, not
            // frame nf (out of range). Without the %nf the loop boundary interpolated to an invalid
            // frame every cycle -> a periodic hitch on looping sprites (S.: "анимация у сокола
            // подтормаживает"; also poring idle / torch flames).
            if (doInterp && nf > 1) { frac = static_cast<float>(phase - std::floor(phase)); frameNext = (frame + 1) % nf; }
        } else {
            const double elapsedMs = (time - animStart) * 1000.0;
            const double fp = delayMs > 0.0f ? elapsedMs / delayMs : 0.0;
            const int f = static_cast<int>(fp < 0.0 ? 0.0 : fp);
            frame = std::min(f, nf - 1);  // play once, then hold the last frame
            if (doInterp && frame < nf - 1) { frac = static_cast<float>(fp - std::floor(fp)); frameNext = frame + 1; }
        }
    }
    std::vector<ComposedQuad> quads;
    buildQuads(action, frame, quads, frameNext, frac);
    // Shield (part 6) is drawn ONLY during an attack swing or a defensive flinch, not while walking or
    // standing (S.: "при ходьбе щит не должен показываться, только во время атаки/защиты").
    if (anim != Anim::Attack && anim != Anim::Hurt)
        quads.erase(std::remove_if(quads.begin(), quads.end(),
                                   [](const ComposedQuad& q) { return q.part == 6; }),
                    quads.end());

    // Vertical anchor (feetPx) must stay constant as the camera spins the unit through its 8 facing
    // sprites, or a standing unit bobs up and down by the per-direction difference in the body's
    // lowest pixel (S.: "нпц под разными углами отрисованы на разной высоте" — Prontera guards).
    // Measure the feet from the SOUTH-facing (dir 0) body of the SAME motion+frame: the default
    // camera looks south, so feetPx there is unchanged (heights S. already approved stay put) while
    // every rotated facing now grounds to that same height. The walk bounce (frame) is preserved.
    // Death anchors its feet to FRAME 0, not the current frame: a monster's death frames squash the
    // body downward (its lowest pixel drops, so per-frame re-grounding would shove the whole billboard
    // UP to keep that pixel on the ground -> the corpse "climbs" into the air instead of collapsing,
    // S.: "остатки взлетают а потом растворяются"). A fixed frame-0 anchor lets the squash play down.
    const int feetFrame = (anim == Anim::Dead) ? 0 : frame;
    float feetPx = 0.0f;
    {
        int refAction = motion * 8;  // same motion group, direction 0
        if (refAction >= nActions) refAction = 0;
        if (riding_) {
            // Peco mount: ground by the CONTENT bottom (lowest opaque pixel), taken as the LOWEST
            // extent across all dir-0 frames of this motion, so the mounted char stands on ONE steady
            // line and never bobs. Grounding by bbox bottom (q.y+q.h) made it "jump" while walking —
            // the peco frames pad transparent space below the feet differently front/back/mid-step, so
            // the bbox drifted even though the drawn feet did not (S.: "спрайт чара на пеко прыгает").
            const int rfN = (refAction < nActions)
                                ? static_cast<int>(act_[0]->actions()[refAction].frames.size())
                                : 1;
            feetPx = ridingGroundPx(refAction, rfN);
        } else if (refAction == action && feetFrame == frame) {
            for (const ComposedQuad& q : quads)
                if (q.part == 0) feetPx = std::max(feetPx, q.y + q.h);
        } else {
            std::vector<ComposedQuad> ref;
            buildQuads(refAction, feetFrame, ref);
            for (const ComposedQuad& q : ref)
                if (q.part == 0) feetPx = std::max(feetPx, q.y + q.h);
        }
    }
    // Horizontal/vertical SPAN from the actual facing, for the centre and the mouse pick box.
    float left = 1e9f, right = -1e9f;
    float topPx = 1e9f, botPx = -1e9f;  // true (unclamped) vertical span, for the pick box
    for (const ComposedQuad& q : quads) {
        if (q.part != 0) continue;
        left = std::min(left, q.x);
        right = std::max(right, q.x + q.w);
        topPx = std::min(topPx, q.y);
        botPx = std::max(botPx, q.y + q.h);
    }
    // Pin the sprite's horizontal anchor on the ACT origin (0,0), NOT the per-facing body
    // bbox centre. The origin is the actual feet/pivot and is stable across the 8 facing
    // sprites, whereas the bbox centre shifts a few px from one facing to the next; recentring
    // on it made a STATIONARY unit orbit its own cell as spinning the camera cycled the facing
    // sprite (S.: "когда камеру крутишь, нпц крутится по радиусу, точка привязки сбоку"). roBrowser
    // draws each layer at L.x from the anchor with no recentre — this matches it. The vertical
    // still stands the unit on its feet (feetPx) so heights are unchanged.
    float cx = 0.0f;
    // An effect sprite also drops the feet-stand so it lands exactly where the map author placed
    // it (the brazier bowl) with its designed lean — the way roBrowser draws effects.
    if (originAnchor) feetPx = 0.0f;
    // Record the drawn body extent (world units, relative to feetWorld) for mouse picking.
    // Mirrors the draw's vertical map worldY(pixelY) = (feetPx - pixelY) * wpx: the body
    // bottom (botPx) lands at feetWorld for a grounded unit and ABOVE it for a floating mob
    // (drainliar), and the top (topPx) gives the sprite height. pickActor sizes the click box
    // to this so the hittable region equals the image, not a box pinned to the ground cell.
    if (!originAnchor && left <= right) {
        const float wpx = kWorldPerPx * sizeScale;
        pickHiY_ = (feetPx - topPx) * wpx;
        pickLoY_ = (feetPx - botPx) * wpx;
        pickHalfW_ = std::max(std::fabs(left - cx), std::fabs(right - cx)) * wpx;
    }

    struct V {
        f32 x, y, z, u, v;
        u32 abgr;
    };
    // LEQUAL (not LESS) so coplanar parts drawn in back-to-front order (body, head,
    // headgear, weapon) each pass against the equal depth of the one below and layer
    // correctly, and so feet sharing the ground's depth aren't clipped — no base
    // depth bias is needed for that any more. The remaining bias is per-CALL
    // (`depthBias`): 0 makes buildings honestly occlude the sprite, which every unit
    // (NPCs/mobs and the own player alike) now uses so nobody shows through walls.
    // A fading corpse (alpha < 1) alpha-blends and does NOT write depth/alpha, so it
    // dissolves cleanly; a normal sprite stays opaque and depth-writing as before.
    const bool fading = alpha < 0.999f;
    // Pick the blend/depth state. additive: glow blend for flame/light effects (depth-tested so
    // geometry in front still hides it, no Z write). onTop: feet visible from the camera -> draw
    // over the whole world (DEPTH_TEST_ALWAYS, no Z write) so the head never clips into a wall the
    // actor stands in front of and doesn't pollute other actors' feet-occlusion tests. fading: a
    // dying corpse alpha-blends without depth/Z write so it dissolves cleanly. (These cases are
    // mutually exclusive in practice — effects pass additive, actors pass onTop, corpses fade.)
    const u64 state =
        fading     ? (BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ALPHA)
        : additive ? (BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ADD)
        // onTop draws over the world (ALWAYS) so the sprite never clips into a wall it stands in front
        // of -- but it must STILL WRITE_Z at its true depth, or skill particles drawn afterwards have no
        // character depth to test against and bleed THROUGH the char (S.: "эффекты просвечиваются через
        // чаров"). Writing depth here doesn't change the color (ALWAYS still draws over walls).
        : onTop    ? (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                      BGFX_STATE_DEPTH_TEST_ALWAYS)
                   : (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                      BGFX_STATE_DEPTH_TEST_LEQUAL);
    // A negligible per-layer tie-breaker kept as a safety net for part order; far too
    // small to lift a sprite in front of a building.
    constexpr float kLayerBias = 0.0008f;
    // flat (a misnomer now): stand the quad UPRIGHT — its "up" is world +Y, its "right" follows the
    // camera horizontally — so a warp portal STANDS vertically and faces you, instead of lying flat
    // on the floor or tilting back with the camera pitch (S.: "повернуть по x/y, чтобы вверх встал, а
    // то плашмя лежит"). Otherwise it's a normal camera billboard (tilts with pass.up).
    Vec3 R = pass.right;
    Vec3 U = flat ? Vec3{0.0f, 1.0f, 0.0f} : pass.up;
    // Roll the billboard within its own plane (the flying arrow points along its flight).
    // Rotation of the camera basis keeps it a right-handed pair, so winding is preserved.
    if (roll != 0.0f) {
        const float cs = std::cos(roll), sn = std::sin(roll);
        const Vec3 R0 = R, U0 = U;
        R = Vec3{R0.x * cs + U0.x * sn, R0.y * cs + U0.y * sn, R0.z * cs + U0.z * sn};
        U = Vec3{U0.x * cs - R0.x * sn, U0.y * cs - R0.y * sn, U0.z * cs - R0.z * sn};
    }
    // Feet sit exactly on the world cell; the vs_sprite3d shader applies a
    // clip-space depth bias so the flat sprite doesn't clip into adjacent models
    // (no world-space nudge needed, so no positional offset or float).
    const Vec3 O = feetWorld;
    auto world = [&](float xi, float yj) -> Vec3 {
        return {O.x + R.x * xi + U.x * yj, O.y + R.y * xi + U.y * yj, O.z + R.z * xi + U.z * yj};
    };

    // Each composed layer becomes a camera-facing quad on the actor's billboard
    // plane (feet at the world cell), sized in world units so perspective scales
    // it and the map depth-buffer occludes it. fs_sprite3d alpha-cuts + fades the sprite.
    for (usize qi = 0; qi < quads.size(); ++qi) {
        const ComposedQuad& q = quads[qi];
        Texture& t = frameTex(q.part, q.sprIndex, q.indexed);
        if (!t.valid()) continue;
        const float wpx = kWorldPerPx * sizeScale;  // sizeScale enlarges effects (warp portal)
        const float xL = (q.x - cx) * wpx, xR = (q.x + q.w - cx) * wpx;
        const float yT = (feetPx - q.y) * wpx, yB = (feetPx - (q.y + q.h)) * wpx;
        const Vec3 tl = world(xL, yT), tr = world(xR, yT), br = world(xR, yB), bl = world(xL, yB);
        const float uL = q.mirror ? 1.0f : 0.0f, uR = q.mirror ? 0.0f : 1.0f;

        if (bgfx::getAvailTransientVertexBuffer(4, *pass.layout) < 4 ||
            bgfx::getAvailTransientIndexBuffer(6) < 6)
            continue;
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, 4, *pass.layout);
        bgfx::allocTransientIndexBuffer(&tib, 6);
        V* vd = reinterpret_cast<V*>(tvb.data);
        // The ACT layer carries an RGBA tint (q.r/g/b/a) that RO multiplies onto the sprite —
        // e.g. drainliar.act tints the purple sprite RED (250,69,96), type-bug greens, etc. Feed
        // it as the vertex colour so fs_sprite3d multiplies the texel by it. White
        // (255,255,255,255 — the vast majority of layers) is identity, so only genuinely tinted
        // mobs recolour; PCs/effects are unchanged. (S.: "use the act files, the mouse is red".)
        // Dim the sprite by the ground cell's environment light (#118): a char/mob in a building's
        // shadow darkens. Not for additive effects (they're emissive). Default white = identity.
        u8 cr = q.r, cg = q.g, cb = q.b;  // NB: tr/br/tl/bl are the quad corners above — don't shadow them
        if (!additive) {
            cr = static_cast<u8>(std::clamp(static_cast<float>(q.r) * envLight.x, 0.0f, 255.0f));
            cg = static_cast<u8>(std::clamp(static_cast<float>(q.g) * envLight.y, 0.0f, 255.0f));
            cb = static_cast<u8>(std::clamp(static_cast<float>(q.b) * envLight.z, 0.0f, 255.0f));
        }
        const u32 col = (static_cast<u32>(q.a) << 24) | (static_cast<u32>(cb) << 16) |
                        (static_cast<u32>(cg) << 8) | static_cast<u32>(cr);
        vd[0] = {tl.x, tl.y, tl.z, uL, 0.0f, col};
        vd[1] = {tr.x, tr.y, tr.z, uR, 0.0f, col};
        vd[2] = {br.x, br.y, br.z, uR, 1.0f, col};
        vd[3] = {bl.x, bl.y, bl.z, uL, 1.0f, col};
        u16* id = reinterpret_cast<u16*>(tib.data);
        id[0] = 0; id[1] = 1; id[2] = 2; id[3] = 0; id[4] = 2; id[5] = 3;
        // Scale the bias with zoom: a fixed clip-space bias is a fixed NDC shift, which
        // at distance (zoomed out) becomes a large WORLD-depth pull. Shrinking it with
        // zoom keeps the world-depth offset (the own-player "always visible" lift)
        // roughly constant. Part order survives.
        // Bias by the DRAW-ORDER index (qi), NOT the part id: buildQuads already emits the layers
        // back-to-front for THIS facing — including the shield BEFORE the body for the back/side
        // octants (roBrowser `behind`, dir 2..5). Biasing by q.part instead put the shield (part 6)
        // permanently nearest the camera, so with depth-write LEQUAL the body failed the test behind
        // it and the shield drew OVER the char's back — worst at max zoom where the world-depth pull
        // is largest (S.: "щит рисуется поверх чара при виде сзади"). Index bias matches painter order.
        const float biasVec[4] = {
            (depthBias + static_cast<float>(qi) * kLayerBias) * pass.biasScale, 0.0f, 0.0f, 0.0f};
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        // Objects (character/NPC/mob sprite) filter from Setup Video (#104): 0=point, else linear.
        const u32 oFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                           (g_objectFilterMode == 0 ? BGFX_SAMPLER_POINT : 0u);
        bgfx::setTexture(0, pass.sampler, t.handle(), oFlags);
        if (bgfx::isValid(pass.bias)) bgfx::setUniform(pass.bias, biasVec);
        // Always set the fade (the fs_sprite3d shader reads it): 1.0 for a living actor
        // keeps it byte-for-byte opaque, < 1.0 dissolves a dying corpse.
        if (bgfx::isValid(pass.fade)) {
            const float fadeVec[4] = {alpha, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(pass.fade, fadeVec);
        }
        bgfx::setState(state);
        // Lit-sprite path (S.: sprite relief): non-additive quads with a valid luminance-derived
        // normal map draw through fs_spritelit (normal on slot 1, sun dir as a uniform). Additive
        // quads (effects) are emissive and keep the plain shader; strength 0 also keeps it.
        if (bgfx::isValid(pass.litProgram) && !additive && pass.lightTS[3] > 0.001f) {
            Texture& nt = frameNrmTex(q.part, q.sprIndex, q.indexed);
            if (nt.valid()) {
                bgfx::setTexture(1, pass.nrmSampler, nt.handle(), oFlags);
                if (bgfx::isValid(pass.lightUniform)) bgfx::setUniform(pass.lightUniform, pass.lightTS);
                bgfx::submit(pass.view, pass.litProgram);
                continue;
            }
        }
        bgfx::submit(pass.view, pass.program);
    }
}

// External-linkage setter (declared in CharacterActor.hpp, called by Application). Defined OUT of the
// file's anonymous namespace so it isn't internal; reaches the anon-namespace s_jobResolver by name.
void setJobSpriteResolver(std::function<std::string(u16)> f) { s_jobResolver = std::move(f); }

} // namespace uaro
