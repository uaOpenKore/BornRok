// Auto-generated from the server's spawned MVPs (tools/gen_mvp_spawns.py) -- do not edit.
// (map, mob id) -> respawn delay in ms (the spawn line's delay1; the server has no random window). 44 spawns.
#include <cstdint>
#include <cstring>

namespace uaro {

struct MvpSpawn { const char* map; std::uint16_t mob; std::uint32_t delayMs; };
static const MvpSpawn kMvpSpawns[] = {
    {"abbey02", 1871, 7200000},
    {"abyss_03", 1719, 10800000},
    {"ama_dun03", 1492, 5400000},
    {"anthell02", 1147, 7200000},
    {"ayo_dun02", 1688, 7200000},
    {"beach_dun", 1583, 7200000},
    {"ein_dun02", 1623, 7500000},
    {"gef_dun01", 1389, 3600000},
    {"gef_dun02", 1046, 7200000},
    {"gef_fild02", 1087, 86400000},
    {"gef_fild10", 1190, 7200000},
    {"gef_fild14", 1087, 3600000},
    {"gl_chyard", 1272, 3600000},
    {"gld_dun01", 1115, 28800000},
    {"gld_dun02", 1046, 28800000},
    {"gld_dun03", 1147, 28800000},
    {"gld_dun04", 1272, 28800000},
    {"gon_dun03", 1418, 5400000},
    {"in_sphinx5", 1157, 3600000},
    {"jupe_core", 1685, 7200000},
    {"kh_dun02", 1734, 7200000},
    {"lhz_dun02", 1658, 7200000},
    {"lou_dun03", 1630, 7000000},
    {"mjolnir_04", 1059, 7200000},
    {"moc_fild15", 1159, 7200000},
    {"moc_pryd04", 1038, 3600000},
    {"moc_pryd06", 1511, 3600000},
    {"niflheim", 1373, 7980000},
    {"odin_tem03", 1751, 3600000},
    {"pay_dun04", 1150, 3600000},
    {"pay_fild11", 1115, 7200000},
    {"prt_maze03", 1039, 7200000},
    {"prt_sewb4", 1086, 3600000},
    {"ra_fild02", 1785, 14400000},
    {"ra_fild03", 1785, 10800000},
    {"ra_fild04", 1785, 18000000},
    {"ra_san05", 1768, 18000000},
    {"thor_v03", 1832, 39600000},
    {"treasure02", 1112, 7200000},
    {"tur_dun04", 1312, 3600000},
    {"ve_fild01", 1785, 10800000},
    {"ve_fild02", 1785, 21600000},
    {"xmas_dun02", 1251, 3600000},
    {"xmas_fild01", 1252, 7200000},
};

// The respawn delay of an MVP spawned on that map (ms), or 0 when it is not one.
std::uint32_t embedded_mvp_respawn_ms(const char* map, std::uint16_t mob) {
    for (const MvpSpawn& s : kMvpSpawns)
        if (s.mob == mob && std::strcmp(s.map, map) == 0) return s.delayMs;
    return 0;
}

}  // namespace uaro
