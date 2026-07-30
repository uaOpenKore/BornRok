#include "world/Pathfind.hpp"

#include <algorithm>

#include "formats/Gat.hpp"

namespace uaro {

// Mirror the server's path_search_real "easy path" (Meruru, src/map/path.c): step toward the
// target on BOTH axes at once (diagonal-first) until aligned on one axis, then straight. The
// server tries THIS before any A* and uses it whenever the diagonal-first line to the target is
// unobstructed — which is the common open-terrain case. Only the stepped cell is checked (the
// server does NOT corner-check here, so this cuts corners exactly as the server does). Returns the
// cell list (excluding start, including dest) on success, else empty so the caller routes with A*.
// Matching this is what keeps OUR glide on the SAME cells the server walks: if we route an A* path
// of equal length but a different shape (staircase vs diagonal-first), our visible position drifts
// sideways from the server's over a long run, and the next walk reply snaps us back -> the char
// "jumps" on a running re-click (S.). Identical paths -> drift stays within a cell -> no snap.
static std::vector<std::pair<u16, u16>> serverEasyPath(
        const std::function<bool(int, int)>& walkable, int sx, int sy, int dx, int dy) {
    std::vector<std::pair<u16, u16>> path;
    int stepx = (dx > sx) ? 1 : (dx < sx ? -1 : 0);
    int stepy = (dy > sy) ? 1 : (dy < sy ? -1 : 0);
    int x = sx, y = sy;
    for (int i = 0; i < 32; ++i) {  // server caps the easy path at MAX_WALKPATH (32)
        x += stepx;
        y += stepy;
        if (x == dx) stepx = 0;
        if (y == dy) stepy = 0;
        if (!walkable(x, y)) return {};  // blocked before the goal -> A* fallback (as the server does)
        path.push_back({static_cast<u16>(x), static_cast<u16>(y)});
        if (stepx == 0 && stepy == 0) break;  // reached the target
    }
    if (path.empty() || path.back().first != dx || path.back().second != dy) return {};
    return path;
}

// ---- The server's A* (path_search_real, src/map/path.c) on a FULL-MAP scratch --------------------
// Around obstacles the server routes with a specific A*, and its exact path SHAPE (not just length)
// decides which cells the char walks: cost seed = Manhattan(start,target)*10, then +20 per step that
// moves AWAY from the target on an axis and -6 per diagonal (dist = +10 straight / +14 diagonal
// tracked separately for re-open); neighbour order (x,y+1),(x-1,y),(x,y-1),(x+1,y) then the four
// diagonals, each gated on BOTH its orthogonals (never cuts a wall corner). Reproducing it keeps our
// glide on the server's cells, so the reconcile doesn't snap the char around obstacle-dense maps (#112).
//
// The server's own scratch is a 32x32 wrapped torus (MAX_WALKPATH) and it FAILS any path longer than
// that. The CLIENT must NOT copy that cap: sendNextWalkSegment plans the FULL route here and then
// slices a <=28-cell hop from it, so a capped (empty) result made the caller send the raw far target
// and glide a straight line through walls (the reverted regression). So we use a full-map scratch:
// for any path the server can solve (<=32) we match it cell-for-cell (verified vs a standalone copy of
// path_search_real over 24k random grids, 0 mismatches); longer routes still return so the hop-slicer
// works, and the per-hop request the server then re-paths is itself <=28 and stays server-exact.
namespace {
struct TmpPath { int x, y, dist, before, cost, flag; unsigned gen; };

void pushHeap(int* heap, TmpPath* tp, int index) {
    int h, i;
    heap[0]++;
    for (h = heap[0] - 1, i = (h - 1) / 2; h > 0 && tp[index].cost < tp[heap[i + 1]].cost; i = (h - 1) / 2)
        heap[h + 1] = heap[i + 1], h = i;
    heap[h + 1] = index;
}
void updateHeap(int* heap, TmpPath* tp, int index) {
    int h, i;
    for (h = 0; h < heap[0]; h++)
        if (heap[h + 1] == index) break;
    for (i = (h - 1) / 2; h > 0 && tp[index].cost < tp[heap[i + 1]].cost; i = (h - 1) / 2)
        heap[h + 1] = heap[i + 1], h = i;
    heap[h + 1] = index;
}
int popHeap(int* heap, TmpPath* tp) {
    int ret = heap[1], last = heap[heap[0]], h, k, i;
    heap[0]--;
    for (h = 0, k = 2; k < heap[0]; k = k * 2 + 2) {
        if (tp[heap[k + 1]].cost > tp[heap[k]].cost) k--;
        heap[h + 1] = heap[k + 1], h = k;
    }
    if (k == heap[0]) heap[h + 1] = heap[k], h = k - 1;
    for (i = (h - 1) / 2; h > 0 && tp[heap[i + 1]].cost > tp[last].cost; i = (h - 1) / 2)
        heap[h + 1] = heap[i + 1], h = i;
    heap[h + 1] = last;
    return ret;
}
int calcCost(const TmpPath* p, int x1, int y1) {
    int xd = x1 - p->x; if (xd < 0) xd = -xd;
    int yd = y1 - p->y; if (yd < 0) yd = -yd;
    return (xd + yd) * 10 + p->dist;
}
// index = x + y*W (full map, no 32-wrap) -> every cell has a unique slot, so add_path's collision
// branch never fires. gen-stamp keeps a slot fresh across calls without clearing the whole scratch.
int addPath(int* heap, TmpPath* tp, unsigned gen, int W, int x, int y, int dist, int before, int cost) {
    int i = x + y * W;
    if (tp[i].gen != gen) {
        tp[i].gen = gen;
        tp[i].x = tp[i].y = tp[i].dist = tp[i].before = tp[i].cost = tp[i].flag = 0;
    }
    if (tp[i].x == x && tp[i].y == y) {
        if (tp[i].dist > dist) {
            tp[i].dist = dist; tp[i].before = before; tp[i].cost = cost;
            if (tp[i].flag) pushHeap(heap, tp, i); else updateHeap(heap, tp, i);
            tp[i].flag = 0;
        }
        return 0;
    }
    if (tp[i].x || tp[i].y) return 1;  // can't happen with unique full-map slots; kept for parity
    tp[i].x = x; tp[i].y = y; tp[i].dist = dist; tp[i].before = before; tp[i].cost = cost; tp[i].flag = 0;
    pushHeap(heap, tp, i);
    return 0;
}

std::vector<std::pair<u16, u16>> serverAStar(const std::function<bool(int, int)>& walkable, int W,
                                             int H, int x0, int y0, int x1, int y1) {
    std::vector<std::pair<u16, u16>> out;
    const int N = W * H;
    // Single-threaded main loop -> reuse a generation-stamped scratch (grown to the biggest map seen).
    static thread_local std::vector<TmpPath> scratch;
    static thread_local std::vector<int> heapv;
    static thread_local unsigned gen = 0;
    if (static_cast<int>(scratch.size()) < N) scratch.assign(N, TmpPath{});
    if (static_cast<int>(heapv.size()) < N + 1) heapv.assign(N + 1, 0);
    TmpPath* tp = scratch.data();
    int* heap = heapv.data();
    if (++gen == 0) { std::fill(scratch.begin(), scratch.end(), TmpPath{}); gen = 1; }

    const int si = x0 + y0 * W;
    tp[si].x = x0; tp[si].y = y0; tp[si].dist = 0; tp[si].before = 0;
    tp[si].cost = calcCost(&tp[si], x1, y1); tp[si].flag = 0; tp[si].gen = gen;
    heap[0] = 0;
    pushHeap(heap, tp, si);
    const int xs = W - 1, ys = H - 1;
    int rp = si, x = x0, y = y0;
    while (true) {
        int e = 0, f = 0, dist, cost, dc[4] = {0, 0, 0, 0};
        if (heap[0] == 0) return out;  // exhausted -> unreachable
        rp = popHeap(heap, tp);
        x = tp[rp].x; y = tp[rp].y;
        dist = tp[rp].dist + 10;
        cost = tp[rp].cost;
        if (x == x1 && y == y1) break;
        if (y < ys && walkable(x, y + 1)) { f |= 1; dc[0] = (y >= y1 ? 20 : 0); e += addPath(heap, tp, gen, W, x, y + 1, dist, rp, cost + dc[0]); }
        if (x > 0  && walkable(x - 1, y)) { f |= 2; dc[1] = (x <= x1 ? 20 : 0); e += addPath(heap, tp, gen, W, x - 1, y, dist, rp, cost + dc[1]); }
        if (y > 0  && walkable(x, y - 1)) { f |= 4; dc[2] = (y <= y1 ? 20 : 0); e += addPath(heap, tp, gen, W, x, y - 1, dist, rp, cost + dc[2]); }
        if (x < xs && walkable(x + 1, y)) { f |= 8; dc[3] = (x >= x1 ? 20 : 0); e += addPath(heap, tp, gen, W, x + 1, y, dist, rp, cost + dc[3]); }
        if ((f & (2 + 1)) == (2 + 1) && walkable(x - 1, y + 1)) e += addPath(heap, tp, gen, W, x - 1, y + 1, dist + 4, rp, cost + dc[1] + dc[0] - 6);
        if ((f & (2 + 4)) == (2 + 4) && walkable(x - 1, y - 1)) e += addPath(heap, tp, gen, W, x - 1, y - 1, dist + 4, rp, cost + dc[1] + dc[2] - 6);
        if ((f & (8 + 4)) == (8 + 4) && walkable(x + 1, y - 1)) e += addPath(heap, tp, gen, W, x + 1, y - 1, dist + 4, rp, cost + dc[3] + dc[2] - 6);
        if ((f & (8 + 1)) == (8 + 1) && walkable(x + 1, y + 1)) e += addPath(heap, tp, gen, W, x + 1, y + 1, dist + 4, rp, cost + dc[3] + dc[0] - 6);
        tp[rp].flag = 1;
        if (e) return out;  // (unreachable in practice; unique slots never collide)
    }
    // Reconstruct target -> start via .before (cells excluding start, including dest), guarded against a
    // cycle by the cell count.
    int cur = rp, guard = 0;
    while (cur != si && guard++ < N) {
        out.push_back({static_cast<u16>(tp[cur].x), static_cast<u16>(tp[cur].y)});
        cur = tp[cur].before;
    }
    if (guard >= N) return {};
    std::reverse(out.begin(), out.end());
    return out;
}
}  // namespace

std::vector<std::pair<u16, u16>> findPath(int W, int H, const std::function<bool(int, int)>& walkable,
                                          u16 sx, u16 sy, u16 dx, u16 dy, int maxNodes) {
    std::vector<std::pair<u16, u16>> path;
    if (W <= 0 || H <= 0) return path;
    if (sx == dx && sy == dy) return path;
    if (!walkable(dx, dy) || !walkable(sx, sy)) return path;

    // Try the server's diagonal-first easy path first (and only fall through to A* when it is
    // blocked), exactly as path_search_real does, so our route matches the server's cell-for-cell.
    path = serverEasyPath(walkable, sx, sy, dx, dy);
    if (!path.empty()) return path;

    // Obstacle case: reproduce the server's A* (see serverAStar) so the route is the SAME cells the
    // server walks. maxNodes is unused now -- serverAStar searches the whole (bounded) map.
    (void)maxNodes;
    return serverAStar(walkable, W, H, sx, sy, dx, dy);
}

std::vector<std::pair<u16, u16>> findPath(const Gat& gat, u16 sx, u16 sy, u16 dx, u16 dy,
                                          int maxNodes) {
    const int W = static_cast<int>(gat.width()), H = static_cast<int>(gat.height());
    // The server treats the last row/column as impassable (map_getcellp bounds).
    auto walkable = [&gat, W, H](int x, int y) {
        return x >= 0 && y >= 0 && x < W - 1 && y < H - 1 && gat.at(x, y).walkable();
    };
    return findPath(W, H, walkable, sx, sy, dx, dy, maxNodes);
}

} // namespace uaro
