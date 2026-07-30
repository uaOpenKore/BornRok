#include "world/Pathfind.hpp"

#include <functional>
#include <cstdlib>

#include "microtest.hpp"

using namespace uaro;

namespace {
const std::function<bool(int, int)> kOpen = [](int, int) { return true; };
}

TEST_CASE(pathfind_straight) {
    auto p = findPath(10, 10, kOpen, 0, 0, 3, 0);
    CHECK_EQ(p.size(), 3u);  // (1,0),(2,0),(3,0)
    CHECK_EQ(p.back().first, 3);
    CHECK_EQ(p.back().second, 0);
}

TEST_CASE(pathfind_diagonal) {
    auto p = findPath(10, 10, kOpen, 0, 0, 3, 3);
    CHECK_EQ(p.size(), 3u);  // 3 diagonal steps
    CHECK_EQ(p.back().first, 3);
    CHECK_EQ(p.back().second, 3);
}

TEST_CASE(pathfind_diagonal_first_shape) {
    // The server's easy path (path_search_real) steps BOTH axes until one aligns, then straight:
    // (0,0)->(6,2) = 2 diagonals then 4 straight. Our route must match cell-for-cell so the glide
    // stays on the server's cells (no sideways drift -> no jump on a running re-click).
    auto p = findPath(20, 20, kOpen, 0, 0, 6, 2);
    const std::vector<std::pair<u16, u16>> want = {{1, 1}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}};
    CHECK_EQ(p.size(), want.size());
    for (usize i = 0; i < want.size() && i < p.size(); ++i) {
        CHECK_EQ(p[i].first, want[i].first);
        CHECK_EQ(p[i].second, want[i].second);
    }
}

TEST_CASE(pathfind_around_wall) {
    // Vertical wall at x==2 for y in [0,4]; must detour below it.
    std::function<bool(int, int)> w = [](int x, int y) { return !(x == 2 && y <= 4); };
    auto p = findPath(10, 10, w, 0, 0, 4, 0);
    CHECK(!p.empty());
    CHECK_EQ(p.back().first, 4);
    CHECK_EQ(p.back().second, 0);
    for (auto& c : p) CHECK(!(c.first == 2 && c.second <= 4));  // never steps on the wall
}

TEST_CASE(pathfind_around_wall_server_exact_shape) {
    // The A* fallback reproduces the server's path_search_real CELL-FOR-CELL (verified against a
    // verbatim copy of src/map/path.c over 24k random grids, 0 mismatches). Vertical wall at x==2,
    // y in [0,4]; detour below. If our shape drifts from this the glide desyncs and the server snaps
    // the char around (S. sewer stutter, #112).
    std::function<bool(int, int)> w = [](int x, int y) { return !(x == 2 && y <= 4); };
    auto p = findPath(10, 10, w, 0, 0, 4, 0);
    const std::vector<std::pair<u16, u16>> want = {{1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {2, 5},
                                                   {3, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0}};
    CHECK_EQ(p.size(), want.size());
    for (usize i = 0; i < want.size() && i < p.size(); ++i) {
        CHECK_EQ(p[i].first, want[i].first);
        CHECK_EQ(p[i].second, want[i].second);
    }
}

TEST_CASE(pathfind_long_walk_is_hoppable) {
    // Regression guard for the reverted #112 break: the caller (sendNextWalkSegment) plans the FULL
    // route with findPath, then slices a <=28-cell hop. A >32-cell walk therefore MUST return a
    // non-empty path -- a capped/empty result made the caller fall back to the raw far target and
    // glide a straight line through walls. Emulate the hop-slice here on a long detour.
    constexpr usize kHop = 28;
    // A long corridor with a wall that forces a >32-cell route: a 60-wide, 6-tall band, wall column
    // at x==30 for y in [0,3] so the path must dip to y>=4 to pass.
    std::function<bool(int, int)> w = [](int x, int y) { return !(x == 30 && y <= 3); };
    auto path = findPath(64, 8, w, 0, 0, 58, 0);
    CHECK(!path.empty());              // must plan the long route (else caller walks straight = the bug)
    CHECK(path.size() > 32u);          // genuinely longer than the server's own 32-cell limit
    // The sliced hop is a real, reachable, wall-avoiding cell -- never the far target itself.
    const usize i = std::min(path.size(), kHop) - 1;
    CHECK(!(path[i].first == 30 && path[i].second <= 3));  // hop cell is not on the wall
    // Every cell is walkable and each step is adjacent (no teleport through a wall).
    int px = 0, py = 0;
    for (auto& c : path) {
        CHECK(w(c.first, c.second));
        const int adx = std::abs(static_cast<int>(c.first) - px), ady = std::abs(static_cast<int>(c.second) - py);
        CHECK(adx <= 1);
        CHECK(ady <= 1);
        px = c.first; py = c.second;
    }
    CHECK_EQ(path.back().first, 58);  // reaches the destination
    CHECK_EQ(path.back().second, 0);
}

TEST_CASE(pathfind_unreachable) {
    // Fully box in (4,3).
    std::function<bool(int, int)> w = [](int x, int y) {
        if ((x == 3 || x == 5) && y >= 2 && y <= 4) return false;
        if ((y == 2 || y == 4) && x >= 3 && x <= 5) return false;
        return true;
    };
    auto p = findPath(10, 10, w, 0, 0, 4, 3);
    CHECK(p.empty());
}

TEST_CASE(pathfind_dest_blocked) {
    std::function<bool(int, int)> w = [](int x, int y) { return !(x == 5 && y == 5); };
    CHECK(findPath(10, 10, w, 0, 0, 5, 5).empty());
}

TEST_CASE(pathfind_same_cell) {
    CHECK(findPath(10, 10, kOpen, 4, 4, 4, 4).empty());
}
