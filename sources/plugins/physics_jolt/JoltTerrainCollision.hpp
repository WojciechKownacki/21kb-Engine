#pragma once

#include "engine/assets/TerrainAsset.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kb::physics_jolt {

// Inclusive rectangle of field samples.
struct TerrainFieldRect {
    std::uint32_t minX = 0U;
    std::uint32_t minZ = 0U;
    std::uint32_t maxX = 0U;
    std::uint32_t maxZ = 0U;
};

[[nodiscard]] TerrainFieldRect MergeTerrainFieldRects(const TerrainFieldRect& first, const TerrainFieldRect& second) noexcept;

// A terrain asset laid out the way a Jolt height field wants it: a square grid of heights, one every
// collisionStep-th terrain sample, at least two blocks wide, with the samples past the terrain (a terrain need
// not be square) and the holes marked as no collision. The grid is kept after the shape is made so a changed
// rectangle of the terrain can be resampled without reading the asset again.
class TerrainCollisionField {
public:
    // Fails with the reason in `error` for an invalid terrain.
    [[nodiscard]] static std::optional<TerrainCollisionField> Build(const kb::assets::TerrainAsset& terrain, std::string& error);

    // Whether `terrain` has the grid this field was built for (same samples, step and size); a terrain that was
    // resized or reconfigured needs a new field.
    [[nodiscard]] bool Matches(const kb::assets::TerrainAsset& terrain) const noexcept;
    // Resamples what the terrain samples of `region` (inclusive) influence and returns the field rectangle that
    // was resampled; nullopt when the region lies outside the terrain or `terrain` is not the one this field has.
    [[nodiscard]] std::optional<TerrainFieldRect> Update(
        const kb::assets::TerrainAsset& terrain, const kb::assets::TerrainSampleRegion& region);

    [[nodiscard]] std::uint32_t SampleCount() const noexcept { return sampleCount_; }
    [[nodiscard]] TerrainFieldRect WholeRect() const noexcept { return { 0U, 0U, sampleCount_ - 1U, sampleCount_ - 1U }; }
    [[nodiscard]] const std::vector<float>& Samples() const noexcept { return samples_; }
    [[nodiscard]] float SpacingX() const noexcept { return spacingX_; }
    [[nodiscard]] float SpacingZ() const noexcept { return spacingZ_; }
    [[nodiscard]] float OriginX() const noexcept { return originX_; }
    [[nodiscard]] float OriginZ() const noexcept { return originZ_; }
    [[nodiscard]] std::uint32_t BlockSize() const noexcept { return blockSize_; }
    // The rectangle aligned outwards to whole blocks, as Jolt wants it for SetHeights.
    [[nodiscard]] TerrainFieldRect AlignToBlocks(const TerrainFieldRect& rect) const noexcept;

private:
    void Resample(const kb::assets::TerrainAsset& terrain, const TerrainFieldRect& rect);

    std::uint32_t step_ = 1U;
    std::uint32_t columns_ = 0U;
    std::uint32_t rows_ = 0U;
    std::uint32_t blockSize_ = 2U;
    std::uint32_t sampleCount_ = 0U;
    float spacingX_ = 1.0F;
    float spacingZ_ = 1.0F;
    float originX_ = 0.0F;
    float originZ_ = 0.0F;
    std::vector<float> samples_;
};

// The static shape of a field. Fails with the reason in `error`.
[[nodiscard]] JPH::Ref<JPH::HeightFieldShape> CreateTerrainHeightField(const TerrainCollisionField& field, std::string& error);

// Writes the field samples of `rect` (aligned to blocks inside) into the shape, which has to have been made from
// this field. Returns false and leaves the shape alone when a height lies outside the range the shape can hold;
// the caller then makes the shape again.
[[nodiscard]] bool ApplyTerrainFieldRect(JPH::HeightFieldShape& shape, const TerrainCollisionField& field, const TerrainFieldRect& rect);

} // namespace kb::physics_jolt
