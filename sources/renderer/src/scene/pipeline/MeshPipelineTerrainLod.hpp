#pragma once

#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

// The level of detail of a terrain mesh is chosen for every chunk, not for the mesh: a chunk shows the coarsest level
// whose error stays under the screen error of the terrain (RenderTerrainChunkGrid), measured in the pixels of the
// view that draws it. A chunk beside one at another level also draws its skirt.
class MeshPipelineTerrainLod {
public:
    MeshPipelineTerrainLod() = delete;

    static constexpr std::uint8_t kCulled = 0xFFU;
    static constexpr std::uint8_t kSkirt = 0x80U;

    [[nodiscard]] static bool IsTerrain(const RenderMeshResource* mesh) noexcept {
        return mesh != nullptr && mesh->terrainChunks.Valid();
    }
    // For every instance and chunk (instance-major) the level to draw, with kSkirt added where a neighbour is at
    // another level, or kCulled where the chunk is out of the frustum.
    static void SelectChunkLevels(
        const RenderMeshResource& mesh,
        std::span<const SceneRenderMeshInstance> instances,
        const SceneRenderCamera* camera,
        const MeshPipelineFrustum& frustum,
        std::vector<std::uint8_t>& levels);
    // The sections those levels draw, each once, chunk by chunk.
    static void CollectSections(
        const RenderMeshResource& mesh,
        std::span<const std::uint8_t> levels,
        std::size_t instanceCount,
        std::vector<std::uint32_t>& sections);
    [[nodiscard]] static bool Draws(
        const RenderMeshResource& mesh,
        std::span<const std::uint8_t> levels,
        std::size_t instanceIndex,
        const RenderMeshSection& section) noexcept;
};

} // namespace kb::render
