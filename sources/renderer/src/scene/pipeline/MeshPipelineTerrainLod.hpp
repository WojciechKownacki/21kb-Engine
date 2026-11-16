#pragma once

#include "kb/render/scene/MeshPipeline.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

// Chooses the level of detail of every chunk of a terrain from its distance to the viewer, so the chunks near the viewer
// draw finer than the far ones, and picks for each the index range that joins its edges to coarser neighbours.
class MeshPipelineTerrainLod {
public:
    // A chunk's choice is one byte: its level in the low bits, the edges whose neighbour is a level coarser above them.
    static constexpr std::uint8_t kLevelMask = 0x0FU;
    static constexpr std::uint32_t kEdgeShift = 4U;

    // A chunk keeps its level while the distance, measured in levels (the log2 of the distance in chunk sizes), stays
    // within this much of the range of that level: a viewer that sways around a border does not switch it.
    static constexpr float kHysteresis = 0.1F;

    // The viewer a pass chooses levels for: the camera's eye, or for a pass whose camera is not the viewer's (a shadow
    // cascade) the viewer it carries, so such a pass chooses what the viewer's pass chooses. False when the pass has none.
    [[nodiscard]] static bool Viewer(const SceneRenderCamera& camera, MeshPassType pass, std::array<float, 3>& viewer) noexcept;

    // Fills `chosen` with one byte per chunk of the terrain `mesh` for `instance`. `previous` holds what the viewer's
    // pass chose last (empty when it chose nothing yet). Neighbouring chunks differ by at most one level.
    static void Choose(
        const RenderMeshResource& mesh,
        const SceneRenderMeshInstance& instance,
        const std::array<float, 3>& viewer,
        std::span<const std::uint8_t> previous,
        std::vector<std::uint8_t>& chosen);

    // The chosen levels of a terrain drawn without a viewer: every chunk at its finest.
    static void ChooseFinest(const RenderMeshResource& mesh, std::vector<std::uint8_t>& chosen);

    // The index range chunk section `section` of the terrain draws when its chunk was chosen as `choice`.
    [[nodiscard]] static std::pair<std::uint32_t, std::uint32_t> IndexRange(
        const RenderMeshResource& mesh, const RenderMeshSection& section, std::uint8_t choice) noexcept;
};

} // namespace kb::render
