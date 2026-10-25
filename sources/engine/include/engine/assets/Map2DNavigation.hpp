#pragma once
#include "engine/assets/Map2DAsset.hpp"

namespace kb::assets {

struct Map2DPathOptions {
    std::int32_t floor = 0;
    bool platformer = false;
    std::uint32_t jumpHeight = 3;
    std::uint32_t jumpDistance = 4;
    std::size_t maximumVisited = 65536;
};
[[nodiscard]] bool Map2DCellBlocked(const Map2DAsset& map, int x, int y, int floor);
// Cell-center waypoints. Platformer edges describe walking, falling and clear jump arcs.
// A controller follows a jump edge with its own movement policy; top-down paths use four neighbors.
[[nodiscard]] std::vector<std::array<int, 2>> FindMap2DPath(const Map2DAsset& map,
    std::array<int, 2> start, std::array<int, 2> end, const Map2DPathOptions& options = {});

} // namespace kb::assets
