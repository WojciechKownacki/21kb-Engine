#include "scene/pipeline/MeshPipelineTerrainLod.hpp"

#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <algorithm>
#include <cmath>

namespace kb::render {
namespace {

// Stands for the level of a chunk that draws nothing, which no neighbour has to match.
constexpr int kAbsent = 255;

[[nodiscard]] bool IsOrthographic(const SceneRenderCamera& camera) noexcept {
    return camera.projection[3] == 0.0F && camera.projection[7] == 0.0F &&
        camera.projection[11] == 0.0F && camera.projection[15] != 0.0F;
}

[[nodiscard]] float LargestScale(const std::array<float, 16>& model) noexcept {
    float largest = 0.0F;
    for (std::size_t column = 0U; column < 3U; ++column) {
        const float* axis = model.data() + column * 4U;
        largest = std::max(largest, std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]));
    }
    return largest;
}

} // namespace

bool MeshPipelineTerrainLod::Viewer(const SceneRenderCamera& camera, MeshPassType pass, std::array<float, 3>& viewer) noexcept {
    if (pass == MeshPassType::ShadowDepth) {
        if (!(camera.distanceLimit > 0.0F)) return false;
        viewer = camera.distanceOrigin;
        return true;
    }
    if (IsOrthographic(camera)) return false;
    // The view is a rigid transform, so the camera sits at -R^T t.
    const std::array<float, 16>& view = camera.view;
    viewer = { -(view[0] * view[12] + view[1] * view[13] + view[2] * view[14]),
               -(view[4] * view[12] + view[5] * view[13] + view[6] * view[14]),
               -(view[8] * view[12] + view[9] * view[13] + view[10] * view[14]) };
    return true;
}

void MeshPipelineTerrainLod::ChooseFinest(const RenderMeshResource& mesh, std::vector<std::uint8_t>& chosen) {
    chosen.assign(mesh.terrainGrid.chunkSections.size(), 0U);
}

void MeshPipelineTerrainLod::Choose(
    const RenderMeshResource& mesh,
    const SceneRenderMeshInstance& instance,
    const std::array<float, 3>& viewer,
    std::span<const std::uint8_t> previous,
    std::vector<std::uint8_t>& chosen) {
    const RenderTerrainChunkGrid& grid = mesh.terrainGrid;
    const std::uint32_t countX = grid.chunkCountX;
    const std::uint32_t countZ = grid.chunkCountZ;
    const std::size_t count = grid.chunkSections.size();
    if (!instance.lodEnabled) {
        ChooseFinest(mesh, chosen);
        return;
    }
    const int maxLevel = static_cast<int>(mesh.lods.size()) - 1;
    const float chunkSize = std::max(grid.chunkSize * LargestScale(instance.model), 1.0e-6F);
    const bool hasPrevious = previous.size() == count;
    std::vector<int> level(count, kAbsent);
    for (std::size_t chunk = 0U; chunk < count; ++chunk) {
        if (grid.chunkSections[chunk] == UINT32_MAX) continue;
        const RenderBoundsSphere bounds = MeshPipelineVisibility::TransformBounds(
            mesh.sections[grid.chunkSections[chunk]].bounds, instance.model);
        const float dx = bounds.center[0] - viewer[0];
        const float dy = bounds.center[1] - viewer[1];
        const float dz = bounds.center[2] - viewer[2];
        const float distance = std::max(std::sqrt(dx * dx + dy * dy + dz * dz) - bounds.radius, 0.0F);
        const float range = std::log2(std::max(distance, chunkSize) / chunkSize) + static_cast<float>(instance.lodBias);
        const int held = hasPrevious ? static_cast<int>(previous[chunk] & kLevelMask) : -1;
        level[chunk] = held >= 0 && range >= static_cast<float>(held) - kHysteresis &&
                range < static_cast<float>(held) + 1.0F + kHysteresis
            ? std::min(held, maxLevel)
            : std::clamp(static_cast<int>(std::floor(range)), 0, maxLevel);
    }
    // A chunk is never more than one level coarser than a neighbour: each level is at most one above its neighbours'.
    const auto limit = [&](std::size_t chunk, std::size_t neighbour) {
        if (level[chunk] != kAbsent) level[chunk] = std::min(level[chunk], level[neighbour] + 1);
    };
    for (std::uint32_t z = 0U; z < countZ; ++z) {
        for (std::uint32_t x = 0U; x < countX; ++x) {
            const std::size_t chunk = static_cast<std::size_t>(z) * countX + x;
            if (x > 0U) limit(chunk, chunk - 1U);
            if (z > 0U) limit(chunk, chunk - countX);
        }
    }
    for (std::uint32_t z = countZ; z-- > 0U;) {
        for (std::uint32_t x = countX; x-- > 0U;) {
            const std::size_t chunk = static_cast<std::size_t>(z) * countX + x;
            if (x + 1U < countX) limit(chunk, chunk + 1U);
            if (z + 1U < countZ) limit(chunk, chunk + countX);
        }
    }
    chosen.assign(count, 0U);
    for (std::uint32_t z = 0U; z < countZ; ++z) {
        for (std::uint32_t x = 0U; x < countX; ++x) {
            const std::size_t chunk = static_cast<std::size_t>(z) * countX + x;
            if (level[chunk] == kAbsent) continue;
            const int coarser = level[chunk] + 1;
            const unsigned edges = (x > 0U && level[chunk - 1U] == coarser ? 1U : 0U) |
                (x + 1U < countX && level[chunk + 1U] == coarser ? 2U : 0U) |
                (z > 0U && level[chunk - countX] == coarser ? 4U : 0U) |
                (z + 1U < countZ && level[chunk + countX] == coarser ? 8U : 0U);
            chosen[chunk] = static_cast<std::uint8_t>(level[chunk] | (edges << kEdgeShift));
        }
    }
}

std::pair<std::uint32_t, std::uint32_t> MeshPipelineTerrainLod::IndexRange(
    const RenderMeshResource& mesh, const RenderMeshSection& section, std::uint8_t choice) noexcept {
    const RenderTerrainEdgeVariants& variants = mesh.terrainGrid.variants[section.terrainVariants];
    const std::uint32_t edges = choice >> kEdgeShift;
    return { variants.indexStart[edges], variants.indexCount[edges] };
}

} // namespace kb::render
