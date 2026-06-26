#pragma once

#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/batch/SceneMeshBatch.hpp"

#include <cstdint>
#include <span>

namespace kb::render {

class MeshPipelinePassPolicy {
public:
    MeshPipelinePassPolicy() = delete;

    [[nodiscard]] static bool CanEverContain(
        MeshPassType pass,
        const SceneRenderMeshInstance& instance,
        std::span<const std::uint64_t> selectedEntityIds) noexcept;
    [[nodiscard]] static std::uint32_t CountCandidateInstances(
        MeshPassType pass,
        const SceneMeshBatch& batch,
        std::span<const std::uint64_t> selectedEntityIds) noexcept;
    [[nodiscard]] static bool Accepts(
        MeshPassType pass,
        const SceneRenderMeshInstance& instance,
        const RenderMaterialResource* material,
        std::span<const std::uint64_t> selectedEntityIds) noexcept;
    [[nodiscard]] static bool UsesDisabledAlphaBlend(const RenderMaterialResource* material) noexcept;
    [[nodiscard]] static std::uint64_t State(
        MeshPassType pass,
        const RenderMeshResource* mesh,
        const RenderMaterialResource* material) noexcept;
};

} // namespace kb::render
