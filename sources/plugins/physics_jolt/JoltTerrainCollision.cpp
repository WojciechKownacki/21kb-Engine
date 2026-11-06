#include "JoltTerrainCollision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kb::physics_jolt {
namespace {

constexpr float kNoCollision = JPH::HeightFieldShapeConstants::cNoCollisionValue;
// Samples are kept at 16 bits: Jolt stores them relative to the range of their block, so a steep block still
// resolves finer than a centimetre over the shape's height range.
constexpr std::uint32_t kBitsPerSample = 16U;
constexpr std::uint32_t kLargestBlockSize = 4U;

[[nodiscard]] std::uint32_t CeilDiv(std::uint32_t value, std::uint32_t divisor) noexcept {
    return (value + divisor - 1U) / divisor;
}

[[nodiscard]] std::uint32_t NextPowerOfTwo(std::uint32_t value) noexcept {
    std::uint32_t result = 1U;
    while (result < value) result <<= 1U;
    return result;
}

// Jolt builds its acceleration grid over a power of two of blocks per side and wants at least two blocks, so
// the block size that leaves the smallest such grid (the smaller block on a tie) spares memory and query work.
[[nodiscard]] std::uint32_t ChooseBlockSize(std::uint32_t extent) noexcept {
    std::uint32_t best = 2U;
    std::uint32_t bestGrid = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t block = 2U; block <= kLargestBlockSize; ++block) {
        if (CeilDiv(extent, block) < 2U) continue;
        const std::uint32_t grid = NextPowerOfTwo(CeilDiv(extent, block));
        if (grid < bestGrid) {
            best = block;
            bestGrid = grid;
        }
    }
    return best;
}

// A field sample is a hole only where every cell around it is. Jolt drops the triangles that touch a missing
// sample, so this cuts a hole of two cells or more across, but for one triangle in each of two corners, and
// leaves a one-cell hole closed instead of widening every hole by a cell.
[[nodiscard]] bool IsHole(const kb::assets::TerrainAsset& terrain, std::uint32_t step, std::uint32_t sampleX, std::uint32_t sampleZ) noexcept {
    const std::uint32_t cellsX = terrain.width - 1U;
    const std::uint32_t cellsZ = terrain.height - 1U;
    const std::uint32_t firstX = sampleX * step >= step ? sampleX * step - step : 0U;
    const std::uint32_t firstZ = sampleZ * step >= step ? sampleZ * step - step : 0U;
    const std::uint32_t lastX = std::min(sampleX * step + step - 1U, cellsX - 1U);
    const std::uint32_t lastZ = std::min(sampleZ * step + step - 1U, cellsZ - 1U);
    for (std::uint32_t z = firstZ; z <= lastZ; ++z) {
        for (std::uint32_t x = firstX; x <= lastX; ++x) {
            if (terrain.holes[std::size_t{ z } * cellsX + x] == 0U) return false;
        }
    }
    return true;
}

} // namespace

TerrainFieldRect MergeTerrainFieldRects(const TerrainFieldRect& first, const TerrainFieldRect& second) noexcept {
    return { std::min(first.minX, second.minX), std::min(first.minZ, second.minZ),
        std::max(first.maxX, second.maxX), std::max(first.maxZ, second.maxZ) };
}

std::optional<TerrainCollisionField> TerrainCollisionField::Build(const kb::assets::TerrainAsset& terrain, std::string& error) {
    error.clear();
    if (!kb::assets::IsTerrainAssetValid(terrain, &error)) return std::nullopt;
    TerrainCollisionField field;
    field.step_ = terrain.collisionStep;
    field.columns_ = (terrain.width - 1U) / field.step_ + 1U;
    field.rows_ = (terrain.height - 1U) / field.step_ + 1U;
    const std::uint32_t extent = std::max(field.columns_, field.rows_);
    field.blockSize_ = ChooseBlockSize(extent);
    field.sampleCount_ = CeilDiv(extent, field.blockSize_) * field.blockSize_;
    field.spacingX_ = terrain.worldSizeX / static_cast<float>(terrain.width - 1U) * static_cast<float>(field.step_);
    field.spacingZ_ = terrain.worldSizeZ / static_cast<float>(terrain.height - 1U) * static_cast<float>(field.step_);
    field.originX_ = -terrain.worldSizeX * 0.5F;
    field.originZ_ = -terrain.worldSizeZ * 0.5F;
    field.samples_.assign(std::size_t{ field.sampleCount_ } * field.sampleCount_, kNoCollision);
    field.Resample(terrain, { 0U, 0U, field.columns_ - 1U, field.rows_ - 1U });
    return field;
}

bool TerrainCollisionField::Matches(const kb::assets::TerrainAsset& terrain) const noexcept {
    return terrain.collisionStep == step_ && (terrain.width - 1U) / step_ + 1U == columns_ &&
        (terrain.height - 1U) / step_ + 1U == rows_ &&
        terrain.worldSizeX / static_cast<float>(terrain.width - 1U) * static_cast<float>(step_) == spacingX_ &&
        terrain.worldSizeZ / static_cast<float>(terrain.height - 1U) * static_cast<float>(step_) == spacingZ_;
}

std::optional<TerrainFieldRect> TerrainCollisionField::Update(
    const kb::assets::TerrainAsset& terrain, const kb::assets::TerrainSampleRegion& region) {
    if (!Matches(terrain) || terrain.heights.size() != std::size_t{ terrain.width } * terrain.height ||
        region.minX > region.maxX || region.minZ > region.maxZ || region.maxX >= terrain.width || region.maxZ >= terrain.height) {
        return std::nullopt;
    }
    // A field sample reads the terrain sample under it and the cells within one step of it.
    const auto first = [this](std::uint32_t low) { return low + 1U > step_ ? CeilDiv(low + 1U - step_, step_) : 0U; };
    const auto last = [this](std::uint32_t high, std::uint32_t count) { return std::min((high + step_ - 1U) / step_, count - 1U); };
    const TerrainFieldRect rect{ first(region.minX), first(region.minZ), last(region.maxX, columns_), last(region.maxZ, rows_) };
    Resample(terrain, rect);
    return rect;
}

TerrainFieldRect TerrainCollisionField::AlignToBlocks(const TerrainFieldRect& rect) const noexcept {
    return { rect.minX / blockSize_ * blockSize_, rect.minZ / blockSize_ * blockSize_,
        std::min(CeilDiv(rect.maxX + 1U, blockSize_) * blockSize_, sampleCount_) - 1U,
        std::min(CeilDiv(rect.maxZ + 1U, blockSize_) * blockSize_, sampleCount_) - 1U };
}

void TerrainCollisionField::Resample(const kb::assets::TerrainAsset& terrain, const TerrainFieldRect& rect) {
    for (std::uint32_t z = rect.minZ; z <= rect.maxZ; ++z) {
        for (std::uint32_t x = rect.minX; x <= rect.maxX; ++x) {
            samples_[std::size_t{ z } * sampleCount_ + x] = IsHole(terrain, step_, x, z)
                ? kNoCollision
                : terrain.heights[std::size_t{ z * step_ } * terrain.width + x * step_];
        }
    }
}

JPH::Ref<JPH::HeightFieldShape> CreateTerrainHeightField(const TerrainCollisionField& field, std::string& error) {
    float lowest = std::numeric_limits<float>::max();
    float highest = std::numeric_limits<float>::lowest();
    for (const float height : field.Samples()) {
        if (height == kNoCollision) continue;
        lowest = std::min(lowest, height);
        highest = std::max(highest, height);
    }
    if (lowest > highest) lowest = highest = 0.0F;
    // Jolt quantizes heights over [min, max] of the shape, and an update can only write inside that range, so
    // leave room for the ground to rise and sink before the shape has to be made again.
    const float margin = std::max(32.0F, (highest - lowest) * 0.5F);
    JPH::HeightFieldShapeSettings settings(field.Samples().data(), JPH::Vec3(field.OriginX(), 0.0F, field.OriginZ()),
        JPH::Vec3(field.SpacingX(), 1.0F, field.SpacingZ()), field.SampleCount());
    settings.mBlockSize = field.BlockSize();
    settings.mBitsPerSample = kBitsPerSample;
    settings.mMinHeightValue = lowest - margin;
    settings.mMaxHeightValue = highest + margin;
    const JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) {
        error = "Terrain height field construction failed: " + std::string{ result.GetError().c_str() };
        return {};
    }
    return static_cast<JPH::HeightFieldShape*>(result.Get().GetPtr());
}

bool ApplyTerrainFieldRect(JPH::HeightFieldShape& shape, const TerrainCollisionField& field, const TerrainFieldRect& rect) {
    const TerrainFieldRect aligned = field.AlignToBlocks(rect);
    const std::uint32_t count = field.SampleCount();
    float lowest = std::numeric_limits<float>::max();
    float highest = std::numeric_limits<float>::lowest();
    for (std::uint32_t z = aligned.minZ; z <= aligned.maxZ; ++z) {
        for (std::uint32_t x = aligned.minX; x <= aligned.maxX; ++x) {
            const float height = field.Samples()[std::size_t{ z } * count + x];
            if (height == kNoCollision) continue;
            lowest = std::min(lowest, height);
            highest = std::max(highest, height);
        }
    }
    if (lowest < shape.GetMinHeightValue() || highest > shape.GetMaxHeightValue()) return false;
    JPH::TempAllocatorMalloc allocator;
    shape.SetHeights(aligned.minX, aligned.minZ, aligned.maxX - aligned.minX + 1U, aligned.maxZ - aligned.minZ + 1U,
        field.Samples().data() + std::size_t{ aligned.minZ } * count + aligned.minX, count, allocator);
    return true;
}

} // namespace kb::physics_jolt
