#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kb::render {

// A coarse world-space voxel copy of the scene around a focus point, used to gather bounce light from
// objects the camera cannot see. Every mesh instance is rasterised with the base colour (and emissive
// colour) of its material, so the grid is independent of the view; it is rebuilt when the scene, the
// materials or the snapped grid origin change. A mesh that kept a CPU copy of its triangles
// (RenderMeshProxyGeometry) fills only the voxels its triangles touch, through a per-mesh occupancy mask
// built once; any other mesh fills its oriented bounding box.
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
    // Voxels marked occupied by the last rebuild.
    [[nodiscard]] std::uint32_t OccupiedVoxelCount() const noexcept { return occupiedVoxels_; }

private:
    // Occupancy of a mesh in a cubic-cell grid over its bounds, with a summed-volume table so a box of cells
    // is tested in constant time.
    struct MeshMask {
        std::uint64_t version = 0U;
        bool usable = false;
        std::array<float, 3> minCorner{};
        std::array<float, 3> center{};
        std::array<float, 3> halfExtents{};
        float cell = 1.0F;
        std::array<int, 3> dims{};
        std::vector<std::uint32_t> prefix; // (dims + 1)^3, sum of cells below each corner
    };

    [[nodiscard]] static MeshMask BuildMask(const RenderMeshProxyGeometry& geometry);
    // True when any cell of `mask` in the inclusive local box [lo, hi] (in local units) is occupied.
    [[nodiscard]] static bool MaskTouches(const MeshMask& mask, const std::array<float, 3>& lo, const std::array<float, 3>& hi) noexcept;

    bgfx::TextureHandle albedo_ = BGFX_INVALID_HANDLE;   // RGBA8: albedo, a = occupied
    bgfx::TextureHandle emissive_ = BGFX_INVALID_HANDLE; // RGBA16F: emissive radiance
    std::array<float, 4> origin_{};
    std::uint64_t signature_ = 0U;
    std::vector<std::uint8_t> albedoVoxels_;
    std::vector<std::uint16_t> emissiveVoxels_;
    std::unordered_map<std::uint64_t, MeshMask> masks_;
    std::uint32_t occupiedVoxels_ = 0U;
};

} // namespace kb::render
