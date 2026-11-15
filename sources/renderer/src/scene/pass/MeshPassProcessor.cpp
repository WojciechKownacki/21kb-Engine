#include "scene/pass/MeshPassProcessor.hpp"
#include "scene/cache/SceneMeshBatchCommandCache.hpp"

#include "engine/ecs/QueryPrefetch.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/scene/ScenePortalVisibility.hpp"
#include "kb/render/scene/cache/SceneCachedDrawCommand.hpp"
#include "scene/cache/SceneCachedDrawCommandMaterializer.hpp"
#include "scene/cache/SceneMaterialTextureDependencySignature.hpp"
#include "scene/pipeline/MeshPipelineCommandBuilder.hpp"
#include "scene/pipeline/MeshPipelineGpuDrivenRecorder.hpp"
#include "scene/pipeline/MeshPipelinePassPolicy.hpp"
#include "scene/pipeline/MeshPipelineResourceResolver.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kb::render {
namespace {

void EmitInstanceDiagnostic(
    SceneRenderDiagnostics* diagnostics,
    SceneRenderDiagnosticKind kind,
    SceneRenderDiagnosticSeverity severity,
    const SceneRenderMeshInstance& instance,
    std::uint64_t materialAssetId) {
    if (diagnostics == nullptr) {
        return;
    }

    diagnostics->events.push_back(SceneRenderDiagnosticEvent{
        .severity = severity,
        .kind = kind,
        .entityId = instance.entityId,
        .meshAssetId = instance.meshAssetId,
        .materialAssetId = materialAssetId,
        .instanceCount = 1U,
    });
}

[[nodiscard]] bool PassUsesMaterialTextureDependencies(MeshPassType pass) noexcept {
    switch (pass) {
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
    case MeshPassType::MotionVectors:
        return false;
    case MeshPassType::Depth:
    case MeshPassType::BaseOpaque:
    case MeshPassType::GBuffer:
    case MeshPassType::BaseTransparent:
    case MeshPassType::ShadowDepth:
    case MeshPassType::Gizmo:
        return true;
    }
    return true;
}


[[nodiscard]] bool PassDisablesAlphaBlend(MeshPassType pass) noexcept {
    switch (pass) {
    case MeshPassType::Depth:
    case MeshPassType::BaseOpaque:
    case MeshPassType::GBuffer:
    case MeshPassType::ShadowDepth:
    case MeshPassType::MotionVectors:
        return true;
    case MeshPassType::BaseTransparent:
        // The transparent pass IS the alpha-blend pass (MAT-80); it must not skip blended materials.
        return false;
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
    case MeshPassType::Gizmo:
        return false;
    }
    return true;
}

[[nodiscard]] bool PassNeedsPerInstanceCandidateFilter(MeshPassType pass) noexcept {
    switch (pass) {
    case MeshPassType::ShadowDepth:
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
        return true;
    case MeshPassType::Depth:
    case MeshPassType::BaseOpaque:
    case MeshPassType::GBuffer:
    case MeshPassType::BaseTransparent:
    case MeshPassType::MotionVectors:
    case MeshPassType::Gizmo:
        return false;
    }
    return true;
}

[[nodiscard]] std::uint64_t DetailSwitchKey(const SceneRenderMeshInstance& instance) noexcept {
    return instance.detailSwitchGroupId != 0U ? instance.detailSwitchGroupId : (instance.entityId ^ 0x9e3779b97f4a7c15ULL);
}

[[nodiscard]] bool ReversesWinding(const std::array<float, 16>& m) noexcept {
    const double determinant = double(m[0]) * (double(m[5]) * m[10] - double(m[9]) * m[6])
        - double(m[4]) * (double(m[1]) * m[10] - double(m[9]) * m[2])
        + double(m[8]) * (double(m[1]) * m[6] - double(m[5]) * m[2]);
    return determinant < 0.0;
}

struct DetailSwitchCandidate {
    std::uint64_t policyEntityId = UINT64_MAX;
    float coverage = 0.0F;
    std::uint8_t desiredLod = UINT8_MAX;
    std::uint32_t minimumLod = 0U;
    std::uint32_t maximumLod = 255U;
    float promoteCoverage = 0.20F;
    float demoteCoverage = 0.15F;
};

void ResolveDetailSwitchLevels(const MeshPassProcessorDesc& desc, MeshPipelineBuildResult& result) {
    result.detailSwitchLevels.clear();
    const bool advancesHistory = desc.pass == MeshPassType::BaseOpaque || desc.pass == MeshPassType::GBuffer;
    std::unordered_map<std::uint64_t, DetailSwitchCandidate> candidates;
    // A pass with one resolved mesh that has no chain of levels has no instance to choose a level for.
    const bool noLevelChains = desc.resourceValidation != MeshPipelineResourceValidation::ResolveAndValidate &&
        (desc.resolvedMeshResource == nullptr || desc.resolvedMeshResource->lods.size() <= 1U);
    for (const SceneMeshBatch& batch : noLevelChains ? std::span<const SceneMeshBatch>{} : desc.meshBatches) {
        const RenderMeshResource* mesh = nullptr;
        if (desc.resourceValidation == MeshPipelineResourceValidation::ResolveAndValidate) {
            const RenderMeshHandle handle = desc.resourceMap->ResolveMesh(batch.meshAssetId);
            mesh = desc.resources->FindMesh(handle);
        } else {
            mesh = desc.resolvedMeshResource;
        }
        if (mesh == nullptr || mesh->lods.size() <= 1U) continue;
        for (const SceneRenderMeshInstance& instance : batch.instances) {
            if (!instance.detailSwitchEnabled) continue;
            const std::uint64_t key = DetailSwitchKey(instance);
            DetailSwitchCandidate& candidate = candidates[key];
            const std::uint8_t desired = MeshPipelineVisibility::SelectLodLevel(mesh, instance, desc.camera);
            candidate.desiredLod = std::min(candidate.desiredLod, desired);
            const RenderBoundsSphere bounds = MeshPipelineVisibility::TransformBounds(
                instance.boundsOverride.IsValid() ? instance.boundsOverride : mesh->bounds, instance.model);
            candidate.coverage = std::max(candidate.coverage, MeshPipelineVisibility::ScreenCoverage(desc.camera, bounds));
            if (instance.entityId < candidate.policyEntityId) {
                candidate.policyEntityId = instance.entityId;
                candidate.minimumLod = instance.detailSwitchMinimumLod;
                candidate.maximumLod = instance.detailSwitchMaximumLod;
                candidate.promoteCoverage = instance.detailSwitchPromoteCoverage;
                candidate.demoteCoverage = instance.detailSwitchDemoteCoverage;
            }
        }
    }
    if (advancesHistory) {
        std::unordered_set<std::uint64_t> activeKeys;
        activeKeys.reserve(candidates.size());
        for (const auto& [key, candidate] : candidates) {
            static_cast<void>(candidate);
            activeKeys.emplace(key);
        }
        for (auto previous = result.detailSwitchPreviousLevels.begin(); previous != result.detailSwitchPreviousLevels.end();) {
            if (!activeKeys.contains(previous->first)) {
                previous = result.detailSwitchPreviousLevels.erase(previous);
                ++result.detailSwitchHistoryRevision;
            } else {
                ++previous;
            }
        }
    }
    for (const auto& [key, candidate] : candidates) {
        const std::uint8_t requested = static_cast<std::uint8_t>(std::clamp<std::uint32_t>(candidate.desiredLod, candidate.minimumLod, candidate.maximumLod));
        std::uint8_t resolved = requested;
        if (const auto previous = result.detailSwitchPreviousLevels.find(key); previous != result.detailSwitchPreviousLevels.end()) {
            resolved = previous->second;
            if (advancesHistory) {
                if (requested < resolved && candidate.coverage >= candidate.promoteCoverage) resolved = requested;
                if (requested > resolved && candidate.coverage < candidate.demoteCoverage) resolved = requested;
            }
        }
        result.detailSwitchLevels.emplace(key, resolved);
        if (advancesHistory) {
            const auto previous = result.detailSwitchPreviousLevels.find(key);
            if (previous == result.detailSwitchPreviousLevels.end() || previous->second != resolved) {
                result.detailSwitchPreviousLevels.insert_or_assign(key, resolved);
                ++result.detailSwitchHistoryRevision;
            }
        }
    }
}

constexpr std::uint8_t kLevelNotChosen = UINT8_MAX;

// A pass is split into at most this many chunks, none of fewer instances than the minimum: chunks small enough that the
// calling thread can start writing the first while the rest are scanned, and large enough to be worth taking one by one.
constexpr std::size_t kMaxChunks = 32U;
constexpr std::size_t kMinChunkInstances = 256U;

// A pass of fewer chunks is built on the calling thread: it has too little to scan side by side.
constexpr std::size_t kMinChunks = 4U;

// Passes built together are written side by side once their instances add up to this many; fewer take less to build one
// after the other than to hand to the workers.
constexpr std::size_t kMinSideBySideInstances = 512U;

// Passes built together are planned side by side once they have this many batches; fewer take less to plan one after the
// other than to hand to the workers.
constexpr std::size_t kMinSideBySidePlanBatches = 1024U;

// How many admitted instances ahead the merge of a chunk fetches the instance it copies into a draw command.
constexpr std::uint32_t kMergePrefetchDistance = 4U;

// The workers that scan a pass beside the calling thread. Scanning costs a few times what writing the draw commands does,
// so a few workers keep ahead of the calling thread, and every worker woken costs it time.
constexpr std::size_t kMaxScanWorkers = 8U;

// What every thread scanning batches reads and none of them changes.
struct MeshScanContext {
    const MeshPassProcessorDesc& desc;
    const MeshPipelineFrustum& frustum;
    std::uint32_t cullingMask;
    const std::unordered_map<std::uint64_t, std::uint8_t>& detailSwitchLevels;
};

// What one thread scanning batches writes. Threads own their state, so no two of them share any.
struct MeshScanState {
    SceneRenderSubmitStats& stats;
    SceneRenderDiagnostics* diagnostics;
    std::unordered_map<std::uint64_t, MeshPipelineMaterialResolution>& materialResolutions;
    std::vector<std::unordered_map<std::uint64_t, MeshPipelineMaterialResolution>::node_type>& materialResolutionNodes;
    // The level each instance of the batch being scanned draws; empty for a mesh without a chain of levels.
    std::vector<std::uint8_t>& chainLevels;
    // The instances of the batch section being scanned that its visibility index leaves to the scan, one bit each.
    std::vector<std::uint64_t>& selectionBits;
    std::uint32_t culledInstanceCount = 0U;
    // Stands for the instances of one kind (castsShadow, shadowOnly) when a pass tells what it makes of them.
    SceneRenderMeshInstance kindProbe{};
    // What the pass makes of an instance of each kind, which it makes the same of every batch, told once: whether it can
    // contain it, and, for the material and terrain layer the last scanned section had, whether only the frustum can still
    // turn it away. Negative while not yet told.
    std::array<std::int8_t, 4> candidateKinds{ -1, -1, -1, -1 };
    struct SectionKinds {
        const RenderMaterialResource* material = nullptr;
        bool terrainLayerActive = false;
        std::uint8_t terrainLayerIndex = 0U;
        std::array<std::int8_t, 4> verdicts{ -1, -1, -1, -1 };
    } sectionKinds;
};

// What the scan of one section of a batch keeps the same for every instance.
struct MeshSectionScope {
    RenderMeshHandle mesh{};
    const RenderMeshResource* meshResource = nullptr;
    RenderMeshSection section{};
    std::uint32_t sectionIndex = 0U;
    std::uint32_t sectionVertexCount = 0U;
    std::pair<std::uint32_t, std::uint32_t> meshletRange{};
    bool gpuFrustumCull = false;
    bool gpuDrivenCandidate = false;
};

// The visibility index of a batch, when it covers the instances of the batch.
[[nodiscard]] const SceneMeshVisibilityIndex* UsableIndex(const SceneMeshBatch& batch) noexcept {
    const SceneMeshVisibilityIndex* index = batch.visibility;
    return index != nullptr && !index->Empty() && index->slots.size() == batch.instances.size() ? index : nullptr;
}

// Makes the probe an instance of the given kind (see SceneMeshVisibilityBounds::kindCount) in every layer.
void SetProbeKind(SceneRenderMeshInstance& probe, std::uint32_t kind) noexcept {
    probe.castsShadow = (kind & 1U) != 0U;
    probe.shadowOnly = (kind & 2U) != 0U;
    probe.layer = 0xFFFFFFFFU;
}

// The candidates of a batch counted from the kinds of its instances, when its layers say which of them the pass takes.
[[nodiscard]] std::optional<std::uint32_t> CountIndexedCandidates(const MeshScanContext& context, MeshScanState& state,
    const SceneMeshBatch& batch) noexcept {
    const SceneMeshVisibilityIndex* index = UsableIndex(batch);
    const MeshPassType pass = context.desc.pass;
    if (index == nullptr || pass == MeshPassType::SelectionId || pass == MeshPassType::EditorSelection) {
        return std::nullopt;
    }
    const SceneMeshVisibilityBounds& root = index->root;
    if ((root.layersAll & context.cullingMask) == 0U) {
        return (root.layersAny & context.cullingMask) == 0U ? std::optional<std::uint32_t>{ 0U } : std::nullopt;
    }
    std::uint32_t count = 0U;
    for (std::uint32_t kind = 0U; kind < root.kindCount.size(); ++kind) {
        if (root.kindCount[kind] == 0U) {
            continue;
        }
        std::int8_t& candidate = state.candidateKinds[kind];
        if (candidate < 0) {
            SetProbeKind(state.kindProbe, kind);
            candidate = MeshPipelinePassPolicy::CanEverContain(pass, state.kindProbe, context.desc.selectedEntityIds, context.cullingMask) ? 1 : 0;
        }
        if (candidate != 0) {
            count += root.kindCount[kind];
        }
    }
    return count;
}

[[nodiscard]] std::uint32_t CountCandidates(const MeshScanContext& context, MeshScanState& state, const SceneMeshBatch& batch) noexcept {
    const bool wholeBatchIsCandidate = context.cullingMask == 0xFFFFFFFFU && !PassNeedsPerInstanceCandidateFilter(context.desc.pass);
    if (wholeBatchIsCandidate) {
        return static_cast<std::uint32_t>(batch.instances.size());
    }
    if (const std::optional<std::uint32_t> counted = CountIndexedCandidates(context, state, batch)) {
        return *counted;
    }
    return MeshPipelinePassPolicy::CountCandidateInstances(context.desc.pass, batch, context.desc.selectedEntityIds, context.cullingMask);
}

// Resolves the mesh of a batch that has candidate instances. A batch that is not drawn builds nothing: its mesh is
// missing, which is counted in `stats` and reported to `diagnostics`, or the pass leaves it out.
[[nodiscard]] MeshPipelineScanBatch ResolveBatchMesh(const MeshScanContext& context, const SceneMeshBatch& batch,
    std::uint32_t candidateCount, SceneRenderSubmitStats& stats, SceneRenderDiagnostics* diagnostics) noexcept {
    const MeshPassProcessorDesc& desc = context.desc;
    MeshPipelineScanBatch resolved;
    if (desc.resourceValidation == MeshPipelineResourceValidation::ResolveAndValidate) {
        const MeshPipelineResolvedMesh resolvedMesh = MeshPipelineResourceResolver::ResolveMeshBatch(
            desc.pass,
            batch,
            candidateCount,
            *desc.resources,
            *desc.resourceMap,
            stats,
            diagnostics,
            desc.selectedEntityIds,
            context.cullingMask);
        resolved.mesh = resolvedMesh.handle;
        resolved.meshResource = resolvedMesh.resource;
        if (resolved.meshResource == nullptr) {
            return resolved;
        }
    } else {
        resolved.meshResource = desc.resolvedMeshResource;
    }
    resolved.drawn = !(desc.terrainLayersOnly &&
        (desc.pass != MeshPassType::BaseTransparent || resolved.meshResource == nullptr || resolved.meshResource->terrainLayerCount <= 1U));
    return resolved;
}

[[nodiscard]] std::uint32_t SectionCountOf(const RenderMeshResource* mesh) noexcept {
    return mesh == nullptr || mesh->sections.empty() ? 1U : static_cast<std::uint32_t>(mesh->sections.size());
}

[[nodiscard]] MeshSectionScope MakeSectionScope(const MeshPassProcessorDesc& desc, const MeshPipelineScanBatch& resolved,
    std::uint32_t sectionIndex) noexcept {
    const RenderMeshResource* mesh = resolved.meshResource;
    MeshSectionScope scope;
    scope.mesh = resolved.mesh;
    scope.meshResource = mesh;
    scope.sectionIndex = sectionIndex;
    scope.section = mesh == nullptr || mesh->sections.empty()
        ? RenderMeshSection{
              .indexStart = 0U,
              .indexCount = mesh == nullptr ? 0U : mesh->indexCount,
              .vertexStart = 0U,
              .vertexCount = mesh == nullptr ? 0U : mesh->vertexCount,
              .materialSlot = 0U,
              .bounds = mesh == nullptr ? RenderBoundsSphere{} : mesh->bounds,
          }
        : mesh->sections[sectionIndex];
    scope.sectionVertexCount = scope.section.vertexCount != 0U
        ? scope.section.vertexCount
        : (mesh != nullptr && scope.section.vertexStart < mesh->vertexCount ? mesh->vertexCount - scope.section.vertexStart : 0U);
    scope.meshletRange = MeshPipelineVisibility::MeshletRangeForSection(mesh, sectionIndex);
    scope.gpuFrustumCull = mesh != nullptr &&
        (mesh->gpuCullingEnabled || mesh->indirectDrawsEnabled) &&
        desc.gpuDrivenSupport.computeCullingSupported && desc.gpuDrivenSupport.indirectDrawSupported &&
        desc.gpuDrivenSupport.runtimeGpuDispatchSupported &&
        desc.maxDrawCommands == 0U && desc.maxVisibleInstances == 0U &&
        (desc.pass == MeshPassType::BaseOpaque || desc.pass == MeshPassType::GBuffer ||
         desc.pass == MeshPassType::Depth || desc.pass == MeshPassType::ShadowDepth);
    scope.gpuDrivenCandidate = MeshPipelineGpuDrivenRecorder::IsCandidate(mesh);
    return scope;
}

[[nodiscard]] bool SectionIsDrawn(const MeshPassProcessorDesc& desc, const RenderMeshSection& section) noexcept {
    return !(desc.terrainLayersOnly && (section.terrainLayerIndex == UINT8_MAX || section.terrainLayerIndex == 0U));
}

// Which instances of a batch section the scan visits: every one, or those whose bit is set.
struct InstanceSelection {
    const std::vector<std::uint64_t>* bits = nullptr;

    [[nodiscard]] std::size_t First(std::size_t count) const noexcept {
        return bits == nullptr ? 0U : NextSet(0U, count);
    }
    [[nodiscard]] std::size_t After(std::size_t instanceIndex, std::size_t count) const noexcept {
        return bits == nullptr ? instanceIndex + 1U : NextSet(instanceIndex + 1U, count);
    }

private:
    [[nodiscard]] std::size_t NextSet(std::size_t from, std::size_t count) const noexcept {
        std::size_t word = from >> 6U;
        if (word >= bits->size()) {
            return count;
        }
        std::uint64_t value = (*bits)[word] & (~std::uint64_t{ 0U } << (from & 63U));
        while (value == 0U) {
            if (++word >= bits->size()) {
                return count;
            }
            value = (*bits)[word];
        }
        return (word << 6U) + static_cast<std::size_t>(std::countr_zero(value));
    }
};

// Fetches the memory of an instance that will be read soon: instances far apart are not read ahead of time.
void PrefetchInstance(const SceneRenderMeshInstance& instance) noexcept {
    const char* bytes = reinterpret_cast<const char*>(&instance);
    for (std::size_t offset = 0U; offset < sizeof(SceneRenderMeshInstance); offset += 64U) {
        kb::ecs::PrefetchQueryMemory(bytes + offset);
    }
}

// A pass makes use of the visibility index only where it admits instances by what the index counts and by the frustum:
// a pass that admits them by entity, or by whether they moved, reads more of each.
[[nodiscard]] bool PassUsesVisibilityIndex(const MeshPassProcessorDesc& desc) noexcept {
    switch (desc.pass) {
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
        return false;
    case MeshPassType::MotionVectors:
        return desc.motionFrame == 0U;
    case MeshPassType::Depth:
    case MeshPassType::BaseOpaque:
    case MeshPassType::GBuffer:
    case MeshPassType::BaseTransparent:
    case MeshPassType::ShadowDepth:
    case MeshPassType::Gizmo:
        return true;
    }
    return false;
}

// What a scan of one batch section holds the same for every instance, which lets the visibility index rule instances out.
struct IndexedSectionPlan {
    const RenderMaterialResource* material = nullptr;
    float margin = 0.0F;
    // How far from its origin the bounds of an instance without a bounds override of its own reach, per unit of its scale.
    float localReach = 0.0F;
    // Whether the scan counts the LOD selection of every instance.
    bool countsLods = false;
    // Whether the pass leaves out the material's instances for being blended.
    bool blendSkipped = false;
    // What the scan makes of an instance of each kind: bit 0 when it takes it as a candidate, bit 1 when nothing but the
    // frustum can still turn it away; negative while not yet told.
    std::array<std::int8_t, 4>* verdicts = nullptr;
};

// A bounds radius below this could scale to nothing in the arithmetic of a scan, which then never culls it.
constexpr float kMinimumIndexedRadius = 1.0e-6F;

// Whether the scan of a batch section is the same for every instance up to what the instances themselves tell (their
// flags, their layers, their place), which is when the visibility index may rule instances out, and tells what it holds the
// same. A section whose resources are not found cleanly is left to the scan, which reports what is missing instance by instance.
[[nodiscard]] bool PlanIndexedSection(const MeshScanContext& context, MeshScanState& state, const SceneMeshBatch& batch,
    const MeshSectionScope& scope, IndexedSectionPlan& plan) {
    const MeshPassProcessorDesc& desc = context.desc;
    const RenderMeshResource* mesh = scope.meshResource;
    if (!PassUsesVisibilityIndex(desc) || !context.frustum.valid || scope.gpuFrustumCull || mesh == nullptr ||
        mesh->lods.size() > 1U || scope.section.lodLevel != 0U || batch.hasMaterialSlotOverrides) {
        return false;
    }
    const RenderBoundsSphere& local = scope.section.bounds.IsValid() ? scope.section.bounds : mesh->bounds;
    if (!(local.radius >= kMinimumIndexedRadius)) {
        return false;
    }
    const RenderMaterialResource* material = desc.resolvedMaterialResource;
    if (desc.resourceValidation == MeshPipelineResourceValidation::ResolveAndValidate) {
        const SceneRenderMeshInstance& first = batch.instances.front();
        const std::uint64_t materialAssetId = MeshPipelineResourceResolver::MaterialAssetForSectionInstance(batch, first, mesh, scope.section);
        material = nullptr;
        if (const auto cached = state.materialResolutions.find(materialAssetId); cached != state.materialResolutions.end()) {
            material = cached->second.resource;
        } else {
            SceneRenderSubmitStats found;
            RenderMaterialHandle handle{};
            material = MeshPipelineResourceResolver::ResolveMaterialOrFallback(first, materialAssetId, *desc.resources, *desc.resourceMap, handle, found, nullptr);
            MeshPipelineResourceResolver::ValidateMaterialTextureOrFallback(first, materialAssetId, material, *desc.resources, *desc.resourceMap, found, nullptr);
            if (found.HasMissingResources() || (materialAssetId != 0U && (!handle.IsValid() || material == nullptr))) {
                return false;
            }
            if (handle.IsValid() && material != nullptr) {
                EmplaceKeptNode(state.materialResolutions, state.materialResolutionNodes, materialAssetId, MeshPipelineMaterialResolution{
                    .handle = handle,
                    .resource = material,
                });
            }
        }
    }
    plan.material = material;
    MeshScanState::SectionKinds& kinds = state.sectionKinds;
    if (kinds.material != material || kinds.terrainLayerActive != scope.section.terrainLayerActive ||
        kinds.terrainLayerIndex != scope.section.terrainLayerIndex) {
        kinds = MeshScanState::SectionKinds{ .material = material, .terrainLayerActive = scope.section.terrainLayerActive,
            .terrainLayerIndex = scope.section.terrainLayerIndex };
    }
    plan.verdicts = &kinds.verdicts;
    plan.margin = MeshPipelineVisibility::MaterialBoundsMargin(material);
    plan.localReach = local.radius + std::sqrt(local.center[0] * local.center[0] + local.center[1] * local.center[1] + local.center[2] * local.center[2]);
    plan.countsLods = !mesh->lods.empty();
    plan.blendSkipped = PassDisablesAlphaBlend(desc.pass) && MeshPipelinePassPolicy::UsesDisabledAlphaBlend(material);
    return true;
}

// What the scan makes of an instance of the kind, in a layer the pass takes.
[[nodiscard]] std::int8_t KindVerdict(const MeshScanContext& context, MeshScanState& state, const MeshSectionScope& scope,
    IndexedSectionPlan& plan, std::uint32_t kind) noexcept {
    std::int8_t& verdict = (*plan.verdicts)[kind];
    if (verdict < 0) {
        const MeshPassProcessorDesc& desc = context.desc;
        SetProbeKind(state.kindProbe, kind);
        const bool candidate = MeshPipelinePassPolicy::CanEverContain(desc.pass, state.kindProbe, desc.selectedEntityIds, context.cullingMask);
        const bool reachesFrustum = candidate && !plan.blendSkipped &&
            MeshPipelinePassPolicy::Accepts(desc.pass, state.kindProbe, plan.material, desc.selectedEntityIds, context.cullingMask, &scope.section);
        verdict = static_cast<std::int8_t>((candidate ? 1 : 0) | (reachesFrustum ? 2 : 0));
    }
    return verdict;
}

// The members the frustum turns away that no cluster or batch holds, by kind: what a pass counts of them in the end.
using RuledOutByKind = std::array<std::uint32_t, 4>;

// Adds the members of a set that the frustum turns away. False, and nothing added, when the layers of the members do not tell
// which of them the pass takes.
[[nodiscard]] bool TallyRuledOut(std::uint32_t cullingMask, const SceneMeshVisibilityBounds& bounds, RuledOutByKind& ruledOut) noexcept {
    if ((bounds.layersAll & cullingMask) == 0U) {
        // None of them is a candidate when none has a layer the pass takes.
        return (bounds.layersAny & cullingMask) == 0U;
    }
    for (std::size_t kind = 0U; kind < ruledOut.size(); ++kind) {
        ruledOut[kind] += bounds.kindCount[kind];
    }
    return true;
}

// Counts the instances the frustum turned away as scanning each of them would have counted it.
void CountRuledOut(const MeshScanContext& context, MeshScanState& state, const MeshSectionScope& scope,
    IndexedSectionPlan& plan, const RuledOutByKind& ruledOut) noexcept {
    for (std::uint32_t kind = 0U; kind < ruledOut.size(); ++kind) {
        if (ruledOut[kind] == 0U) {
            continue;
        }
        const std::int8_t verdict = KindVerdict(context, state, scope, plan, kind);
        if (plan.countsLods && (verdict & 1) != 0) {
            state.stats.lodSelectionCount += ruledOut[kind];
        }
        if ((verdict & 2) != 0) {
            state.culledInstanceCount += ruledOut[kind];
        }
    }
}

// Rules out the instances of a batch section that lie in clusters of its visibility index outside the frustum, and counts
// them as scanning each would have. In a cluster the frustum cuts across, each instance is told by its entry, which is
// quicker than reading the instance. The instances that are left are scanned as ever.
void SelectInstances(const MeshScanContext& context, MeshScanState& state, const SceneMeshBatch& batch,
    const MeshSectionScope& scope, InstanceSelection& selection) {
    const SceneMeshVisibilityIndex* index = UsableIndex(batch);
    IndexedSectionPlan plan;
    if (index == nullptr || !PlanIndexedSection(context, state, batch, scope, plan)) {
        return;
    }
    const auto reachOf = [&plan](float maximumScale, float overrideReach) noexcept {
        return std::max(maximumScale * plan.localReach, overrideReach) + plan.margin;
    };
    const SceneMeshVisibilityBounds& root = index->root;
    const MeshPipelineBoundsVisibility rootVisibility = MeshPipelineVisibility::ClassifyBounds(
        context.frustum, root, reachOf(root.maximumScale, root.overrideReach));
    if (rootVisibility == MeshPipelineBoundsVisibility::Inside) {
        return;
    }
    std::vector<std::uint64_t>& bits = state.selectionBits;
    selection.bits = &bits;
    RuledOutByKind ruledOut{};
    if (rootVisibility == MeshPipelineBoundsVisibility::Outside && TallyRuledOut(context.cullingMask, root, ruledOut)) {
        bits.clear();
        CountRuledOut(context, state, scope, plan, ruledOut);
        return;
    }
    bits.assign((batch.instances.size() + 63U) / 64U, 0U);
    for (std::size_t clusterIndex = 0U; clusterIndex < index->clusters.size(); ++clusterIndex) {
        const SceneMeshVisibilityIndex::Cluster& cluster = index->clusters[clusterIndex];
        const MeshPipelineBoundsVisibility visibility = MeshPipelineVisibility::ClassifyBounds(
            context.frustum, cluster, reachOf(cluster.maximumScale, cluster.overrideReach));
        if (visibility == MeshPipelineBoundsVisibility::Outside && TallyRuledOut(context.cullingMask, cluster, ruledOut)) {
            continue;
        }
        const SceneMeshVisibilityIndex::Entry* members = index->entries.data() + clusterIndex * SceneMeshVisibilityIndex::kClusterCapacity;
        for (std::uint32_t member = 0U; member < cluster.count; ++member) {
            const SceneMeshVisibilityIndex::Entry& entry = members[member];
            if (visibility != MeshPipelineBoundsVisibility::Inside && !entry.unbounded &&
                MeshPipelineVisibility::IsReachOutsideFrustum(context.frustum, entry.origin, reachOf(entry.scale, entry.overrideReach))) {
                if ((entry.layer & context.cullingMask) != 0U) {
                    ++ruledOut[entry.kind];
                }
                continue;
            }
            bits[entry.instance >> 6U] |= std::uint64_t{ 1U } << (entry.instance & 63U);
        }
    }
    CountRuledOut(context, state, scope, plan, ruledOut);
}

// Scans the instances of a batch for one section and hands each one the pass draws to `sink`, with the draw command it
// joins. Nothing is written but the state of the thread, so batches can be scanned on several threads.
template <typename Sink>
void ScanSection(const MeshScanContext& context, MeshScanState& state, const SceneMeshBatch& batch,
    const MeshSectionScope& scope, Sink&& sink) {
    const MeshPassProcessorDesc& desc = context.desc;
    const bool validateResources = desc.resourceValidation == MeshPipelineResourceValidation::ResolveAndValidate;
    const RenderMeshResource* meshResource = scope.meshResource;
    const RenderMeshSection& section = scope.section;
    std::uint64_t lastMaterialAssetId = 0U;
    MeshPipelineMaterialResolution lastMaterialResolution{};
    std::size_t clusterIndex = 0U;
    InstanceSelection selection;
    if (batch.visibility != nullptr) {
        SelectInstances(context, state, batch, scope, selection);
    }
    const std::size_t instanceCount = batch.instances.size();
    for (std::size_t instanceIndex = selection.First(instanceCount); instanceIndex < instanceCount;
         instanceIndex = selection.After(instanceIndex, instanceCount)) {
        if (selection.bits != nullptr) {
            // The instances that are left may lie far apart: the next is fetched while this one is scanned.
            const std::size_t ahead = selection.After(instanceIndex, instanceCount);
            if (ahead < instanceCount && ahead != instanceIndex + 1U) PrefetchInstance(batch.instances[ahead]);
        }
        const SceneRenderMeshInstance& instance = batch.instances[instanceIndex];
        if (!MeshPipelinePassPolicy::CanEverContain(desc.pass, instance, desc.selectedEntityIds, context.cullingMask)) {
            continue;
        }
        // A mesh with a chain of levels is scanned once per section of every level; the level an instance
        // draws does not depend on the section, so it is chosen the first time and kept for the batch.
        std::uint8_t selectedLod = state.chainLevels.empty() ? std::uint8_t{ 0U } : state.chainLevels[instanceIndex];
        if (selectedLod == kLevelNotChosen || state.chainLevels.empty()) {
            selectedLod = MeshPipelineVisibility::SelectLodLevel(meshResource, instance, desc.camera);
            if (instance.detailSwitchEnabled) {
                if (const auto selected = context.detailSwitchLevels.find(DetailSwitchKey(instance)); selected != context.detailSwitchLevels.end()) {
                    selectedLod = static_cast<std::uint8_t>(std::min<std::uint32_t>(selected->second, meshResource == nullptr || meshResource->lods.empty() ? 0U : static_cast<std::uint32_t>(meshResource->lods.size() - 1U)));
                }
            }
            if (!state.chainLevels.empty()) state.chainLevels[instanceIndex] = selectedLod;
        }
        if (meshResource != nullptr && !meshResource->lods.empty()) {
            ++state.stats.lodSelectionCount;
        }
        if (selectedLod != section.lodLevel) {
            continue;
        }
        const std::uint64_t materialAssetId = MeshPipelineResourceResolver::MaterialAssetForSectionInstance(batch, instance, meshResource, section);
        RenderMaterialHandle materialHandle{};
        const RenderMaterialResource* materialResource = validateResources ? nullptr : desc.resolvedMaterialResource;
        if (validateResources) {
            if (materialAssetId == lastMaterialAssetId && lastMaterialResolution.resource != nullptr) {
                materialHandle = lastMaterialResolution.handle;
                materialResource = lastMaterialResolution.resource;
            } else {
                const auto cached = state.materialResolutions.find(materialAssetId);
                if (cached != state.materialResolutions.end()) {
                    materialHandle = cached->second.handle;
                    materialResource = cached->second.resource;
                } else {
                    materialResource = MeshPipelineResourceResolver::ResolveMaterialOrFallback(instance, materialAssetId, *desc.resources, *desc.resourceMap, materialHandle, state.stats, state.diagnostics);
                    MeshPipelineResourceResolver::ValidateMaterialTextureOrFallback(instance, materialAssetId, materialResource, *desc.resources, *desc.resourceMap, state.stats, state.diagnostics);
                    if (materialHandle.IsValid() && materialResource != nullptr) {
                        EmplaceKeptNode(state.materialResolutions, state.materialResolutionNodes, materialAssetId, MeshPipelineMaterialResolution{
                            .handle = materialHandle,
                            .resource = materialResource,
                        });
                    }
                }
                lastMaterialAssetId = materialAssetId;
                lastMaterialResolution = MeshPipelineMaterialResolution{
                    .handle = materialHandle,
                    .resource = materialHandle.IsValid() ? materialResource : nullptr,
                };
            }
        }
        if (PassDisablesAlphaBlend(desc.pass) && MeshPipelinePassPolicy::UsesDisabledAlphaBlend(materialResource)) {
            // Blended materials are skipped from opaque/depth/shadow and render in the transparent
            // pass instead (MAT-80); this is correct routing, not an unsupported-material error.
            continue;
        }
        if (!MeshPipelinePassPolicy::Accepts(desc.pass, instance, materialResource, desc.selectedEntityIds, context.cullingMask, &section)) {
            continue;
        }
        if (desc.pass == MeshPassType::MotionVectors && desc.motionFrame != 0U &&
            !MeshPipelinePassPolicy::HasOwnMotion(instance, materialResource, desc.motionFrame)) {
            continue;
        }
        // The material's World Position Offset reach (Max World Position Offset Displacement).
        const float boundsMargin = MeshPipelineVisibility::MaterialBoundsMargin(materialResource);
        const RenderBoundsSphere localBounds = instance.boundsOverride.IsValid()
            ? instance.boundsOverride
            : (section.bounds.IsValid() ? section.bounds : (meshResource == nullptr ? RenderBoundsSphere{} : meshResource->bounds));
        while (clusterIndex < batch.visibilityClusters.size() &&
            batch.visibilityClusters[clusterIndex].firstInstance < instanceIndex) ++clusterIndex;
        if (!scope.gpuFrustumCull && clusterIndex < batch.visibilityClusters.size() &&
            batch.visibilityClusters[clusterIndex].firstInstance == instanceIndex &&
            localBounds.IsValid() && meshResource != nullptr && meshResource->lods.size() <= 1U &&
            !instance.detailSwitchEnabled && !batch.hasMaterialSlotOverrides &&
            desc.pass != MeshPassType::SelectionId && desc.pass != MeshPassType::EditorSelection &&
            !state.stats.HasMissingResources()) {
            const auto& cluster = batch.visibilityClusters[clusterIndex];
            auto bounds = cluster.origins;
            const auto& center = localBounds.center;
            bounds.radius += cluster.maximumScale * (localBounds.radius +
                std::sqrt(center[0]*center[0] + center[1]*center[1] + center[2]*center[2])) + boundsMargin;
            if (cluster.instanceCount != 0U && cluster.instanceCount <= batch.instances.size()-instanceIndex &&
                !MeshPipelineVisibility::IsInsideFrustum(context.frustum, bounds)) {
                state.culledInstanceCount += cluster.instanceCount;
                if (!meshResource->lods.empty()) state.stats.lodSelectionCount += cluster.instanceCount - 1U;
                instanceIndex += cluster.instanceCount - 1U;
                continue;
            }
        }
        const RenderBoundsSphere worldBounds = MeshPipelineVisibility::GrowBounds(
            MeshPipelineVisibility::TransformBounds(localBounds, instance.model), boundsMargin);
        if (!scope.gpuFrustumCull && !MeshPipelineVisibility::IsInsideFrustum(context.frustum, worldBounds)) {
            ++state.culledInstanceCount;
            continue;
        }
        if (desc.pass != MeshPassType::ShadowDepth && desc.pass != MeshPassType::Gizmo &&
            MeshPipelineVisibility::IsOccludedByVisibilityBlockers(desc.camera, worldBounds, desc.visibilityBlockers)) {
            ++state.culledInstanceCount;
            continue;
        }
        if (desc.portalVisibility != nullptr && desc.pass != MeshPassType::ShadowDepth && desc.pass != MeshPassType::Gizmo &&
            desc.portalVisibility->Hides(kb::math::Vec3{ worldBounds.center[0], worldBounds.center[1], worldBounds.center[2] }, instance.layer)) {
            ++state.culledInstanceCount;
            continue;
        }
        const float viewDepth = MeshPipelineVisibility::ViewDepth(desc.camera, worldBounds);
        sink(MeshPipelineScanTarget{
                 .key = MeshCommandLookupKey{
                     .meshAssetId = batch.meshAssetId, .sectionIndex = scope.sectionIndex,
                     .materialAssetId = materialAssetId,
                     .materialHandleValue = materialHandle.value,
                     .currentSkinningPalette = instance.currentSkinningPalette,
                     .previousSkinningPalette = instance.previousSkinningPalette,
                     .reversedWinding = !(meshResource != nullptr && meshResource->doubleSided) &&
                         !(materialResource != nullptr && materialResource->doubleSided) && ReversesWinding(instance.model),
                 },
                 .material = materialHandle,
                 .materialResource = materialResource,
             },
             MeshPipelineScanInstance{
                 .instanceIndex = static_cast<std::uint32_t>(instanceIndex),
                 .lodLevel = selectedLod,
                 .depthBucket = MeshPipelineVisibility::DepthBucket(viewDepth),
                 .viewDepth = viewDepth,
                 .worldBounds = worldBounds,
             });
    }
}

// Scans every section of a batch whose mesh resolved. `beginSection` runs before each section that is scanned and
// `sink` takes each admitted instance with the scope of its section.
template <typename BeginSection, typename Sink>
void ScanBatch(const MeshScanContext& context, MeshScanState& state, const SceneMeshBatch& batch,
    const MeshPipelineScanBatch& resolved, BeginSection&& beginSection, Sink&& sink) {
    if (resolved.meshResource != nullptr && resolved.meshResource->lods.size() > 1U) state.chainLevels.assign(batch.instances.size(), kLevelNotChosen);
    else state.chainLevels.clear();
    const std::uint32_t sectionCount = SectionCountOf(resolved.meshResource);
    for (std::uint32_t sectionIndex = 0U; sectionIndex < sectionCount; ++sectionIndex) {
        const MeshSectionScope scope = MakeSectionScope(context.desc, resolved, sectionIndex);
        if (!SectionIsDrawn(context.desc, scope.section)) {
            continue;
        }
        beginSection();
        ScanSection(context, state, batch, scope, [&](const MeshPipelineScanTarget& target, const MeshPipelineScanInstance& admitted) {
            sink(scope, target, admitted);
        });
    }
}

// Writes admitted instances into the draw commands, in the order the scan admitted them.
struct MeshCommandWriter {
    const MeshPassProcessorDesc& desc;
    MeshPipelineBuildResult& result;
    std::size_t commandCount = 0U;
    std::uint32_t acceptedInstanceCount = 0U;
    std::optional<MeshCommandLookupKey> lastCommandKey{};
    std::uint32_t lastCommandIndex = 0U;

    // Creates the draw command that the first instance joining it takes.
    MeshDrawCommand& CreateCommand(const MeshSectionScope& scope, const SceneRenderMeshInstance& instance,
        const MeshPipelineScanTarget& target, std::uint32_t& drawCommandIndex) {
        const MeshCommandLookupKey& commandKey = target.key;
        std::uint64_t commandState = MeshPipelinePassPolicy::State(desc.pass, scope.meshResource, target.materialResource, &scope.section);
        if (commandKey.reversedWinding) {
            const auto cull = commandState & BGFX_STATE_CULL_MASK;
            if (cull == BGFX_STATE_CULL_CCW || cull == BGFX_STATE_CULL_CW) {
                commandState = (commandState & ~BGFX_STATE_CULL_MASK) |
                    (cull == BGFX_STATE_CULL_CCW ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW);
            }
        }
        const std::uint64_t materialTextureDependencySignature = PassUsesMaterialTextureDependencies(desc.pass)
            ? SceneMaterialTextureDependencySignature::Build(SceneMaterialTextureDependencyDesc{
                  .material = target.materialResource,
                  .resources = desc.resources,
                  .resourceMap = desc.resourceMap,
              })
            : 0U;
        const SceneCachedDrawCommandDesc cachedCommandDesc{
            .pass = desc.pass,
            .meshAssetId = commandKey.meshAssetId,
            .materialAssetId = commandKey.materialAssetId,
            .sectionIndex = commandKey.sectionIndex,
            .materialSlot = scope.section.materialSlot,
            .firstMeshlet = scope.meshletRange.first,
            .meshletCount = scope.meshletRange.second,
            .indexStart = scope.section.indexStart,
            .indexCount = scope.section.indexCount,
            .vertexStart = scope.section.vertexStart,
            .vertexCount = scope.sectionVertexCount,
            .lodLevel = scope.section.lodLevel,
            .terrainLayerIndex = scope.section.terrainLayerIndex,
            .mesh = scope.mesh,
            .material = target.material,
            .meshResource = scope.meshResource,
            .materialResource = target.materialResource,
            .meshResourceVersion = scope.meshResource == nullptr ? 0U : scope.meshResource->version,
            .materialResourceVersion = target.materialResource == nullptr ? 0U : target.materialResource->version,
            .materialTextureDependencySignature = materialTextureDependencySignature,
            .state = commandState,
        };
        const SceneCachedDrawCommand& cachedCommand =
            SceneDrawCommandCache::Resolve(result.drawCommandCache, cachedCommandDesc, result.stats);
        MeshDrawCommand* command = &MeshPipelineCommandBuilder::WritableCommand(result, commandCount);
        SceneCachedDrawCommandMaterializer::ApplyTemplate(cachedCommand, *command);
        command->currentSkinningPalette = instance.currentSkinningPalette;
        command->previousSkinningPalette = instance.previousSkinningPalette;
        EmplaceKeptNode(result.commandLookupScratch, result.commandLookupNodes, commandKey, commandCount);
        result.stats.meshCommandLookupCapacity = std::max<std::uint32_t>(
            result.stats.meshCommandLookupCapacity,
            static_cast<std::uint32_t>(result.commandLookupScratch.bucket_count()));
        drawCommandIndex = static_cast<std::uint32_t>(commandCount);
        ++commandCount;
        return *command;
    }

    // The draw command an admitted instance joins, created for the first instance that takes it. Null when a budget
    // drops the instance, which is then recorded.
    MeshDrawCommand* CommandFor(const MeshSectionScope& scope, const SceneRenderMeshInstance& instance,
        const MeshPipelineScanTarget& target, const MeshPipelineScanInstance& admitted, std::uint32_t& drawCommandIndex) {
        const MeshCommandLookupKey& commandKey = target.key;
        drawCommandIndex = static_cast<std::uint32_t>(commandCount);
        const bool joinsLastCommand = lastCommandKey && *lastCommandKey == commandKey;
        if (joinsLastCommand) {
            drawCommandIndex = lastCommandIndex;
        } else if (const auto found = result.commandLookupScratch.find(commandKey); found != result.commandLookupScratch.end()) {
            drawCommandIndex = static_cast<std::uint32_t>(found->second);
        }
        MeshDrawCommand* command = drawCommandIndex < commandCount ? &result.commands[drawCommandIndex] : nullptr;
        if ((command == nullptr && desc.maxDrawCommands != 0U && commandCount >= desc.maxDrawCommands) ||
            (desc.maxVisibleInstances != 0U && acceptedInstanceCount >= desc.maxVisibleInstances)) {
            if (scope.gpuDrivenCandidate) {
                MeshPipelineGpuDrivenRecorder::Record(result, instance.entityId, admitted.worldBounds, UINT32_MAX, admitted.lodLevel, scope.meshletRange, true);
            }
            ++result.stats.droppedInstanceCount;
            EmitInstanceDiagnostic(desc.diagnostics, SceneRenderDiagnosticKind::DroppedInstances, SceneRenderDiagnosticSeverity::Warning, instance, commandKey.materialAssetId);
            return nullptr;
        }
        if (command == nullptr) {
            command = &CreateCommand(scope, instance, target, drawCommandIndex);
        }
        command->gpuFrustumCull = scope.gpuFrustumCull;
        if (!joinsLastCommand) {
            lastCommandKey = commandKey;
            lastCommandIndex = drawCommandIndex;
        }
        return command;
    }

    void Append(const MeshSectionScope& scope, MeshDrawCommand& command, std::uint32_t drawCommandIndex,
        const SceneRenderMeshInstance& instance, const MeshPipelineScanInstance& admitted) {
        std::uint32_t gpuDrivenRecordIndex = UINT32_MAX;
        if (scope.gpuDrivenCandidate) {
            MeshPipelineGpuDrivenRecorder::AccumulateCandidateStats(result.stats, scope.meshResource, scope.meshletRange);
            gpuDrivenRecordIndex = static_cast<std::uint32_t>(result.gpuDrivenInputRecords.size());
            MeshPipelineGpuDrivenRecorder::Record(result, instance.entityId, admitted.worldBounds, drawCommandIndex, admitted.lodLevel, scope.meshletRange, false);
        }
        command.sortKey += admitted.depthBucket;
        SceneRenderMeshInstance& accepted = command.instances.emplace_back(instance);
        accepted.worldBounds = admitted.worldBounds;
        accepted.depthBucket = admitted.depthBucket;
        if (desc.pass == MeshPassType::BaseTransparent) {
            const float viewDepth = std::abs(admitted.viewDepth);
            result.transparentInstanceScratch.push_back(MeshPipelineTransparentInstanceRef{
                .commandIndex = drawCommandIndex,
                .instanceIndex = static_cast<std::uint32_t>(command.instances.size() - 1U),
                .gpuDrivenRecordIndex = gpuDrivenRecordIndex,
                .viewDepth = std::isfinite(viewDepth) ? viewDepth : 0.0F,
            });
        }
        ++acceptedInstanceCount;
    }

    void Add(const MeshSectionScope& scope, const SceneRenderMeshInstance& instance,
        const MeshPipelineScanTarget& target, const MeshPipelineScanInstance& admitted) {
        std::uint32_t drawCommandIndex = 0U;
        if (MeshDrawCommand* command = CommandFor(scope, instance, target, admitted, drawCommandIndex)) {
            Append(scope, *command, drawCommandIndex, instance, admitted);
        }
    }
};

// Builds one batch into the draw commands on the calling thread.
void BuildBatch(const MeshScanContext& context, MeshScanState& state, MeshCommandWriter& writer, const SceneMeshBatch& batch) {
    const MeshPassProcessorDesc& desc = context.desc;
    // Without a cache, which most passes have none of, there is nothing to reuse and nothing to record.
    std::optional<SceneMeshBatchCommandCache::BatchScope> batchScope;
    if (desc.batchCommandCache != nullptr) {
        batchScope.emplace(desc.batchCommandCache, writer.result, batch, writer.commandCount, writer.acceptedInstanceCount, desc.diagnostics);
        if (batchScope->TryReuse()) return;
        batchScope->RecycleStorage();
    }
    const std::uint32_t candidateCount = CountCandidates(context, state, batch);
    if (candidateCount == 0U) {
        return;
    }
    const MeshPipelineScanBatch resolved = ResolveBatchMesh(context, batch, candidateCount, state.stats, state.diagnostics);
    if (!resolved.drawn) {
        return;
    }
    ScanBatch(context, state, batch, resolved,
        [&] {
            if (desc.batchCommandCache != nullptr) ClearKeepingNodes(writer.result.commandLookupScratch, writer.result.commandLookupNodes);
            writer.lastCommandKey.reset();
            writer.lastCommandIndex = 0U;
        },
        [&](const MeshSectionScope& scope, const MeshPipelineScanTarget& target, const MeshPipelineScanInstance& admitted) {
            writer.Add(scope, batch.instances[admitted.instanceIndex], target, admitted);
        });
    writer.result.stats.culledInstanceCount += std::exchange(state.culledInstanceCount, 0U);
}

// Scans the batches of a chunk on a worker into the chunk's own storage.
void ScanChunk(const MeshScanContext& context, MeshPipelineScanChunk& chunk) {
    chunk.runs.clear();
    chunk.instances.clear();
    chunk.stats = SceneRenderSubmitStats{};
    ClearKeepingNodes(chunk.materialResolutions, chunk.materialResolutionNodes);
    MeshScanState state{
        .stats = chunk.stats,
        .diagnostics = nullptr,
        .materialResolutions = chunk.materialResolutions,
        .materialResolutionNodes = chunk.materialResolutionNodes,
        .chainLevels = chunk.chainLevels,
        .selectionBits = chunk.selectionBits,
    };
    for (std::size_t offset = 0U; offset < chunk.batchCount; ++offset) {
        const MeshPipelineScanBatch& resolved = chunk.batches[offset];
        if (!resolved.drawn) {
            continue;
        }
        const std::size_t batchIndex = chunk.firstBatch + offset;
        ScanBatch(context, state, context.desc.meshBatches[batchIndex], resolved, [] {},
            [&](const MeshSectionScope&, const MeshPipelineScanTarget& target, const MeshPipelineScanInstance& admitted) {
                if (chunk.runs.empty() || chunk.runs.back().batchIndex != batchIndex ||
                    !(chunk.runs.back().target.key == target.key)) {
                    chunk.runs.push_back(MeshPipelineScanRun{ .batchIndex = batchIndex, .instanceCount = 0U, .target = target });
                }
                ++chunk.runs.back().instanceCount;
                chunk.instances.push_back(admitted);
            });
    }
    chunk.culledInstanceCount = state.culledInstanceCount;
    chunk.clean = !chunk.stats.HasMissingResources();
    chunk.ready.store(true, std::memory_order_release);
}

// Writes what a chunk admitted into the draw commands, as building its batches one after the other would have.
void MergeChunk(const MeshScanContext& context, const MeshPipelineScanChunk& chunk, MeshCommandWriter& writer) {
    MeshSectionScope scope;
    std::size_t scopeBatch = SIZE_MAX;
    std::uint32_t scopeSection = UINT32_MAX;
    std::size_t instanceCursor = 0U;
    const bool budgeted = context.desc.maxDrawCommands != 0U || context.desc.maxVisibleInstances != 0U;
    for (const MeshPipelineScanRun& run : chunk.runs) {
        if (run.batchIndex != scopeBatch || run.target.key.sectionIndex != scopeSection) {
            scope = MakeSectionScope(context.desc, chunk.batches[run.batchIndex - chunk.firstBatch], run.target.key.sectionIndex);
            scopeBatch = run.batchIndex;
            scopeSection = run.target.key.sectionIndex;
        }
        const SceneMeshBatch& batch = context.desc.meshBatches[run.batchIndex];
        // Without a budget, the first instance of a run settles its command and the rest join it.
        MeshDrawCommand* command = nullptr;
        std::uint32_t drawCommandIndex = 0U;
        for (std::uint32_t index = 0U; index < run.instanceCount; ++index) {
            if (index + kMergePrefetchDistance < run.instanceCount) {
                PrefetchInstance(batch.instances[chunk.instances[instanceCursor + kMergePrefetchDistance].instanceIndex]);
            }
            const MeshPipelineScanInstance& admitted = chunk.instances[instanceCursor++];
            const SceneRenderMeshInstance& instance = batch.instances[admitted.instanceIndex];
            if (index == 0U || budgeted) {
                command = writer.CommandFor(scope, instance, run.target, admitted, drawCommandIndex);
            }
            if (command != nullptr) {
                writer.Append(scope, *command, drawCommandIndex, instance, admitted);
            }
        }
    }
    writer.result.stats.lodSelectionCount += chunk.stats.lodSelectionCount;
    writer.result.stats.culledInstanceCount += chunk.culledInstanceCount;
}

// Splits the batches of a large pass into chunks of consecutive batches, which `result.scanChunks` then holds, and
// returns their number; 0 when the pass is built on this thread. A batch that is built serially (a missing mesh reports
// in order, visibility clusters read what the scan has counted) ends the chunk it follows.
[[nodiscard]] std::size_t PlanScanChunks(const MeshScanContext& context, MeshPipelineBuildResult& result, std::size_t instanceCount,
    const kb::ecs::WorkerPool* pool) {
    const MeshPassProcessorDesc& desc = context.desc;
    if (pool == nullptr || desc.batchCommandCache != nullptr || !pool->Running() ||
        instanceCount < MeshPassProcessor::ParallelThreshold) {
        return 0U;
    }
    const std::size_t chunkInstances = std::max(kMinChunkInstances, (instanceCount + kMaxChunks - 1U) / kMaxChunks);
    SceneRenderSubmitStats probeStats;
    std::size_t chunkCount = 0U;
    MeshPipelineScanChunk* open = nullptr;
    std::size_t openInstances = 0U;
    for (std::size_t batchIndex = 0U; batchIndex < desc.meshBatches.size(); ++batchIndex) {
        const SceneMeshBatch& batch = desc.meshBatches[batchIndex];
        MeshPipelineScanBatch resolved;
        if (!batch.instances.empty()) {
            // Resolving a mesh that is there changes nothing. A batch whose mesh is not there is built by BuildBatch,
            // which reports it, or skips the batch without resolving the mesh when the pass draws none of its instances.
            resolved = ResolveBatchMesh(context, batch, static_cast<std::uint32_t>(batch.instances.size()), probeStats, nullptr);
            if (!resolved.drawn || !batch.visibilityClusters.empty()) {
                open = nullptr;
                continue;
            }
        } else if (open == nullptr) {
            continue;
        }
        if (open == nullptr) {
            if (chunkCount == result.scanChunks.size()) {
                result.scanChunks.emplace_back();
            }
            open = &result.scanChunks[chunkCount++];
            open->firstBatch = batchIndex;
            open->batchCount = 0U;
            open->batches.clear();
            open->ready.store(false, std::memory_order_relaxed);
            openInstances = 0U;
        }
        open->batches.push_back(resolved);
        ++open->batchCount;
        openInstances += batch.instances.size();
        if (openInstances >= chunkInstances) {
            open = nullptr;
        }
    }
    return chunkCount < kMinChunks ? 0U : chunkCount;
}

// The build of one pass: what its scan reads, the result it writes and the chunks planned for it.
class PassBuild {
public:
    PassBuild(const MeshPassProcessorDesc& desc, MeshPipelineBuildResult& result, const kb::ecs::WorkerPool* pool)
        : frustum_(MeshPipelineVisibility::BuildFrustum(desc.camera)),
          context_{
              .desc = desc,
              .frustum = frustum_,
              .cullingMask = desc.camera != nullptr ? desc.camera->cullingMask : 0xFFFFFFFFU,
              .detailSwitchLevels = result.detailSwitchLevels,
          },
          result_(result) {
        ResolveDetailSwitchLevels(desc, result);
        result.commands.reserve(desc.meshBatches.size());
        for (const SceneMeshBatch& batch : desc.meshBatches) {
            instanceCount_ += batch.instances.size();
        }
        chunkCount_ = PlanScanChunks(context_, result, instanceCount_, pool);
    }

    PassBuild(const PassBuild&) = delete;
    PassBuild& operator=(const PassBuild&) = delete;

    [[nodiscard]] std::size_t InstanceCount() const noexcept { return instanceCount_; }
    [[nodiscard]] std::size_t ChunkCount() const noexcept { return chunkCount_; }
    [[nodiscard]] const MeshPipelineScanChunk& Chunk(std::size_t index) const noexcept { return result_.scanChunks[index]; }
    void Scan(std::size_t index) { ScanChunk(context_, result_.scanChunks[index]); }

    // Writes the batches into the draw commands in order. `await(index)` returns the chunk once it is scanned.
    template <typename Await>
    void Build(Await&& await) {
        const MeshPassProcessorDesc& desc = context_.desc;
        MeshCommandWriter writer{ .desc = desc, .result = result_ };
        std::vector<std::uint8_t> chainLevels;
        std::vector<std::uint64_t> selectionBits;
        MeshScanState state{
            .stats = result_.stats,
            .diagnostics = desc.diagnostics,
            .materialResolutions = result_.materialResolutionScratch,
            .materialResolutionNodes = result_.materialResolutionNodes,
            .chainLevels = chainLevels,
            .selectionBits = selectionBits,
        };
        std::size_t nextChunk = 0U;
        for (std::size_t batchIndex = 0U; batchIndex < desc.meshBatches.size(); ++batchIndex) {
            if (nextChunk < chunkCount_ && result_.scanChunks[nextChunk].firstBatch == batchIndex) {
                const MeshPipelineScanChunk& chunk = await(nextChunk++);
                if (chunk.clean) {
                    MergeChunk(context_, chunk, writer);
                    batchIndex += chunk.batchCount - 1U;
                    continue;
                }
            }
            BuildBatch(context_, state, writer, desc.meshBatches[batchIndex]);
        }
        MeshPipelineCommandBuilder::FinalizeCommands(result_, desc.pass, writer.commandCount,
            desc.maxDrawCommands, desc.diagnostics);
    }

private:
    MeshPipelineFrustum frustum_;
    MeshScanContext context_;
    MeshPipelineBuildResult& result_;
    std::size_t instanceCount_ = 0U;
    std::size_t chunkCount_ = 0U;
};

// Builds passes on the calling thread and on the workers of a pool. Whoever is free takes the next chunk of any pass
// that no one has taken, which the calling thread would otherwise wait for while workers are still waking. Passes that are
// built together are also written into their draw commands side by side: the calling thread takes the first, workers the
// others, and a thread that waits for a chunk of its pass scans chunks of any pass meanwhile. A pass is written in
// order, as its chunks are scanned.
class MeshPassScheduler {
public:
    MeshPassScheduler(std::span<std::optional<PassBuild>> builds, kb::ecs::WorkerPool* pool)
        : builds_(builds) {
        std::size_t chunkCount = 0U;
        std::size_t instanceCount = 0U;
        for (const std::optional<PassBuild>& build : builds_) {
            chunkCount += build->ChunkCount();
            instanceCount += build->InstanceCount();
        }
        // Passes with no chunk of their own are still built side by side once together they are worth the workers.
        const bool sideBySide = builds_.size() > 1U && instanceCount >= kMinSideBySideInstances;
        if (pool == nullptr || !pool->Running() || (chunkCount == 0U && !sideBySide)) {
            return;
        }
        // One chunk of each pass in turn, so that every pass can start to be written early.
        scanOrder_.reserve(chunkCount);
        for (std::size_t round = 0U; scanOrder_.size() < chunkCount; ++round) {
            for (std::size_t pass = 0U; pass < builds_.size(); ++pass) {
                if (round < builds_[pass]->ChunkCount()) {
                    scanOrder_.push_back(ChunkRef{ .pass = pass, .index = round });
                }
            }
        }
        const std::size_t workerCount = std::min(pool->WorkerCount(), kMaxScanWorkers);
        std::vector<kb::ecs::WorkerPoolChunk> drains(workerCount);
        for (std::size_t index = 0U; index < workerCount; ++index) {
            drains[index] = kb::ecs::WorkerPoolChunk{
                .index = index, .begin = index, .count = 1U, .preferredWorkerIndex = index, .workerCountLimit = workerCount };
        }
        job_ = pool->SubmitParallelForChunks(std::move(drains), &MeshPassScheduler::Drain, this);
    }

    // Waits until no worker uses the passes any more.
    ~MeshPassScheduler() {
        job_.Wait();
    }

    MeshPassScheduler(const MeshPassScheduler&) = delete;
    MeshPassScheduler& operator=(const MeshPassScheduler&) = delete;

    // Builds every pass; returns once all are written.
    void Run() {
        BuildPass(0U);
        BuildTakenPasses();
        while (ScanNext()) {
        }
        job_.Wait();
    }

private:
    static void Drain(kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk&, void* self) {
        auto& scheduler = *static_cast<MeshPassScheduler*>(self);
        scheduler.BuildTakenPasses();
        while (scheduler.ScanNext()) {
        }
    }

    // The calling thread builds the first pass; every other goes to whichever thread asks first.
    void BuildTakenPasses() {
        for (std::size_t pass = nextPass_.fetch_add(1U, std::memory_order_relaxed); pass < builds_.size();
             pass = nextPass_.fetch_add(1U, std::memory_order_relaxed)) {
            BuildPass(pass);
        }
    }

    void BuildPass(std::size_t pass) {
        builds_[pass]->Build([this, pass](std::size_t index) -> const MeshPipelineScanChunk& {
            const MeshPipelineScanChunk& chunk = builds_[pass]->Chunk(index);
            while (!chunk.ready.load(std::memory_order_acquire)) {
                ScanNext();
            }
            return chunk;
        });
    }

    bool ScanNext() {
        if (next_.load(std::memory_order_relaxed) >= scanOrder_.size()) {
            return false;
        }
        const std::size_t next = next_.fetch_add(1U, std::memory_order_relaxed);
        if (next >= scanOrder_.size()) {
            return false;
        }
        builds_[scanOrder_[next].pass]->Scan(scanOrder_[next].index);
        return true;
    }

    struct ChunkRef {
        std::size_t pass;
        std::size_t index;
    };

    std::span<std::optional<PassBuild>> builds_;
    // The chunks of all passes in the order the threads take them.
    std::vector<ChunkRef> scanOrder_;
    std::atomic<std::size_t> next_{ 0U };
    std::atomic<std::size_t> nextPass_{ 1U };
    kb::ecs::JobHandle job_;
};

} // namespace

void MeshPassProcessor::BuildCommandsInto(std::span<const MeshPassBuild> passes) noexcept {
    if (passes.empty()) {
        return;
    }
    kb::ecs::WorkerPool* const pool = passes.front().desc.workerPool;
    // A pass built alone needs no storage on the heap.
    std::optional<PassBuild> single;
    std::vector<std::optional<PassBuild>> many(passes.size() > 1U ? passes.size() : 0U);
    const std::span<std::optional<PassBuild>> builds = passes.size() > 1U ? std::span{ many } : std::span{ &single, 1U };
    // Planning a pass reads all its batches: with batches enough to make it worth the workers, the passes are planned side by side.
    if (pool != nullptr && pool->Running() && passes.size() > 1U && passes.front().desc.meshBatches.size() >= kMinSideBySidePlanBatches) {
        pool->ParallelForChunks(passes.size(), 1U, [&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
            for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count; ++index) {
                builds[index].emplace(passes[index].desc, *passes[index].result, pool);
            }
        });
    } else {
        for (std::size_t index = 0U; index < passes.size(); ++index) {
            builds[index].emplace(passes[index].desc, *passes[index].result, pool);
        }
    }
    MeshPassScheduler scheduler(builds, pool);
    scheduler.Run();
}

void MeshPassProcessor::BuildCommandsInto(const MeshPassProcessorDesc& desc, MeshPipelineBuildResult& result) noexcept {
    const MeshPassBuild pass{ .desc = desc, .result = &result };
    BuildCommandsInto(std::span<const MeshPassBuild>{ &pass, 1U });
}

} // namespace kb::render
