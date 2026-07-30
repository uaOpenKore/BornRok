#pragma once
#include <functional>
#include <utility>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

class Gat;

// A* over an 8-connected cell grid (diagonals cost 14, orthogonals 10, octile
// heuristic), with no corner-cutting through blocked diagonals — matching how RO
// walks. Returns the cell path EXCLUDING the start and INCLUDING the destination,
// or an empty vector if the destination is blocked/unreachable or the search hits
// `maxNodes`. `walkable(x, y)` must report whether a cell can be stepped on.
std::vector<std::pair<u16, u16>> findPath(int width, int height,
                                          const std::function<bool(int, int)>& walkable, u16 sx,
                                          u16 sy, u16 dx, u16 dy, int maxNodes = 20000);

// Convenience overload over a parsed GAT (uses GatCell::walkable() and the
// server's border rule: the last row/column is impassable).
std::vector<std::pair<u16, u16>> findPath(const Gat& gat, u16 sx, u16 sy, u16 dx, u16 dy,
                                          int maxNodes = 20000);

} // namespace uaro
