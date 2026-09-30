#pragma once

#include "kb/render/scene/MeshPipeline.hpp"
#include <atomic>
#include <optional>

namespace kb::render {

struct SceneMeshCommandReuseKey {
    std::uint64_t sceneRevision = 0U;
    std::uint64_t resourceRevision = 0U;
    std::uint64_t bindingRevision = 0U;
    std::uint64_t detailSwitchHistoryRevision = 0U;
    const RenderResourceRegistry* resources = nullptr;
    const SceneRenderResourceMap* bindings = nullptr;
    std::optional<SceneRenderCamera> camera;
    SceneRenderDrawBudget budget;
    SceneGpuDrivenFeatureSupport gpuSupport;
    bool terrainLayersOnly = false;
    [[nodiscard]] bool operator==(const SceneMeshCommandReuseKey&) const noexcept = default;
};

[[nodiscard]] inline std::optional<SceneRenderCamera> SceneMeshCullingCamera(const SceneRenderCamera* camera) {
    if (camera == nullptr) return std::nullopt;
    auto stable = *camera;
    stable.projection = camera->CullingProjection();
    stable.temporalProjectionOffset = {};
    return stable;
}

// Owns only validity and telemetry. Instance lists stay in the existing per-pass
// storage; ECS and resource owners publish revisions when their inputs change.
class SceneMeshCommandReuseGate {
public:
    [[nodiscard]] bool Matches(const SceneMeshCommandReuseKey& key) const noexcept {
        return key_ && *key_ == key;
    }
    [[nodiscard]] SceneRenderSubmitStats Stats() const noexcept { return stats_; }
    void Reset() noexcept { key_.reset(); }
    void Commit(const SceneMeshCommandReuseKey& key, std::span<MeshDrawCommand> commands,
        const SceneRenderSubmitStats& stats) {
        Reset();
        if (stats.HasMissingResources() || stats.droppedInstanceCount != 0U ||
            (stats.gpuDrivenFeatureState != SceneGpuDrivenFeatureState::Disabled &&
             stats.gpuDrivenFeatureState != SceneGpuDrivenFeatureState::CpuValidationOnly)) return;
        for (const auto& command : commands) {
            if (command.meshResource == nullptr ||
                command.currentSkinningPalette.IsValid() || command.previousSkinningPalette.IsValid()) return;
        }
        static std::atomic<std::uint64_t> next{1U};
        auto revision = next.fetch_add(commands.size(), std::memory_order_relaxed);
        for (auto& command : commands) command.instanceRevision = revision++;
        stats_ = stats;
        stats_.meshDrawCommandCacheMissCount = 0U;
        stats_.meshDrawCommandCacheHitCount = static_cast<std::uint32_t>(commands.size());
        stats_.meshDrawCommandCacheBuildCount = 0U;
        stats_.meshDrawCommandCachePruneCount = 0U;
        stats_.meshCommandReuseCount = static_cast<std::uint32_t>(commands.size());
        key_ = key;
    }
private:
    std::optional<SceneMeshCommandReuseKey> key_;
    SceneRenderSubmitStats stats_;
};

} // namespace kb::render
