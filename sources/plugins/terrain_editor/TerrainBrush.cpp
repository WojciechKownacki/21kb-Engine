#include "TerrainBrush.hpp"

#include "TerrainAnalysis.hpp"
#include "TerrainParallel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>

namespace kb::terrain_editor {
namespace {

constexpr float kDegreesToRadians = 0.0174532925199433F;
// Below this many samples a stamp runs on the calling thread; thread start costs more than it saves.
constexpr std::uint32_t kParallelSamples = 24'576U;

[[nodiscard]] std::size_t VertexIndex(const kb::assets::TerrainAsset& terrain, std::uint32_t x, std::uint32_t z) noexcept {
    return static_cast<std::size_t>(z) * terrain.width + x;
}

[[nodiscard]] std::size_t CellIndex(const kb::assets::TerrainAsset& terrain, std::uint32_t x, std::uint32_t z) noexcept {
    return static_cast<std::size_t>(z) * (terrain.width - 1U) + x;
}

[[nodiscard]] float SmoothStep(float value) noexcept {
    value = std::clamp(value, 0.0F, 1.0F);
    return value * value * (3.0F - 2.0F * value);
}

[[nodiscard]] float HashNoise(std::uint32_t x, std::uint32_t z, std::uint32_t seed) noexcept {
    std::uint32_t value = x * 0x9E3779B9U ^ z * 0x85EBCA6BU ^ seed * 0xC2B2AE35U;
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return (static_cast<float>(value & 0x00FFFFFFU) / 8388607.5F) - 1.0F;
}

[[nodiscard]] float Weight(
    float distance,
    std::uint32_t sampleX,
    std::uint32_t sampleZ,
    const TerrainBrushSettings& settings) noexcept {
    const float normalized = std::clamp(distance / settings.radius, 0.0F, 1.0F);
    const auto softRound = [&settings, normalized] {
        if (normalized <= settings.falloff) return 1.0F;
        const float edgeWidth = std::max(1.0F - settings.falloff, 0.0001F);
        return 1.0F - SmoothStep((normalized - settings.falloff) / edgeWidth);
    };
    switch (settings.shape) {
    case TerrainBrushShape::SoftRound:
        return softRound();
    case TerrainBrushShape::HardRound:
        return normalized < 0.96F ? 1.0F : 1.0F - SmoothStep((normalized - 0.96F) / 0.04F);
    case TerrainBrushShape::LinearRound:
        return 1.0F - normalized;
    case TerrainBrushShape::Bell: {
        constexpr float kPi = 3.14159265358979323846F;
        return 0.5F + 0.5F * std::cos(normalized * kPi);
    }
    case TerrainBrushShape::Ring: {
        const float ringDistance = std::abs(normalized - 0.58F) / 0.24F;
        return 1.0F - SmoothStep(ringDistance);
    }
    case TerrainBrushShape::Speckle:
        return HashNoise(sampleX, sampleZ, settings.noiseSeed ^ 0xA511E9B3U) > -0.18F
            ? softRound()
            : 0.0F;
    }
    return softRound();
}

void MarkChanged(TerrainBrushResult& result, std::uint32_t x, std::uint32_t z) noexcept {
    ++result.changedSamples;
    result.minX = std::min(result.minX, x);
    result.minZ = std::min(result.minZ, z);
    result.maxX = std::max(result.maxX, x);
    result.maxZ = std::max(result.maxZ, z);
}

void Merge(TerrainBrushResult& into, const TerrainBrushResult& other) noexcept {
    into.changedSamples += other.changedSamples;
    into.minX = std::min(into.minX, other.minX);
    into.minZ = std::min(into.minZ, other.minZ);
    into.maxX = std::max(into.maxX, other.maxX);
    into.maxZ = std::max(into.maxZ, other.maxZ);
}

[[nodiscard]] float SmoothedHeight(
    std::span<const float> sourceHeights,
    std::uint32_t sourceMinX,
    std::uint32_t sourceMinZ,
    std::uint32_t sourceWidth,
    std::uint32_t x,
    std::uint32_t z) noexcept {
    float sum = 0.0F;
    std::uint32_t count = 0U;
    const std::uint32_t minX = x == 0U ? 0U : x - 1U;
    const std::uint32_t minZ = z == 0U ? 0U : z - 1U;
    const std::uint32_t sourceMaxX = sourceMinX + sourceWidth - 1U;
    const std::uint32_t sourceHeight = static_cast<std::uint32_t>(sourceHeights.size() / sourceWidth);
    const std::uint32_t sourceMaxZ = sourceMinZ + sourceHeight - 1U;
    const std::uint32_t maxX = std::min(x + 1U, sourceMaxX);
    const std::uint32_t maxZ = std::min(z + 1U, sourceMaxZ);
    for (std::uint32_t sampleZ = minZ; sampleZ <= maxZ; ++sampleZ) {
        for (std::uint32_t sampleX = minX; sampleX <= maxX; ++sampleX) {
            sum += sourceHeights[
                static_cast<std::size_t>(sampleZ - sourceMinZ) * sourceWidth +
                (sampleX - sourceMinX)];
            ++count;
        }
    }
    return sum / static_cast<float>(count);
}

// Flat steps `step` apart; `softness` is how much of each step is riser (0 a cliff, 1 all slope).
[[nodiscard]] float TerraceHeight(float height, float step, float softness) noexcept {
    const float scaled = height / step;
    const float base = std::floor(scaled);
    const float fraction = scaled - base;
    const float half = 0.5F * softness;
    const float t = half > 0.0F ? std::clamp((fraction - (0.5F - half)) / (2.0F * half), 0.0F, 1.0F) : (fraction >= 0.5F ? 1.0F : 0.0F);
    return (base + SmoothStep(t)) * step;
}

[[nodiscard]] float SampleStamp(const TerrainStampImage& image, float u, float v) noexcept {
    const float x = std::clamp(u, 0.0F, 1.0F) * static_cast<float>(image.width - 1U);
    const float y = std::clamp(v, 0.0F, 1.0F) * static_cast<float>(image.height - 1U);
    const std::uint32_t x0 = std::min(static_cast<std::uint32_t>(x), image.width - 1U);
    const std::uint32_t y0 = std::min(static_cast<std::uint32_t>(y), image.height - 1U);
    const std::uint32_t x1 = std::min(x0 + 1U, image.width - 1U);
    const std::uint32_t y1 = std::min(y0 + 1U, image.height - 1U);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto at = [&image](std::uint32_t sx, std::uint32_t sy) { return image.samples[static_cast<std::size_t>(sy) * image.width + sx]; };
    return std::lerp(std::lerp(at(x0, y0), at(x1, y0), tx), std::lerp(at(x0, y1), at(x1, y1), tx), ty);
}

// One mirrored copy of a stamp: its centre (and a ramp's end) and the signs that map its offsets
// back to the stamp as the user made it.
struct Variant {
    float x = 0.0F;
    float z = 0.0F;
    float endX = 0.0F;
    float endZ = 0.0F;
    float signX = 1.0F;
    float signZ = 1.0F;
};

[[nodiscard]] std::size_t MakeVariants(
    const TerrainBrushSettings& settings, const TerrainBrushStamp& stamp, std::array<Variant, 4>& variants) noexcept {
    std::size_t count = 0U;
    for (const float signX : { 1.0F, -1.0F }) {
        if (signX < 0.0F && !settings.symmetryX) continue;
        for (const float signZ : { 1.0F, -1.0F }) {
            if (signZ < 0.0F && !settings.symmetryZ) continue;
            const Variant candidate{ signX * stamp.localX, signZ * stamp.localZ,
                signX * settings.rampEndX, signZ * settings.rampEndZ, signX, signZ };
            const bool duplicate = std::any_of(variants.begin(), variants.begin() + static_cast<std::ptrdiff_t>(count), [&](const Variant& other) {
                return std::abs(other.x - candidate.x) < 1.0e-4F && std::abs(other.z - candidate.z) < 1.0e-4F &&
                    std::abs(other.endX - candidate.endX) < 1.0e-4F && std::abs(other.endZ - candidate.endZ) < 1.0e-4F;
            });
            if (!duplicate) variants[count++] = candidate;
        }
    }
    return count;
}

struct Region {
    int minX = 0;
    int minZ = 0;
    int maxX = -1;
    int maxZ = -1;
};

struct Grid {
    float cellSizeX = 1.0F;
    float cellSizeZ = 1.0F;
    float halfX = 0.0F;
    float halfZ = 0.0F;
};

// The samples within `extent` world units of the box of the points (x0, z0) - (x1, z1).
[[nodiscard]] Region RegionOf(
    const kb::assets::TerrainAsset& terrain, const Grid& grid, float x0, float z0, float x1, float z1, float extent) noexcept {
    const float lowX = (std::min(x0, x1) - extent + grid.halfX) / grid.cellSizeX;
    const float lowZ = (std::min(z0, z1) - extent + grid.halfZ) / grid.cellSizeZ;
    const float highX = (std::max(x0, x1) + extent + grid.halfX) / grid.cellSizeX;
    const float highZ = (std::max(z0, z1) + extent + grid.halfZ) / grid.cellSizeZ;
    const float lastX = static_cast<float>(terrain.width - 1U);
    const float lastZ = static_cast<float>(terrain.height - 1U);
    if (highX < 0.0F || highZ < 0.0F || lowX > lastX || lowZ > lastZ) return {};
    return { static_cast<int>(std::floor(std::max(lowX, 0.0F))), static_cast<int>(std::floor(std::max(lowZ, 0.0F))),
        static_cast<int>(std::ceil(std::min(highX, lastX))), static_cast<int>(std::ceil(std::min(highZ, lastZ))) };
}

TerrainBrushResult ApplyHoles(
    kb::assets::TerrainAsset& terrain, const TerrainBrushSettings& settings, TerrainBrushMode mode, const Variant& at, const Grid& grid) noexcept {
    TerrainBrushResult result{};
    const Region region = RegionOf(terrain, grid, at.x, at.z, at.x, at.z, settings.radius);
    const std::uint8_t target = mode == TerrainBrushMode::CutHole ? 1U : 0U;
    const float radiusSquared = settings.radius * settings.radius;
    for (int z = region.minZ; z < region.maxZ; ++z) {
        for (int x = region.minX; x < region.maxX; ++x) {
            const float dx = (static_cast<float>(x) + 0.5F) * grid.cellSizeX - grid.halfX - at.x;
            const float dz = (static_cast<float>(z) + 0.5F) * grid.cellSizeZ - grid.halfZ - at.z;
            const float distanceSquared = dx * dx + dz * dz;
            const auto foldedX = static_cast<std::uint32_t>(settings.symmetryX ? std::min(x, static_cast<int>(terrain.width) - 2 - x) : x);
            const auto foldedZ = static_cast<std::uint32_t>(settings.symmetryZ ? std::min(z, static_cast<int>(terrain.height) - 2 - z) : z);
            if (distanceSquared > radiusSquared || Weight(std::sqrt(distanceSquared), foldedX, foldedZ, settings) <= 0.25F) continue;
            std::uint8_t& hole = terrain.holes[CellIndex(terrain, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z))];
            if (hole != target) {
                hole = target;
                MarkChanged(result, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z));
            }
        }
    }
    return result;
}

// What a stamp does to one height sample, by mode.
struct StampMath {
    const TerrainBrushSettings& settings;
    TerrainBrushMode mode;
    float pressure;
    const Variant& at;
    float noiseSign;
    float rampLengthSquared;
    float rotationCos;
    float rotationSin;
    TerrainNoiseSettings noise;
    std::span<const float> smoothSource;
    std::uint32_t smoothMinX;
    std::uint32_t smoothMinZ;
    std::uint32_t smoothWidth;

    [[nodiscard]] float Evaluate(const kb::assets::TerrainAsset& terrain, int x, int z, float worldX, float worldZ, float before) const noexcept {
        const float dx = worldX - at.x;
        const float dz = worldZ - at.z;
        const auto foldedX = static_cast<std::uint32_t>(settings.symmetryX ? std::min(x, static_cast<int>(terrain.width) - 1 - x) : x);
        const auto foldedZ = static_cast<std::uint32_t>(settings.symmetryZ ? std::min(z, static_cast<int>(terrain.height) - 1 - z) : z);
        if (mode == TerrainBrushMode::Ramp) {
            const float abx = at.endX - at.x;
            const float abz = at.endZ - at.z;
            const float t = rampLengthSquared > 0.0F ? std::clamp((dx * abx + dz * abz) / rampLengthSquared, 0.0F, 1.0F) : 0.0F;
            const float offsetX = dx - abx * t;
            const float offsetZ = dz - abz * t;
            const float distance = std::sqrt(offsetX * offsetX + offsetZ * offsetZ);
            if (distance > settings.radius) return before;
            const float normalized = distance / settings.radius;
            const float side = normalized <= settings.falloff ? 1.0F
                : 1.0F - SmoothStep((normalized - settings.falloff) / std::max(1.0F - settings.falloff, 0.0001F));
            const float target = std::lerp(settings.targetHeight, settings.rampEndHeight, t);
            return std::lerp(before, target, std::clamp(settings.strength * side * pressure, 0.0F, 1.0F));
        }
        if (mode == TerrainBrushMode::Stamp) {
            // The offset in the stamp as the user placed it, turned back by the rotation.
            const float ox = dx * at.signX;
            const float oz = dz * at.signZ;
            const float u = (ox * rotationCos + oz * rotationSin) / (2.0F * settings.radius) + 0.5F;
            const float v = (oz * rotationCos - ox * rotationSin) / (2.0F * settings.radius) + 0.5F;
            if (u < 0.0F || u > 1.0F || v < 0.0F || v > 1.0F) return before;
            const float edge = std::max(std::abs(u - 0.5F), std::abs(v - 0.5F)) * 2.0F * settings.radius;
            const float influence = Weight(edge, foldedX, foldedZ, settings) * pressure;
            const float image = SampleStamp(*settings.stampImage, u, v) * settings.strength;
            switch (settings.stampBlend) {
            case TerrainStampBlend::Add: return before + noiseSign * image * influence;
            case TerrainStampBlend::Max: {
                const float level = settings.targetHeight + image;
                return std::lerp(before, noiseSign > 0.0F ? std::max(before, level) : std::min(before, level), std::clamp(influence, 0.0F, 1.0F));
            }
            case TerrainStampBlend::Min: {
                const float level = settings.targetHeight + image;
                return std::lerp(before, noiseSign > 0.0F ? std::min(before, level) : std::max(before, level), std::clamp(influence, 0.0F, 1.0F));
            }
            }
            return before;
        }
        const float distanceSquared = dx * dx + dz * dz;
        if (distanceSquared > settings.radius * settings.radius) return before;
        const float influence = Weight(std::sqrt(distanceSquared), foldedX, foldedZ, settings) * pressure;
        switch (mode) {
        case TerrainBrushMode::Raise: return before + settings.strength * influence;
        case TerrainBrushMode::Lower: return before - settings.strength * influence;
        case TerrainBrushMode::Smooth:
            return std::lerp(before, SmoothedHeight(smoothSource, smoothMinX, smoothMinZ, smoothWidth,
                static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z)), std::clamp(settings.strength * influence, 0.0F, 1.0F));
        case TerrainBrushMode::Flatten:
            return std::lerp(before, settings.targetHeight, std::clamp(settings.strength * influence, 0.0F, 1.0F));
        case TerrainBrushMode::Noise: {
            // The noise is read at the mirrored-in position so that mirrored strokes are mirror images.
            const float noiseX = settings.symmetryX ? std::abs(worldX) : worldX;
            const float noiseZ = settings.symmetryZ ? std::abs(worldZ) : worldZ;
            return before + noiseSign * SampleTerrainNoise(noise, noiseX, noiseZ) * settings.strength * influence;
        }
        case TerrainBrushMode::Terrace:
            return std::lerp(before, TerraceHeight(before, settings.terraceStep, settings.terraceSoftness),
                std::clamp(settings.strength * influence, 0.0F, 1.0F));
        default: return before;
        }
    }
};

TerrainBrushResult ApplyHeights(
    kb::assets::TerrainAsset& terrain,
    const TerrainBrushSettings& settings,
    TerrainBrushMode mode,
    float pressure,
    const Variant& at,
    const Grid& grid,
    std::vector<float>& scratchHeights) noexcept {
    const bool ramp = mode == TerrainBrushMode::Ramp;
    const bool stamp = mode == TerrainBrushMode::Stamp;
    // A stamp turned by any angle stays within radius x sqrt(2) of its centre.
    const float extent = stamp ? settings.radius * 1.41421356F : settings.radius;
    const Region region = RegionOf(terrain, grid, at.x, at.z, ramp ? at.endX : at.x, ramp ? at.endZ : at.z, extent);
    if (region.maxX < region.minX || region.maxZ < region.minZ) return {};

    StampMath math{ .settings = settings, .mode = mode, .pressure = pressure, .at = at, .noiseSign = settings.invert ? -1.0F : 1.0F,
        .rampLengthSquared = (at.endX - at.x) * (at.endX - at.x) + (at.endZ - at.z) * (at.endZ - at.z),
        .rotationCos = std::cos(settings.stampRotationDegrees * kDegreesToRadians),
        .rotationSin = std::sin(settings.stampRotationDegrees * kDegreesToRadians),
        .noise = TerrainNoiseSettings{ .kind = settings.noiseKind, .scale = settings.noiseScale, .octaves = settings.noiseOctaves,
            .seed = settings.noiseSeed } };
    if (mode == TerrainBrushMode::Smooth) {
        math.smoothMinX = region.minX == 0 ? 0U : static_cast<std::uint32_t>(region.minX - 1);
        math.smoothMinZ = region.minZ == 0 ? 0U : static_cast<std::uint32_t>(region.minZ - 1);
        const std::uint32_t sourceMaxX = std::min(static_cast<std::uint32_t>(region.maxX + 1), terrain.width - 1U);
        const std::uint32_t sourceMaxZ = std::min(static_cast<std::uint32_t>(region.maxZ + 1), terrain.height - 1U);
        math.smoothWidth = sourceMaxX - math.smoothMinX + 1U;
        const std::uint32_t sourceHeight = sourceMaxZ - math.smoothMinZ + 1U;
        scratchHeights.resize(static_cast<std::size_t>(math.smoothWidth) * sourceHeight);
        for (std::uint32_t sourceZ = math.smoothMinZ; sourceZ <= sourceMaxZ; ++sourceZ) {
            std::copy_n(terrain.heights.data() + VertexIndex(terrain, math.smoothMinX, sourceZ), math.smoothWidth,
                scratchHeights.data() + static_cast<std::size_t>(sourceZ - math.smoothMinZ) * math.smoothWidth);
        }
        math.smoothSource = scratchHeights;
    }
    // Rows are independent (a smooth reads its own copy), so a big stamp is shared out by rows.
    const auto rowWidth = static_cast<std::uint32_t>(region.maxX - region.minX + 1);
    std::array<TerrainBrushResult, kTerrainMaximumWorkers> partial{};
    ParallelForRange(static_cast<std::uint32_t>(region.minZ), static_cast<std::uint32_t>(region.maxZ) + 1U,
        std::max(1U, kParallelSamples / rowWidth), [&](std::uint32_t firstRow, std::uint32_t endRow, std::uint32_t part) noexcept {
            TerrainBrushResult& local = partial[part];
            for (std::uint32_t z = firstRow; z < endRow; ++z) {
                const float worldZ = static_cast<float>(z) * grid.cellSizeZ - grid.halfZ;
                for (int x = region.minX; x <= region.maxX; ++x) {
                    const float worldX = static_cast<float>(x) * grid.cellSizeX - grid.halfX;
                    const std::size_t index = VertexIndex(terrain, static_cast<std::uint32_t>(x), z);
                    const float before = terrain.heights[index];
                    const float after = std::clamp(
                        math.Evaluate(terrain, x, static_cast<int>(z), worldX, worldZ, before), -1'000'000.0F, 1'000'000.0F);
                    if (std::abs(after - before) > std::numeric_limits<float>::epsilon()) {
                        terrain.heights[index] = after;
                        MarkChanged(local, static_cast<std::uint32_t>(x), z);
                    }
                }
            }
        });
    TerrainBrushResult result{};
    for (const TerrainBrushResult& local : partial) Merge(result, local);
    return result;
}

} // namespace

namespace {

struct ModeName { std::string_view name; TerrainBrushMode mode; };
constexpr ModeName kModeNames[]{
    { "raise", TerrainBrushMode::Raise }, { "lower", TerrainBrushMode::Lower }, { "smooth", TerrainBrushMode::Smooth },
    { "flatten", TerrainBrushMode::Flatten }, { "noise", TerrainBrushMode::Noise }, { "terrace", TerrainBrushMode::Terrace },
    { "cut-hole", TerrainBrushMode::CutHole }, { "fill-hole", TerrainBrushMode::FillHole },
    { "ramp", TerrainBrushMode::Ramp }, { "stamp", TerrainBrushMode::Stamp },
};
struct ShapeName { std::string_view name; TerrainBrushShape shape; };
constexpr ShapeName kShapeNames[]{
    { "soft-round", TerrainBrushShape::SoftRound }, { "hard-round", TerrainBrushShape::HardRound },
    { "linear-round", TerrainBrushShape::LinearRound }, { "bell", TerrainBrushShape::Bell },
    { "ring", TerrainBrushShape::Ring }, { "speckle", TerrainBrushShape::Speckle },
};
struct BlendName { std::string_view name; TerrainStampBlend blend; };
constexpr BlendName kBlendNames[]{ { "add", TerrainStampBlend::Add }, { "max", TerrainStampBlend::Max }, { "min", TerrainStampBlend::Min } };

} // namespace

std::string_view TerrainBrushModeName(TerrainBrushMode mode) noexcept {
    for (const ModeName& entry : kModeNames) if (entry.mode == mode) return entry.name;
    return {};
}

std::optional<TerrainBrushMode> ParseTerrainBrushMode(std::string_view text) noexcept {
    for (const ModeName& entry : kModeNames) if (entry.name == text) return entry.mode;
    return std::nullopt;
}

std::string_view TerrainBrushShapeName(TerrainBrushShape shape) noexcept {
    for (const ShapeName& entry : kShapeNames) if (entry.shape == shape) return entry.name;
    return {};
}

std::optional<TerrainBrushShape> ParseTerrainBrushShape(std::string_view text) noexcept {
    for (const ShapeName& entry : kShapeNames) if (entry.name == text) return entry.shape;
    return std::nullopt;
}

std::string_view TerrainStampBlendName(TerrainStampBlend blend) noexcept {
    for (const BlendName& entry : kBlendNames) if (entry.blend == blend) return entry.name;
    return {};
}

std::optional<TerrainStampBlend> ParseTerrainStampBlend(std::string_view text) noexcept {
    for (const BlendName& entry : kBlendNames) if (entry.name == text) return entry.blend;
    return std::nullopt;
}

bool IsTerrainBrushSettingsValid(const TerrainBrushSettings& settings) noexcept {
    const bool stampOk = settings.mode != TerrainBrushMode::Stamp ||
        (settings.stampImage != nullptr && settings.stampImage->width >= 1U && settings.stampImage->height >= 1U &&
            settings.stampImage->samples.size() == static_cast<std::size_t>(settings.stampImage->width) * settings.stampImage->height &&
            static_cast<std::uint8_t>(settings.stampBlend) <= static_cast<std::uint8_t>(TerrainStampBlend::Min) &&
            std::isfinite(settings.stampRotationDegrees));
    return static_cast<std::uint8_t>(settings.mode) <= static_cast<std::uint8_t>(TerrainBrushMode::Stamp) &&
        static_cast<std::uint8_t>(settings.shape) <= static_cast<std::uint8_t>(TerrainBrushShape::Speckle) &&
        std::isfinite(settings.radius) && settings.radius > 0.0F && settings.radius <= 100'000.0F &&
        std::isfinite(settings.strength) && settings.strength >= 0.0F && settings.strength <= 100'000.0F &&
        std::isfinite(settings.falloff) && settings.falloff >= 0.0F && settings.falloff <= 1.0F &&
        std::isfinite(settings.targetHeight) && std::abs(settings.targetHeight) <= 1'000'000.0F &&
        std::isfinite(settings.terraceStep) && settings.terraceStep > 0.0F && settings.terraceStep <= 100'000.0F &&
        std::isfinite(settings.terraceSoftness) && settings.terraceSoftness >= 0.0F && settings.terraceSoftness <= 1.0F &&
        IsTerrainNoiseValid(TerrainNoiseSettings{ .kind = settings.noiseKind, .scale = settings.noiseScale, .octaves = settings.noiseOctaves }) &&
        std::isfinite(settings.rampEndX) && std::isfinite(settings.rampEndZ) &&
        std::isfinite(settings.rampEndHeight) && std::abs(settings.rampEndHeight) <= 1'000'000.0F && stampOk;
}

void BeginTerrainStroke(
    const kb::assets::TerrainAsset& terrain, TerrainBrushSettings& settings, const TerrainBrushStamp& first) noexcept {
    if (!settings.sampleTargetHeight) return;
    if (terrain.width < 2U || terrain.height < 2U || terrain.heights.size() != static_cast<std::size_t>(terrain.width) * terrain.height) return;
    settings.sampleTargetHeight = false;
    if (settings.mode == TerrainBrushMode::Flatten || settings.mode == TerrainBrushMode::Stamp) {
        settings.targetHeight = SampleTerrainHeight(terrain, first.localX, first.localZ);
    } else if (settings.mode == TerrainBrushMode::Ramp) {
        settings.targetHeight = SampleTerrainHeight(terrain, first.localX, first.localZ);
        settings.rampEndHeight = SampleTerrainHeight(terrain, settings.rampEndX, settings.rampEndZ);
    }
}

float TerrainBrushWeight(
    float distance,
    std::uint32_t sampleX,
    std::uint32_t sampleZ,
    const TerrainBrushSettings& settings) noexcept {
    return Weight(distance, sampleX, sampleZ, settings);
}

namespace {

TerrainBrushResult ApplyTerrainBrushImpl(
    kb::assets::TerrainAsset& terrain,
    const TerrainBrushSettings& settings,
    const TerrainBrushStamp& stamp,
    bool validateSamples,
    std::vector<float>& scratchHeights) noexcept {
    TerrainBrushResult result{};
    const std::uint64_t vertexCount = static_cast<std::uint64_t>(terrain.width) * terrain.height;
    const std::uint64_t cellCount = terrain.width > 0U && terrain.height > 0U
        ? static_cast<std::uint64_t>(terrain.width - 1U) * (terrain.height - 1U)
        : 0U;
    const bool validLayout = kb::assets::IsTerrainResolutionValid(terrain.width) &&
        kb::assets::IsTerrainResolutionValid(terrain.height) &&
        std::isfinite(terrain.worldSizeX) && std::isfinite(terrain.worldSizeZ) &&
        terrain.worldSizeX > 0.0F && terrain.worldSizeZ > 0.0F &&
        terrain.heights.size() == vertexCount && terrain.holes.size() == cellCount;
    if (!validLayout || (validateSamples && !kb::assets::IsTerrainAssetValid(terrain)) ||
        !IsTerrainBrushSettingsValid(settings) ||
        !std::isfinite(stamp.localX) || !std::isfinite(stamp.localZ) || !std::isfinite(stamp.pressure)) return result;

    const float pressure = std::clamp(stamp.pressure, 0.0F, 1.0F);
    const Grid grid{ terrain.worldSizeX / static_cast<float>(terrain.width - 1U), terrain.worldSizeZ / static_cast<float>(terrain.height - 1U),
        terrain.worldSizeX * 0.5F, terrain.worldSizeZ * 0.5F };
    TerrainBrushMode mode = settings.mode;
    if (settings.invert) {
        if (mode == TerrainBrushMode::Raise) mode = TerrainBrushMode::Lower;
        else if (mode == TerrainBrushMode::Lower) mode = TerrainBrushMode::Raise;
        else if (mode == TerrainBrushMode::CutHole) mode = TerrainBrushMode::FillHole;
        else if (mode == TerrainBrushMode::FillHole) mode = TerrainBrushMode::CutHole;
    }
    std::array<Variant, 4> variants{};
    const std::size_t variantCount = MakeVariants(settings, stamp, variants);
    for (std::size_t index = 0U; index < variantCount; ++index) {
        Merge(result, mode == TerrainBrushMode::CutHole || mode == TerrainBrushMode::FillHole
            ? ApplyHoles(terrain, settings, mode, variants[index], grid)
            : ApplyHeights(terrain, settings, mode, pressure, variants[index], grid, scratchHeights));
    }
    return result;
}

} // namespace

TerrainBrushResult ApplyTerrainBrush(
    kb::assets::TerrainAsset& terrain,
    const TerrainBrushSettings& settings,
    const TerrainBrushStamp& stamp) noexcept {
    std::vector<float> scratchHeights;
    return ApplyTerrainBrushImpl(terrain, settings, stamp, true, scratchHeights);
}

TerrainBrushResult ApplyTerrainBrushToValidatedTerrain(
    kb::assets::TerrainAsset& terrain,
    const TerrainBrushSettings& settings,
    const TerrainBrushStamp& stamp) noexcept {
    thread_local std::vector<float> scratchHeights;
    return ApplyTerrainBrushImpl(terrain, settings, stamp, false, scratchHeights);
}

TerrainBrushResult ApplyTerrainBrushToValidatedTerrain(
    kb::assets::TerrainAsset& terrain,
    const TerrainBrushSettings& settings,
    const TerrainBrushStamp& stamp,
    std::vector<float>& scratchHeights) noexcept {
    return ApplyTerrainBrushImpl(terrain, settings, stamp, false, scratchHeights);
}

} // namespace kb::terrain_editor
