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
        std::span<const std::uint64_t> selectedEntityIds,
        std::uint32_t cullingMask) noexcept;
    [[nodiscard]] static std::uint32_t CountCandidateInstances(
        MeshPassType pass,
        const SceneMeshBatch& batch,
        std::span<const std::uint64_t> selectedEntityIds,
        std::uint32_t cullingMask) noexcept;
    [[nodiscard]] static bool Accepts(
        MeshPassType pass,
        const SceneRenderMeshInstance& instance,
        const RenderMaterialResource* material,
        std::span<const std::uint64_t> selectedEntityIds,
        std::uint32_t cullingMask,
        const RenderMeshSection* section = nullptr) noexcept;
    // Velocity is drawn only for primitives that move on their own: a moved transform, a skeleton or a
    // material that offsets its vertices. Everything else keeps the camera motion of the full-screen pass.
    [[nodiscard]] static bool HasOwnMotion(
        const SceneRenderMeshInstance& instance,
        const RenderMaterialResource* material,
        std::uint64_t motionFrame) noexcept;
    [[nodiscard]] static bool UsesDisabledAlphaBlend(const RenderMaterialResource* material) noexcept;
    [[nodiscard]] static bool UsesAdvancedBuiltinPbr(const RenderMaterialResource* material) noexcept;
    [[nodiscard]] static std::uint64_t State(
        MeshPassType pass,
        const RenderMeshResource* mesh,
        const RenderMaterialResource* material,
        const RenderMeshSection* section = nullptr) noexcept;
};

} // namespace kb::render
