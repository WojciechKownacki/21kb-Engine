#pragma once

#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace kb::world {

// A streaming cell on the horizontal X/Z plane. Coordinates are 64-bit so the
// grid stays exact for worlds whose extent no 32-bit index (or float position)
// can address; cell (x, z) covers [x * size, (x + 1) * size) on X and the same
// on Z.
struct WorldCellCoord {
    std::int64_t x = 0;
    std::int64_t z = 0;

    friend constexpr auto operator<=>(const WorldCellCoord&, const WorldCellCoord&) = default;
};

struct WorldCellCoordHash {
    [[nodiscard]] std::size_t operator()(const WorldCellCoord& coord) const noexcept {
        std::uint64_t value = static_cast<std::uint64_t>(coord.x) * 0x9E3779B97F4A7C15ULL;
        value ^= static_cast<std::uint64_t>(coord.z) + 0x7F4A7C159E3779B9ULL + (value << 6U) + (value >> 2U);
        return static_cast<std::size_t>(value);
    }
};

// A world-space point in double precision. World partition math never narrows
// positions to float, whatever precision the transform storage itself uses.
struct WorldPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Cell coordinates that the text formats (world descriptor and cell index) can
// store exactly as JSON numbers.
inline constexpr std::int64_t kMaxSerializedCellCoordinate = std::int64_t{ 1 } << 53;

inline constexpr double kMinCellSize = 1.0;
inline constexpr double kMaxCellSize = 1'000'000.0;

[[nodiscard]] inline bool IsValidCellSize(double size) noexcept {
    return std::isfinite(size) && size >= kMinCellSize && size <= kMaxCellSize;
}

[[nodiscard]] inline bool IsSerializableCellCoord(const WorldCellCoord& coord) noexcept {
    return coord.x >= -kMaxSerializedCellCoordinate && coord.x <= kMaxSerializedCellCoordinate &&
        coord.z >= -kMaxSerializedCellCoordinate && coord.z <= kMaxSerializedCellCoordinate;
}

class WorldPartitionGrid {
public:
    // `cellSize` must satisfy IsValidCellSize; callers validate authored values first.
    explicit WorldPartitionGrid(double cellSize) noexcept
        : cellSize_(IsValidCellSize(cellSize) ? cellSize : kMinCellSize) {}

    [[nodiscard]] double CellSize() const noexcept { return cellSize_; }

    // The cell containing (x, z), or nothing for a non-finite position or one
    // whose cell index would not be serializable.
    [[nodiscard]] std::optional<WorldCellCoord> CellOf(double x, double z) const noexcept {
        const std::optional<std::int64_t> cellX = Index(x);
        const std::optional<std::int64_t> cellZ = Index(z);
        if (!cellX.has_value() || !cellZ.has_value()) {
            return std::nullopt;
        }
        return WorldCellCoord{ *cellX, *cellZ };
    }

    [[nodiscard]] double MinX(const WorldCellCoord& coord) const noexcept { return static_cast<double>(coord.x) * cellSize_; }
    [[nodiscard]] double MinZ(const WorldCellCoord& coord) const noexcept { return static_cast<double>(coord.z) * cellSize_; }

    // Planar (X/Z) squared distance from a point to the closest point of a cell;
    // zero inside the cell.
    [[nodiscard]] double DistanceSquared(const WorldCellCoord& coord, double x, double z) const noexcept {
        const double minX = MinX(coord);
        const double minZ = MinZ(coord);
        const double dx = x < minX ? minX - x : (x > minX + cellSize_ ? x - (minX + cellSize_) : 0.0);
        const double dz = z < minZ ? minZ - z : (z > minZ + cellSize_ ? z - (minZ + cellSize_) : 0.0);
        return dx * dx + dz * dz;
    }

    // Number of cells in the square that bounds a disc; used to decide whether a
    // grid walk or a scan over the occupied cells is cheaper. Saturates.
    [[nodiscard]] std::uint64_t BoundingCellCount(double radius) const noexcept {
        if (!std::isfinite(radius) || radius < 0.0) {
            return 0U;
        }
        const double side = std::floor(2.0 * radius / cellSize_) + 2.0;
        if (side * side >= 9.0e18) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return static_cast<std::uint64_t>(side * side);
    }

    // Visits, row by row (ascending z, then x), every cell that overlaps the disc
    // centred at (x, z). The order is deterministic for identical input.
    template <typename Visitor>
    void ForEachCellInRadius(double x, double z, double radius, Visitor&& visit) const {
        if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(radius) || radius < 0.0) {
            return;
        }
        const std::optional<std::int64_t> minX = Index(x - radius);
        const std::optional<std::int64_t> maxX = Index(x + radius);
        const std::optional<std::int64_t> minZ = Index(z - radius);
        const std::optional<std::int64_t> maxZ = Index(z + radius);
        if (!minX || !maxX || !minZ || !maxZ) {
            return;
        }
        const double radiusSquared = radius * radius;
        for (std::int64_t cellZ = *minZ; cellZ <= *maxZ; ++cellZ) {
            for (std::int64_t cellX = *minX; cellX <= *maxX; ++cellX) {
                const WorldCellCoord coord{ cellX, cellZ };
                if (DistanceSquared(coord, x, z) <= radiusSquared) {
                    visit(coord);
                }
            }
        }
    }

private:
    [[nodiscard]] std::optional<std::int64_t> Index(double value) const noexcept {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
        const double index = std::floor(value / cellSize_);
        if (index < -static_cast<double>(kMaxSerializedCellCoordinate) ||
            index > static_cast<double>(kMaxSerializedCellCoordinate)) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(index);
    }

    double cellSize_ = kMinCellSize;
};

} // namespace kb::world
