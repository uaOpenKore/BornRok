#pragma once
// The in-game Reference data (S. 2026-11-23): generated from the server by tools/gen_reference_db.py.
#include <cstddef>
#include <cstdint>

namespace uaro::ref {

struct Drop { std::uint32_t item; std::uint16_t rate; std::uint8_t kind; };  // rate per 10000; kind 0 drop, 1 card, 2 MVP
struct Spawn { const char* map; std::uint16_t count; std::uint32_t delayMs; };
struct Mob {
    std::uint16_t id;
    const char* name;
    std::uint32_t lv;
    std::uint32_t hp, exp, jexp;
    std::uint32_t atk1, atk2, def, mdef;
    std::uint8_t size, race, ele;  // ele: element + 20 * level (mob_db)
    std::uint8_t mvp;
    std::uint32_t drop0;  std::uint8_t dropN;   // kDrops[drop0 .. drop0 + dropN)
    std::uint32_t spawn0; std::uint16_t spawnN; // kSpawns[spawn0 .. spawn0 + spawnN)
};
struct Item {
    std::uint32_t id;
    const char* name;
    std::uint8_t type;
    std::uint32_t buy, sell;
    std::uint32_t weight, atk, def;
    std::uint8_t slots;
    std::uint8_t elv;
};

const Mob* mobs(std::size_t& n);
const Item* items(std::size_t& n);
const Drop* drops();
const Spawn* spawns();

}  // namespace uaro::ref
