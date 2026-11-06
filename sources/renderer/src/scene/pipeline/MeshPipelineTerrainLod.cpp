#include "scene/pipeline/MeshPipelineTerrainLod.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::render {
namespace {

// Longest axis of the model: units of the mesh become world units by at most this much.
[[nodiscard]] float LargestScale(const std::array<float, 16>& model) noexcept {
    const auto length = [&model](std::size_t column) {
        const std::size_t first = column * 4U;
        return std::sqrt(model[first] * model[first] + model[first + 1U] * model[first + 1U] + model[first + 2U] * model[first + 2U]);
    };
    return std::max({ length(0U), length(1U), length(2U) });
}

// Whether a chunk next to this one in the grid is drawn at another level.
[[nodiscard]] bool NeighbourDiffers(
    const RenderTerrainChunkGrid& grid,
    std::span<const std::uint8_t> chosen,
    std::uint32_t chunk) noexcept {
    const std::uint32_t chunkX = chunk % grid.chunkCountX;
    const std::uint32_t chunkZ = chunk / grid.chunkCountX;
    const std::uint8_t own = chosen[chunk];
    const auto differs = [&](std::uint32_t other) { return chosen[other] != own; };
    return (chunkX > 0U && differs(chunk - 1U)) || (chunkX + 1U < grid.chunkCountX && differs(chunk + 1U)) ||
        (chunkZ > 0U && differs(chunk - grid.chunkCountX)) || (chunkZ + 1U < grid.chunkCountZ && differs(chunk + grid.chunkCountX));
}

} // namespace

void MeshPipelineTerrainLod::SelectChunkLevels(
    const RenderMeshResource& mesh,
    std::span<const SceneRenderMeshInstance> instances,
    const SceneRenderCamera* camera,
    const MeshPipelineFrustum& frustum,
    std::vector<std::uint8_t>& levels) {
    const RenderTerrainChunkGrid& grid = mesh.terrainChunks;
    const std::uint32_t chunkCount = grid.ChunkCount();
    levels.assign(instances.size() * chunkCount, kCulled);
    if (camera == nullptr) {
        // No view to measure by (a pass that only validates resources): every chunk is drawn at full resolution.
        for (std::size_t instanceIndex = 0U; instanceIndex < instances.size(); ++instanceIndex) {
            for (std::uint32_t chunk = 0U; chunk < chunkCount; ++chunk) {
                if (grid.sectionIndices[chunk] != UINT32_MAX) levels[instanceIndex * chunkCount + chunk] = 0U;
            }
        }
        return;
    }
    const bool orthographic = MeshPipelineVisibility::IsOrthographic(*camera);
    const std::array<float, 3> eye = MeshPipelineVisibility::CameraPosition(*camera);
    const float viewportHeight = static_cast<float>(
        camera->viewportHeight != 0U ? camera->viewportHeight : SceneRenderCamera::kDefaultViewportHeight);
    // Pixels a unit of the view covers at distance one (the whole view for an orthographic camera).
    const float pixelsPerUnit = 0.5F * viewportHeight * std::abs(camera->projection[5]);
    std::vector<std::uint8_t> chosen(chunkCount);
    std::vector<std::uint8_t> visible(chunkCount);
    const std::int32_t lastLevel = static_cast<std::int32_t>(grid.lodCount) - 1;
    for (std::size_t instanceIndex = 0U; instanceIndex < instances.size(); ++instanceIndex) {
        const SceneRenderMeshInstance& instance = instances[instanceIndex];
        const float scale = LargestScale(instance.model);
        for (std::uint32_t chunk = 0U; chunk < chunkCount; ++chunk) {
            visible[chunk] = 0U;
            chosen[chunk] = 0U;
            if (grid.sectionIndices[chunk] == UINT32_MAX) continue;
            const RenderMeshSection& anchor = mesh.sections[grid.sectionIndices[chunk]];
            const RenderBoundsSphere bounds = MeshPipelineVisibility::TransformBounds(anchor.bounds, instance.model);
            visible[chunk] = MeshPipelineVisibility::IsInsideFrustum(frustum, bounds) ? 1U : 0U;
            std::int32_t level = 0;
            if (instance.lodEnabled) {
                const float dx = bounds.center[0] - eye[0];
                const float dy = bounds.center[1] - eye[1];
                const float dz = bounds.center[2] - eye[2];
                const float distance = std::max(std::sqrt(dx * dx + dy * dy + dz * dz) - bounds.radius, 0.01F);
                const float pixelsPerMeshUnit = scale * (orthographic ? pixelsPerUnit : pixelsPerUnit / distance);
                for (std::int32_t candidate = lastLevel; candidate > 0; --candidate) {
                    if (grid.lodError[static_cast<std::size_t>(chunk) * grid.lodCount + candidate] * pixelsPerMeshUnit <= grid.lodPixelError) {
                        level = candidate;
                        break;
                    }
                }
                level = std::clamp(level + instance.lodBias, 0, lastLevel);
            }
            // A level has no triangles where holes took every coarse cell of the chunk; a finer one still draws.
            while (level > 0 && grid.sectionIndices[static_cast<std::size_t>(level) * chunkCount + chunk] == UINT32_MAX) --level;
            chosen[chunk] = static_cast<std::uint8_t>(level);
        }
        std::uint8_t* out = levels.data() + instanceIndex * chunkCount;
        for (std::uint32_t chunk = 0U; chunk < chunkCount; ++chunk) {
            if (visible[chunk] == 0U) continue;
            const std::size_t task = static_cast<std::size_t>(chosen[chunk]) * chunkCount + chunk;
            const bool skirt = grid.skirtSectionIndices[task] != UINT32_MAX && NeighbourDiffers(grid, chosen, chunk);
            out[chunk] = static_cast<std::uint8_t>(chosen[chunk] | (skirt ? kSkirt : 0U));
        }
    }
}

void MeshPipelineTerrainLod::CollectSections(
    const RenderMeshResource& mesh,
    std::span<const std::uint8_t> levels,
    std::size_t instanceCount,
    std::vector<std::uint32_t>& sections) {
    const RenderTerrainChunkGrid& grid = mesh.terrainChunks;
    const std::uint32_t chunkCount = grid.ChunkCount();
    const std::uint32_t layers = std::max<std::uint32_t>(mesh.terrainLayerCount, 1U);
    sections.clear();
    for (std::uint32_t chunk = 0U; chunk < chunkCount; ++chunk) {
        // Bit lod: that level is listed; bit 16 + lod: its skirt is.
        std::uint32_t listed = 0U;
        for (std::size_t instanceIndex = 0U; instanceIndex < instanceCount; ++instanceIndex) {
            const std::uint8_t value = levels[instanceIndex * chunkCount + chunk];
            if (value == kCulled) continue;
            const std::uint32_t lod = value & ~kSkirt;
            const std::size_t task = static_cast<std::size_t>(lod) * chunkCount + chunk;
            if ((listed & (1U << lod)) == 0U) {
                listed |= 1U << lod;
                for (std::uint32_t layer = 0U; layer < layers; ++layer) sections.push_back(grid.sectionIndices[task] + layer);
            }
            if ((value & kSkirt) != 0U && (listed & (1U << (16U + lod))) == 0U) {
                listed |= 1U << (16U + lod);
                sections.push_back(grid.skirtSectionIndices[task]);
            }
        }
    }
}

bool MeshPipelineTerrainLod::Draws(
    const RenderMeshResource& mesh,
    std::span<const std::uint8_t> levels,
    std::size_t instanceIndex,
    const RenderMeshSection& section) noexcept {
    const std::uint8_t value = levels[instanceIndex * mesh.terrainChunks.ChunkCount() + section.terrainChunk];
    if (value == kCulled || (value & ~kSkirt) != section.lodLevel) return false;
    return !section.terrainSkirt || (value & kSkirt) != 0U;
}

} // namespace kb::render
