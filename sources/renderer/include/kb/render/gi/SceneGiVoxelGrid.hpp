#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <vector>

namespace kb::render {

// A coarse world-space voxel copy of the scene around a focus point, used to gather bounce light from
// objects the camera cannot see. Every mesh instance is rasterised as its oriented bounding box with the
// base colour (and emissive colour) of its material, so the grid is independent of the view; it is
// rebuilt when the scene, the materials or the snapped grid origin change.
class SceneGiVoxelGrid {
public:
    static constexpr std::uint32_t kDimension = 64U;

    ~SceneGiVoxelGrid();

    SceneGiVoxelGrid() = default;
    SceneGiVoxelGrid(const SceneGiVoxelGrid&) = delete;
    SceneGiVoxelGrid& operator=(const SceneGiVoxelGrid&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool IsValid() const noexcept { return bgfx::isValid(albedo_) && bgfx::isValid(emissive_); }

    // Centres the grid on `focus` (snapped to whole voxels) and re-rasterises it when anything it depends on changed.
    void Update(const RenderScene& renderScene, const RenderResourceRegistry& resources, const SceneRenderResourceMap& resourceMap,
        const std::array<float, 3>& focus, float voxelSize);

    [[nodiscard]] bgfx::TextureHandle Albedo() const noexcept { return albedo_; }
    [[nodiscard]] bgfx::TextureHandle Emissive() const noexcept { return emissive_; }
    // xyz = world position of the grid's minimum corner, w = voxel size.
    [[nodiscard]] const std::array<float, 4>& Origin() const noexcept { return origin_; }

private:
    bgfx::TextureHandle albedo_ = BGFX_INVALID_HANDLE;   // RGBA8: albedo, a = occupied
    bgfx::TextureHandle emissive_ = BGFX_INVALID_HANDLE; // RGBA16F: emissive radiance
    std::array<float, 4> origin_{};
    std::uint64_t signature_ = 0U;
    std::vector<std::uint8_t> albedoVoxels_;
    std::vector<std::uint16_t> emissiveVoxels_;
};

} // namespace kb::render
