#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <cstdint>
#include <span>

namespace kb::render {

struct MeshPipelineResolvedMesh {
    RenderMeshHandle handle{};
    const RenderMeshResource* resource = nullptr;
};

class MeshPipelineResourceResolver {
public:
    MeshPipelineResourceResolver() = delete;

    [[nodiscard]] static MeshPipelineResolvedMesh ResolveMeshGroup(
        MeshPassType pass,
        const SceneRenderDrawGroup& group,
        std::uint32_t passInstanceCount,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        SceneRenderSubmitStats& stats,
        SceneRenderDiagnostics* diagnostics,
        std::span<const std::uint64_t> selectedEntityIds) noexcept;
    [[nodiscard]] static const RenderMaterialResource* ResolveMaterialOrFallback(
        const SceneRenderMeshInstance& instance,
        std::uint64_t materialAssetId,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        RenderMaterialHandle& outHandle,
        SceneRenderSubmitStats& stats,
        SceneRenderDiagnostics* diagnostics) noexcept;
    static void ValidateMaterialTextureOrFallback(
        const SceneRenderMeshInstance& instance,
        std::uint64_t materialAssetId,
        const RenderMaterialResource* material,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        SceneRenderSubmitStats& stats,
        SceneRenderDiagnostics* diagnostics) noexcept;
    [[nodiscard]] static std::uint64_t MaterialAssetForSectionInstance(
        const SceneRenderDrawGroup& group,
        const SceneRenderMeshInstance& instance,
        const RenderMeshResource* meshResource,
        const RenderMeshSection& section) noexcept;
};

} // namespace kb::render
