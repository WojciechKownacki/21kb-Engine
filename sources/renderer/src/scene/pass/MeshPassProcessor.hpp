#pragma once

#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/batch/SceneMeshBatch.hpp"

#include <cstdint>
#include <span>

namespace kb::render {

struct MeshPassProcessorDesc {
    MeshPassType pass = MeshPassType::BaseOpaque;
    std::span<const SceneMeshBatch> meshBatches{};
    const RenderResourceRegistry* resources = nullptr;
    const SceneRenderResourceMap* resourceMap = nullptr;
    const RenderMeshResource* resolvedMeshResource = nullptr;
    const RenderMaterialResource* resolvedMaterialResource = nullptr;
    const SceneRenderCamera* camera = nullptr;
    std::span<const SceneRenderVisibilityBlocker> visibilityBlockers{};
    SceneRenderDiagnostics* diagnostics = nullptr;
    std::uint32_t maxDrawCommands = 0;
    std::uint32_t maxVisibleInstances = 0;
    std::span<const std::uint64_t> selectedEntityIds{};
    MeshPipelineResourceValidation resourceValidation = MeshPipelineResourceValidation::ResolveAndValidate;
};

class MeshPassProcessor {
public:
    MeshPassProcessor() = delete;

    static void BuildCommandsInto(const MeshPassProcessorDesc& desc, MeshPipelineBuildResult& result) noexcept;
};

} // namespace kb::render
