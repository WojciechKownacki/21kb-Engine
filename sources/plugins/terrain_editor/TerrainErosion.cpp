#include "TerrainErosion.hpp"

#include "TerrainParallel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace kb::terrain_editor {
namespace {

constexpr float kDegreesToRadians = 0.0174532925199433F;

// Hydraulic constants, in units of the terrain's height range and of one cell.
constexpr float kFlowRate = 0.22F;        // flux per unit of water-surface difference
constexpr float kFlowDamping = 0.96F;     // how much of last iteration's flux carries on
constexpr float kMaximumSpeed = 1.2F;     // cells per iteration
constexpr float kCapacity = 0.018F;       // sediment the water can carry per unit of slope and speed, x height range
constexpr float kDissolving = 0.35F;      // the share of the missing capacity picked up
constexpr float kDeposition = 0.30F;      // the share of the excess capacity laid down
constexpr float kMinimumSlope = 0.04F;    // sine of the tilt below which a cell still counts as this tilted

void ParallelRows(const kb::assets::TerrainAsset& terrain, const TerrainTaskControl& control, auto&& rows) {
    ParallelForRange(0U, terrain.height, std::max(1U, 8192U / terrain.width), rows, control.maxWorkers);
}

} // namespace

bool IsTerrainThermalValid(const TerrainThermalSettings& settings) noexcept {
    return settings.iterations >= 1U && settings.iterations <= kTerrainMaximumErosionIterations &&
        std::isfinite(settings.talusDegrees) && settings.talusDegrees > 0.0F && settings.talusDegrees < 90.0F &&
        std::isfinite(settings.strength) && settings.strength > 0.0F && settings.strength <= 1.0F;
}

bool IsTerrainHydraulicValid(const TerrainHydraulicSettings& settings) noexcept {
    return settings.iterations >= 1U && settings.iterations <= kTerrainMaximumErosionIterations &&
        std::isfinite(settings.rain) && settings.rain > 0.0F && settings.rain <= 0.1F &&
        std::isfinite(settings.evaporation) && settings.evaporation > 0.0F && settings.evaporation < 1.0F &&
        std::isfinite(settings.strength) && settings.strength > 0.0F && settings.strength <= 100.0F;
}

bool ErodeTerrainThermal(
    kb::assets::TerrainAsset& terrain, const TerrainThermalSettings& settings, TerrainTaskControl& control, std::string& error) {
    if (!kb::assets::IsTerrainAssetValid(terrain, &error)) return false;
    if (!IsTerrainThermalValid(settings)) {
        error = "thermal erosion settings are out of range (iterations 1..100000, talus 0..90 degrees, strength 0..1)";
        return false;
    }
    const std::uint32_t width = terrain.width;
    const std::uint32_t height = terrain.height;
    const float cellX = terrain.worldSizeX / static_cast<float>(width - 1U);
    const float cellZ = terrain.worldSizeZ / static_cast<float>(height - 1U);
    constexpr std::array<int, 8> kDx{ -1, 1, 0, 0, -1, 1, -1, 1 };
    constexpr std::array<int, 8> kDz{ 0, 0, -1, 1, -1, -1, 1, 1 };
    constexpr std::array<int, 8> kOpposite{ 1, 0, 3, 2, 7, 6, 5, 4 };
    const float tangent = std::tan(settings.talusDegrees * kDegreesToRadians);
    std::array<float, 8> talus{};
    for (std::size_t direction = 0U; direction < 8U; ++direction) {
        talus[direction] = tangent * std::hypot(static_cast<float>(kDx[direction]) * cellX, static_cast<float>(kDz[direction]) * cellZ);
    }
    // How much a cell gives to each of its eight neighbours: the part of every drop steeper than
    // the talus, sharing out half of the steepest excess (so two cells never swap more than they differ).
    const auto outflow = [&](const float* heights, int x, int z, std::array<float, 8>& move) {
        const float centre = heights[static_cast<std::size_t>(z) * width + static_cast<std::size_t>(x)];
        float sum = 0.0F;
        float largest = 0.0F;
        for (std::size_t direction = 0U; direction < 8U; ++direction) {
            const int nx = x + kDx[direction];
            const int nz = z + kDz[direction];
            move[direction] = 0.0F;
            if (nx < 0 || nz < 0 || nx >= static_cast<int>(width) || nz >= static_cast<int>(height)) continue;
            const float excess = centre - heights[static_cast<std::size_t>(nz) * width + static_cast<std::size_t>(nx)] - talus[direction];
            if (excess <= 0.0F) continue;
            move[direction] = excess;
            sum += excess;
            largest = std::max(largest, excess);
        }
        if (sum <= 0.0F) return false;
        const float total = settings.strength * largest * 0.5F;
        for (float& amount : move) amount = total * amount / sum;
        return true;
    };
    std::vector<float> current = terrain.heights;
    std::vector<float> next(current.size());
    control.Begin(settings.iterations);
    for (std::uint32_t iteration = 0U; iteration < settings.iterations; ++iteration) {
        if (control.Cancelled()) break;
        ParallelRows(terrain, control, [&](std::uint32_t firstRow, std::uint32_t endRow, std::uint32_t) {
            for (std::uint32_t z = firstRow; z < endRow; ++z) {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    float change = 0.0F;
                    std::array<float, 8> move{};
                    if (outflow(current.data(), static_cast<int>(x), static_cast<int>(z), move)) {
                        for (const float amount : move) change -= amount;
                    }
                    for (std::size_t direction = 0U; direction < 8U; ++direction) {
                        const int nx = static_cast<int>(x) + kDx[direction];
                        const int nz = static_cast<int>(z) + kDz[direction];
                        if (nx < 0 || nz < 0 || nx >= static_cast<int>(width) || nz >= static_cast<int>(height)) continue;
                        std::array<float, 8> neighbour{};
                        if (outflow(current.data(), nx, nz, neighbour)) change += neighbour[static_cast<std::size_t>(kOpposite[direction])];
                    }
                    next[static_cast<std::size_t>(z) * width + x] = current[static_cast<std::size_t>(z) * width + x] + change;
                }
            }
        });
        std::swap(current, next);
        control.Advance(1U);
    }
    if (control.Cancelled()) {
        error = "cancelled";
        return false;
    }
    terrain.heights = std::move(current);
    return true;
}

bool ErodeTerrainHydraulic(
    kb::assets::TerrainAsset& terrain, const TerrainHydraulicSettings& settings, TerrainTaskControl& control, std::string& error) {
    if (!kb::assets::IsTerrainAssetValid(terrain, &error)) return false;
    if (!IsTerrainHydraulicValid(settings)) {
        error = "hydraulic erosion settings are out of range (iterations 1..100000, rain 0..0.1, evaporation 0..1, strength 0..100)";
        return false;
    }
    const std::uint32_t width = terrain.width;
    const std::uint32_t height = terrain.height;
    const std::size_t count = terrain.heights.size();
    const auto [lowest, highest] = std::minmax_element(terrain.heights.begin(), terrain.heights.end());
    const float range = std::max(*highest - *lowest, 0.001F);
    const float cellX = terrain.worldSizeX / static_cast<float>(width - 1U);
    const float cellZ = terrain.worldSizeZ / static_cast<float>(height - 1U);
    const float rainPerIteration = settings.rain * range;
    const float capacityScale = kCapacity * range * settings.strength;
    const float depthReference = 3.0F * rainPerIteration;

    std::vector<float> ground = terrain.heights;
    std::vector<float> groundNext(count);
    std::vector<float> water(count, rainPerIteration);
    std::vector<float> waterNext(count, 0.0F);
    std::vector<float> sediment(count, 0.0F);
    std::vector<float> sedimentNext(count, 0.0F);
    std::vector<float> speedX(count, 0.0F);
    std::vector<float> speedZ(count, 0.0F);
    // Flux out of every cell towards -x, +x, -z, +z, in units of water depth per iteration.
    std::array<std::vector<float>, 4> flux;
    for (std::vector<float>& direction : flux) direction.assign(count, 0.0F);
    constexpr std::array<int, 4> kDx{ -1, 1, 0, 0 };
    constexpr std::array<int, 4> kDz{ 0, 0, -1, 1 };

    const auto index = [width](int x, int z) { return static_cast<std::size_t>(z) * width + static_cast<std::size_t>(x); };
    const auto inside = [width, height](int x, int z) { return x >= 0 && z >= 0 && x < static_cast<int>(width) && z < static_cast<int>(height); };

    control.Begin(settings.iterations);
    for (std::uint32_t iteration = 0U; iteration < settings.iterations; ++iteration) {
        if (control.Cancelled()) break;
        // The flow between neighbouring cells, from the difference of their water surfaces.
        ParallelRows(terrain, control, [&](std::uint32_t firstRow, std::uint32_t endRow, std::uint32_t) {
            for (std::uint32_t z = firstRow; z < endRow; ++z) {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    const std::size_t i = index(static_cast<int>(x), static_cast<int>(z));
                    const float surface = ground[i] + water[i];
                    float total = 0.0F;
                    std::array<float, 4> out{};
                    for (std::size_t direction = 0U; direction < 4U; ++direction) {
                        const int nx = static_cast<int>(x) + kDx[direction];
                        const int nz = static_cast<int>(z) + kDz[direction];
                        if (!inside(nx, nz)) continue;
                        const std::size_t n = index(nx, nz);
                        out[direction] = std::max(0.0F, flux[direction][i] * kFlowDamping + kFlowRate * (surface - (ground[n] + water[n])));
                        total += out[direction];
                    }
                    // Never more water leaves than the cell holds.
                    const float scale = total > water[i] ? water[i] / total : 1.0F;
                    for (std::size_t direction = 0U; direction < 4U; ++direction) flux[direction][i] = out[direction] * scale;
                }
            }
        });
        if (control.Cancelled()) break;
        // New water depth and the flow speed through every cell.
        ParallelRows(terrain, control, [&](std::uint32_t firstRow, std::uint32_t endRow, std::uint32_t) {
            for (std::uint32_t z = firstRow; z < endRow; ++z) {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    const int ix = static_cast<int>(x);
                    const int iz = static_cast<int>(z);
                    const std::size_t i = index(ix, iz);
                    const float fromLeft = inside(ix - 1, iz) ? flux[1][index(ix - 1, iz)] : 0.0F;
                    const float fromRight = inside(ix + 1, iz) ? flux[0][index(ix + 1, iz)] : 0.0F;
                    const float fromUp = inside(ix, iz - 1) ? flux[3][index(ix, iz - 1)] : 0.0F;
                    const float fromDown = inside(ix, iz + 1) ? flux[2][index(ix, iz + 1)] : 0.0F;
                    const float leaving = flux[0][i] + flux[1][i] + flux[2][i] + flux[3][i];
                    const float depth = std::max(0.0F, water[i] + fromLeft + fromRight + fromUp + fromDown - leaving);
                    const float mean = std::max(0.5F * (water[i] + depth), 1.0e-6F);
                    // Net flow along each axis (the average of what passes in and out), per depth.
                    const float alongX = 0.5F * (fromLeft - flux[0][i] + flux[1][i] - fromRight);
                    const float alongZ = 0.5F * (fromUp - flux[2][i] + flux[3][i] - fromDown);
                    speedX[i] = std::clamp(alongX / mean, -kMaximumSpeed, kMaximumSpeed);
                    speedZ[i] = std::clamp(alongZ / mean, -kMaximumSpeed, kMaximumSpeed);
                    // The edge of the terrain drains: nothing stays there.
                    const bool edge = x == 0U || z == 0U || x + 1U == width || z + 1U == height;
                    waterNext[i] = edge ? 0.0F : depth;
                }
            }
        });
        std::swap(water, waterNext);
        if (control.Cancelled()) break;
        // Cut where the water is fast and the ground steep, lay down where it is slow.
        ParallelRows(terrain, control, [&](std::uint32_t firstRow, std::uint32_t endRow, std::uint32_t) {
            for (std::uint32_t z = firstRow; z < endRow; ++z) {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    const int ix = static_cast<int>(x);
                    const int iz = static_cast<int>(z);
                    const std::size_t i = index(ix, iz);
                    const float left = ground[index(std::max(ix - 1, 0), iz)];
                    const float right = ground[index(std::min(ix + 1, static_cast<int>(width) - 1), iz)];
                    const float up = ground[index(ix, std::max(iz - 1, 0))];
                    const float down = ground[index(ix, std::min(iz + 1, static_cast<int>(height) - 1))];
                    const float gradientX = (right - left) / (2.0F * cellX);
                    const float gradientZ = (down - up) / (2.0F * cellZ);
                    const float slope = std::sqrt(gradientX * gradientX + gradientZ * gradientZ);
                    const float sine = std::max(slope / std::sqrt(1.0F + slope * slope), kMinimumSlope);
                    const float speed = std::sqrt(speedX[i] * speedX[i] + speedZ[i] * speedZ[i]);
                    const float depthFactor = std::min(1.0F, water[i] / depthReference);
                    const float capacity = capacityScale * sine * speed * depthFactor;
                    float carried = sediment[i];
                    float change = 0.0F;
                    if (carried < capacity) {
                        change = -kDissolving * (capacity - carried);
                    } else {
                        change = kDeposition * (carried - capacity);
                    }
                    groundNext[i] = ground[i] + change;
                    sedimentNext[i] = carried + change * -1.0F;
                }
            }
        });
        std::swap(ground, groundNext);
        std::swap(sediment, sedimentNext);
        if (control.Cancelled()) break;
        // The flow carries its sediment along; some water evaporates and the next rain falls.
        ParallelRows(terrain, control, [&](std::uint32_t firstRow, std::uint32_t endRow, std::uint32_t) {
            for (std::uint32_t z = firstRow; z < endRow; ++z) {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    const std::size_t i = index(static_cast<int>(x), static_cast<int>(z));
                    const float sourceX = std::clamp(static_cast<float>(x) - speedX[i], 0.0F, static_cast<float>(width - 1U));
                    const float sourceZ = std::clamp(static_cast<float>(z) - speedZ[i], 0.0F, static_cast<float>(height - 1U));
                    const auto x0 = std::min(static_cast<std::uint32_t>(sourceX), width - 2U);
                    const auto z0 = std::min(static_cast<std::uint32_t>(sourceZ), height - 2U);
                    const float tx = sourceX - static_cast<float>(x0);
                    const float tz = sourceZ - static_cast<float>(z0);
                    const auto at = [&](std::uint32_t sx, std::uint32_t sz) { return sediment[static_cast<std::size_t>(sz) * width + sx]; };
                    const bool edge = x == 0U || z == 0U || x + 1U == width || z + 1U == height;
                    sedimentNext[i] = edge ? 0.0F : std::lerp(std::lerp(at(x0, z0), at(x0 + 1U, z0), tx), std::lerp(at(x0, z0 + 1U), at(x0 + 1U, z0 + 1U), tx), tz);
                    water[i] = water[i] * (1.0F - settings.evaporation) + rainPerIteration;
                }
            }
        });
        std::swap(sediment, sedimentNext);
        control.Advance(1U);
    }
    if (control.Cancelled()) {
        error = "cancelled";
        return false;
    }
    for (std::size_t i = 0U; i < count; ++i) terrain.heights[i] = std::clamp(ground[i], -1'000'000.0F, 1'000'000.0F);
    return true;
}

} // namespace kb::terrain_editor
