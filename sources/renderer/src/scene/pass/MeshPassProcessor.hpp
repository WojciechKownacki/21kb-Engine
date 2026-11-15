#pragma once

#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/batch/SceneMeshBatch.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace kb::ecs {
class WorkerPool;
}

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
    const kb::scene::ScenePortalVisibility* portalVisibility = nullptr;
    SceneRenderDiagnostics* diagnostics = nullptr;
    std::uint32_t maxDrawCommands = 0;
    std::uint32_t maxVisibleInstances = 0;
    std::span<const std::uint64_t> selectedEntityIds{};
    MeshPipelineResourceValidation resourceValidation = MeshPipelineResourceValidation::ResolveAndValidate;
    bool terrainLayersOnly = false;
    SceneMeshBatchCommandCache* batchCommandCache = nullptr;
    SceneGpuDrivenFeatureSupport gpuDrivenSupport{};
    std::uint64_t motionFrame = 0U;
    kb::ecs::WorkerPool* workerPool = nullptr;
};

// A pass to build and the result it fills.
struct MeshPassBuild {
    MeshPassProcessorDesc desc;
    MeshPipelineBuildResult* result = nullptr;
};

class MeshPassProcessor {
public:
    MeshPassProcessor() = delete;

    // A pass over fewer instances than this is scanned on the calling thread: handing it to workers costs more than
    // it saves.
    static constexpr std::size_t ParallelThreshold = 2048U;

    static void BuildCommandsInto(const MeshPassProcessorDesc& desc, MeshPipelineBuildResult& result) noexcept;

    // Builds several passes at once, each as BuildCommandsInto builds it alone, on the workers of the first pass (the
    // `workerPool` of the others is not read). Their results must be distinct and none may use a batch command cache.
    static void BuildCommandsInto(std::span<const MeshPassBuild> passes) noexcept;
};

} // namespace kb::render
