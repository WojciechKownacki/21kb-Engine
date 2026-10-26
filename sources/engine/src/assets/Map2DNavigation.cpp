#include "engine/assets/Map2DNavigation.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <map>
#include <queue>
#include <utility>

namespace kb::assets {
bool Map2DCellBlocked(const Map2DAsset& map, int x, int y, int floor) {
    if (x < 0 || y < 0 || x >= static_cast<int>(map.width) || y >= static_cast<int>(map.height) ||
        floor < Map2DAsset::MinimumFloor || floor > Map2DAsset::MaximumFloor) return true;
    const auto found = map.tiles.find(Map2DTileKey(static_cast<unsigned>(x), static_cast<unsigned>(y), floor));
    if (found == map.tiles.end()) return false;
    const auto& tile = found->second;
    const auto collision = Map2DCellCollision(map, tile);
    if (collision.kind != Map2DCollisionKind::None && !collision.trigger) return true;
    const auto blocked = [&](Map2DSprite sprite) { const auto* definition = Map2DDefinition(map, sprite); return definition && definition->blocksNavigation; };
    if (blocked(tile.ground)) return true;
    return std::ranges::any_of(tile.items, blocked);
}

std::vector<std::array<int, 2>> FindMap2DPath(const Map2DAsset& map, std::array<int, 2> start,
    std::array<int, 2> end, const Map2DPathOptions& options) {
    return FindMap2DPathResult(map, start, end, options).path;
}

Map2DPathResult FindMap2DPathResult(const Map2DAsset& map, std::array<int, 2> start,
    std::array<int, 2> end, const Map2DPathOptions& options) {
    using Cell = std::array<int, 2>;
    Map2DPathResult result;
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    };
    const auto finish = [&](Map2DPathStatus status) {
        result.status = status;
        result.elapsedMilliseconds = elapsed();
        return std::move(result);
    };
    if (!std::isfinite(options.maximumMilliseconds) || options.maximumMilliseconds < 0.0)
        return finish(Map2DPathStatus::InvalidOptions);
    const auto blocked = [&](Cell cell) { return Map2DCellBlocked(map, cell[0], cell[1], options.floor); };
    const int clearance = static_cast<int>(std::min(options.clearanceCells, 8U));
    // A body wider than one cell needs its whole footprint free (top-down only).
    const auto blockedForBody = [&](Cell cell) {
        if (blocked(cell)) return true;
        if (options.platformer || clearance == 0) return false;
        for (int dx = -clearance; dx <= clearance; ++dx) for (int dy = -clearance; dy <= clearance; ++dy) {
            if (blocked({cell[0] + dx, cell[1] + dy})) return true;
        }
        return false;
    };
    if (blocked(start)) return finish(Map2DPathStatus::StartBlocked);
    if (blockedForBody(end)) return finish(Map2DPathStatus::EndBlocked);
    if (options.maximumVisited == 0) return finish(Map2DPathStatus::BudgetExceeded);
    const bool diagonals = !options.platformer && options.diagonal != Map2DDiagonalPolicy::None;
    // Costs are in tenths of a step so that a diagonal step costs 14 against 10.
    const auto distance = [diagonals](Cell a, Cell b) {
        const int dx = std::abs(a[0] - b[0]), dy = std::abs(a[1] - b[1]);
        return diagonals ? 10 * std::max(dx, dy) + 4 * std::min(dx, dy) : 10 * (dx + dy);
    };
    const auto stepFactor = [&](Cell cell) {
        if (options.costProperty.empty() || options.platformer || cell[0] < 0 || cell[1] < 0) return 1.0;
        const auto tile = map.tiles.find(Map2DTileKey(static_cast<unsigned>(cell[0]), static_cast<unsigned>(cell[1]), options.floor));
        if (tile == map.tiles.end()) return 1.0;
        const std::optional<Map2DValue> value = Map2DCellProperty(map, tile->second, options.costProperty);
        if (!value.has_value()) return 1.0;
        double number = 1.0;
        if (const auto* integer = std::get_if<std::int64_t>(&*value)) number = static_cast<double>(*integer);
        else if (const auto* real = std::get_if<double>(&*value)) number = *real;
        return std::isfinite(number) ? std::clamp(number, 1.0, 1000.0) : 1.0;
    };
    bool budgetHit = false;
    // A long route through expensive cells can exceed a signed 32-bit cost.
    struct Entry { std::int64_t score; std::int64_t cost; Cell cell; bool operator>(const Entry& other) const { return std::tie(score, cost, cell) > std::tie(other.score, other.cost, other.cell); } };
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    std::map<Cell, std::pair<std::int64_t, Cell>> visited;
    visited[start] = {0, start};
    open.push({distance(start, end), 0, start});
    const int jumpHeight = static_cast<int>(std::min(options.jumpHeight, 16U));
    const int jumpDistance = static_cast<int>(std::min(options.jumpDistance, 16U));
    while (!open.empty()) {
        if (options.maximumMilliseconds > 0.0 && elapsed() >= options.maximumMilliseconds) {
            result.visited = visited.size();
            result.timeBudgetExceeded = true;
            return finish(Map2DPathStatus::BudgetExceeded);
        }
        const auto current = open.top(); open.pop();
        if (visited[current.cell].first != current.cost) continue;
        if (current.cell == end) {
            std::vector<Cell> path{end};
            while (path.back() != start) path.push_back(visited[path.back()].second);
            std::ranges::reverse(path);
            result.path = std::move(path);
            result.visited = visited.size();
            result.cost = current.cost;
            return finish(Map2DPathStatus::Found);
        }
        const auto add = [&](Cell next) {
            if (blockedForBody(next)) return;
            const int step = static_cast<int>(std::lround(static_cast<double>(distance(current.cell, next)) * stepFactor(next)));
            const auto cost = current.cost + step;
            const auto previous = visited.find(next);
            if (previous != visited.end() && previous->second.first <= cost) return;
            if (previous == visited.end() && visited.size() >= std::min<std::size_t>(options.maximumVisited, 1048576U)) { budgetHit = true; return; }
            visited[next] = {cost, current.cell};
            open.push({cost + distance(next, end), cost, next});
        };
        const int x = current.cell[0], y = current.cell[1];
        if (!options.platformer) {
            add({x - 1, y}); add({x + 1, y}); add({x, y - 1}); add({x, y + 1});
            if (diagonals) {
                for (const int dx : { -1, 1 }) for (const int dy : { -1, 1 }) {
                    if (options.diagonal == Map2DDiagonalPolicy::NoCornerCutting && (blocked({x + dx, y}) || blocked({x, y + dy}))) continue;
                    add({x + dx, y + dy});
                }
            }
            continue;
        }
        const bool grounded = blocked({x, y + 1});
        if (!grounded) { add({x, y + 1}); if (!blocked({x - 1, y})) add({x - 1, y + 1}); if (!blocked({x + 1, y})) add({x + 1, y + 1}); continue; }
        add({x - 1, y}); add({x + 1, y});
        if (jumpHeight == 0) continue;
        for (int dx = -jumpDistance; dx <= jumpDistance; ++dx) for (int dy = -jumpHeight; dy <= jumpHeight; ++dy) {
            const Cell target{x + dx, y + dy};
            if ((dx == 0 && dy == 0) || blocked(target) || !blocked({target[0], target[1] + 1})) continue;
            bool clear = true;
            const int steps = 8 * (std::abs(dx) + std::abs(dy) + jumpHeight);
            for (int step = 1; step < steps; ++step) {
                const float t = static_cast<float>(step) / static_cast<float>(steps);
                const float px = static_cast<float>(x) + 0.5F + static_cast<float>(dx) * t;
                const float py = static_cast<float>(y) + 0.5F + static_cast<float>(dy) * t - 4.0F * static_cast<float>(jumpHeight) * t * (1.0F - t);
                const Cell sample{static_cast<int>(std::floor(px)), static_cast<int>(std::floor(py))};
                if (blocked(sample)) {
                    const auto tile = sample[0] < 0 || sample[1] < 0 ? map.tiles.end() : map.tiles.find(Map2DTileKey(static_cast<unsigned>(sample[0]), static_cast<unsigned>(sample[1]), options.floor));
                    const float vertical = static_cast<float>(dy) - 4.0F * static_cast<float>(jumpHeight) * (1.0F - 2.0F * t);
                    if (vertical < 0 && tile != map.tiles.end() && Map2DCellCollision(map, tile->second).kind == Map2DCollisionKind::OneWay) continue;
                    clear = false; break;
                }
            }
            if (clear) add(target);
        }
    }
    result.visited = visited.size();
    return finish(budgetHit ? Map2DPathStatus::BudgetExceeded : Map2DPathStatus::NoPath);
}
} // namespace kb::assets
