#pragma once

#include "kb/render/resources/RenderHandles.hpp"
#include "kb/render/resources/RenderSkinningPaletteAllocator.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "kb/render/scene/MeshPassType.hpp"
#include "kb/render/scene/SceneGpuDrivenFrameResources.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"
#include "kb/render/scene/batch/SceneMeshBatch.hpp"
#include "kb/render/scene/cache/SceneCachedDrawCommand.hpp"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace kb::render {

class RenderResourceRegistry;
class SceneRenderResourceMap;
enum class RenderPassKind : std::uint8_t;

[[nodiscard]] const char* MeshPassTypeName(MeshPassType pass) noexcept;
[[nodiscard]] std::optional<MeshPassType> MeshPassForRenderPassKind(RenderPassKind kind) noexcept;

enum class MeshPipelineResourceValidation : std::uint8_t {
    ResolveAndValidate,
    Skip,
};

struct MeshDrawCommand {
    MeshPassType pass = MeshPassType::BaseOpaque;
    std::uint64_t meshAssetId = 0;
    std::uint64_t materialAssetId = 0;
    std::uint32_t sectionIndex = 0;
    std::uint32_t materialSlot = 0;
    std::uint32_t firstMeshlet = 0;
    std::uint32_t meshletCount = 0;
    std::uint32_t indexStart = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t vertexStart = 0;
    std::uint32_t vertexCount = 0;
    std::uint8_t lodLevel = 0;
    std::uint8_t terrainLayerIndex = UINT8_MAX;
    std::uint16_t depthBucket = 0;
    RenderMeshHandle mesh{};
    RenderMaterialHandle material{};
    const RenderMeshResource* meshResource = nullptr;
    const RenderMaterialResource* materialResource = nullptr;
    std::uint64_t state = 0;
    std::uint64_t sortKey = 0;
    RenderSkinningPaletteHandle currentSkinningPalette{};
    RenderSkinningPaletteHandle previousSkinningPalette{};
    std::vector<SceneRenderMeshInstance> instances;
    // Nonzero only for a successfully validated, retained instance list.
    std::uint64_t instanceRevision = 0U;
};

struct MeshPipelineBuildDesc {
    MeshPassType pass = MeshPassType::BaseOpaque;
    const std::vector<SceneMeshBatch>* meshBatches = nullptr;
    const std::vector<SceneRenderDrawGroup>* drawGroups = nullptr;
    const RenderResourceRegistry* resources = nullptr;
    const SceneRenderResourceMap* resourceMap = nullptr;
    const RenderMeshResource* resolvedMeshResource = nullptr;
    const RenderMaterialResource* resolvedMaterialResource = nullptr;
    const SceneRenderCamera* camera = nullptr;
    std::span<const SceneRenderVisibilityBlocker> visibilityBlockers{};
    SceneRenderDiagnostics* diagnostics = nullptr;
    std::uint32_t maxDrawCommands = 0;
    std::uint32_t maxVisibleInstances = 0;
    std::uint32_t maxDroppedInstances = 0;
    std::span<const std::uint64_t> selectedEntityIds{};
    SceneGpuDrivenFeatureSupport gpuDrivenSupport{};
    MeshPipelineResourceValidation resourceValidation = MeshPipelineResourceValidation::ResolveAndValidate;
    bool terrainLayersOnly = false;
};

struct MeshCommandLookupKey {
    std::uint64_t materialAssetId = 0;
    std::uint64_t materialHandleValue = 0;
    RenderSkinningPaletteHandle currentSkinningPalette{};
    RenderSkinningPaletteHandle previousSkinningPalette{};
    bool reversedWinding = false;

    [[nodiscard]] friend constexpr bool operator==(MeshCommandLookupKey lhs, MeshCommandLookupKey rhs) noexcept = default;
};

struct MeshCommandLookupKeyHash {
    [[nodiscard]] std::size_t operator()(MeshCommandLookupKey key) const noexcept;
};

struct MeshPipelineMaterialResolution {
    RenderMaterialHandle handle{};
    const RenderMaterialResource* resource = nullptr;
};

struct MeshPipelineTransparentInstanceRef {
    std::uint32_t commandIndex = 0;
    std::uint32_t instanceIndex = 0;
    std::uint32_t gpuDrivenRecordIndex = UINT32_MAX;
    float viewDepth = 0.0F;
};

struct MeshPipelineBuildResult {
    std::vector<MeshDrawCommand> commands;
    std::vector<MeshDrawCommand> transparentSourceCommands;
    std::vector<MeshPipelineTransparentInstanceRef> transparentInstanceScratch;
    // Transient adapter storage for draw-group input. Cleared before BuildInto returns because batches contain spans.
    std::vector<SceneMeshBatch> meshBatchScratch;
    SceneCachedDrawCommandStore drawCommandCache;
    std::vector<SceneGpuDrivenInputRecord> gpuDrivenInputRecords;
    std::unordered_map<MeshCommandLookupKey, std::size_t, MeshCommandLookupKeyHash> commandLookupScratch;
    // Per-build cache for resource resolution shared by every instance using one material.
    // It is renderer-owned scratch and cleared at the next BuildInto call.
    std::unordered_map<std::uint64_t, MeshPipelineMaterialResolution> materialResolutionScratch;
    // Renderer-owned frame cache. It holds only the resolved level, never authored
    // component data; caller reuse preserves hysteresis across submissions.
    std::unordered_map<std::uint64_t, std::uint8_t> detailSwitchLevels;
    std::unordered_map<std::uint64_t, std::uint8_t> detailSwitchPreviousLevels;
    std::uint64_t detailSwitchHistoryRevision = 1U;
    SceneRenderSubmitStats stats{};
};

class MeshPipelineProcessor {
public:
    MeshPipelineProcessor() = delete;

    [[nodiscard]] static MeshPipelineBuildResult Build(const MeshPipelineBuildDesc& desc) noexcept;
    static void BuildInto(const MeshPipelineBuildDesc& desc, MeshPipelineBuildResult& result) noexcept;
    static void CountCommandsAsSubmitted(SceneRenderSubmitStats& stats, const std::vector<MeshDrawCommand>& commands) noexcept;
};

} // namespace kb::render
