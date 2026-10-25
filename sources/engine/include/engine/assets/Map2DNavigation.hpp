#pragma once
#include "engine/assets/Map2DAsset.hpp"

namespace kb::assets {

// Whether a top-down path may step diagonally. NoCornerCutting refuses a diagonal step when either of the two
// orthogonal cells it passes between is blocked.
enum class Map2DDiagonalPolicy : std::uint8_t { None, NoCornerCutting, Allowed };

struct Map2DPathOptions {
    std::int32_t floor = 0;
    bool platformer = false;
    std::uint32_t jumpHeight = 3;
    std::uint32_t jumpDistance = 4;
    std::size_t maximumVisited = 65536;
    // Top-down only. A body this many cells wide on each side of the path cell: every cell of the square around a
    // path cell must be free (the start cell is exempt, so a body can leave a tight spot).
    std::uint32_t clearanceCells = 0;
    Map2DDiagonalPolicy diagonal = Map2DDiagonalPolicy::None;
    // Top-down only. Name of a number property of a cell (its own or its graphic default) that multiplies the cost of
    // stepping onto it; values below 1 count as 1. Empty: every cell costs the same.
    std::string costProperty;
};

enum class Map2DPathStatus : std::uint8_t {
    Found,
    NoPath,          // the search ended: no way exists
    StartBlocked,
    EndBlocked,
    BudgetExceeded,  // the search stopped at maximumVisited before it knew
};

struct Map2DPathResult {
    std::vector<std::array<int, 2>> path;
    Map2DPathStatus status = Map2DPathStatus::NoPath;
    std::size_t visited = 0U;
};
[[nodiscard]] Map2DPathResult FindMap2DPathResult(const Map2DAsset& map,
    std::array<int, 2> start, std::array<int, 2> end, const Map2DPathOptions& options = {});
[[nodiscard]] bool Map2DCellBlocked(const Map2DAsset& map, int x, int y, int floor);
// Cell-center waypoints. Platformer edges describe walking, falling and clear jump arcs.
// A controller follows a jump edge with its own movement policy; top-down paths use four neighbors.
[[nodiscard]] std::vector<std::array<int, 2>> FindMap2DPath(const Map2DAsset& map,
    std::array<int, 2> start, std::array<int, 2> end, const Map2DPathOptions& options = {});

} // namespace kb::assets
