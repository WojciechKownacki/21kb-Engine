#pragma once

#include "scene/cache/SceneMeshCommandReuseGate.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace kb::render {

// Per-pass validity and temporary command ownership. Retention moves the existing
// instance vectors; it never duplicates authored data or caches component values.
class SceneMeshBatchCommandCache {
    struct Record {
        std::uint64_t revision = 0U;
        std::uint64_t historyRevision = 0U;
        std::uint64_t seenBuild = 0U;
        std::size_t commandPrefix = 0U;
        std::uint32_t instancePrefix = 0U;
        std::uint32_t acceptedInstances = 0U;
        SceneRenderSubmitStats stats;
        std::vector<std::size_t> commandIndices;
        // Derived CPU validation records, with command indices relative to this batch.
        std::vector<SceneGpuDrivenInputRecord> gpuRecords;
        std::size_t commandCount = 0U;
        bool valid = false;
    };
public:
    void Reset() noexcept { key_.reset(); records_.clear(); previousCommands_.clear(); }

    [[nodiscard]] bool HasStableCamera(const SceneMeshCommandReuseKey& key) const noexcept {
        return !key_ || key_->camera == key.camera;
    }
    void DiscardContent() noexcept { records_.clear(); previousCommands_.clear(); }

    void Prepare(SceneMeshCommandReuseKey key) {
        key.sceneRevision = 0U;
        key.detailSwitchHistoryRevision = 0U;
        if (!key_ || *key_ != key) {
            // A new camera/resource context invalidates content, not allocation ownership.
            for (auto& [id, record] : records_) { static_cast<void>(id); record.valid = false; }
        }
        key_ = std::move(key);
    }

    void BeginBuild(MeshPassType pass, MeshPipelineBuildResult& result) {
        if (pass_ != pass) records_.clear();
        pass_ = pass;
        ++buildId_;
        previousCommands_.swap(result.commands);
        result.commands.clear();
    }

    class BatchScope {
    public:
        BatchScope(SceneMeshBatchCommandCache* cache, MeshPipelineBuildResult& result,
            const SceneMeshBatch& batch, std::size_t& commandCount,
            std::uint32_t& acceptedInstances, SceneRenderDiagnostics* diagnostics)
            : cache_(cache), result_(result), batch_(batch), commandCount_(commandCount),
              acceptedInstances_(acceptedInstances), diagnostics_(diagnostics),
              firstCommand_(commandCount), firstInstance_(acceptedInstances),
              firstGpuRecord_(result.gpuDrivenInputRecords.size()),
              diagnosticCount_(diagnostics == nullptr ? 0U : diagnostics->events.size()) {
            if (cache_ != nullptr) { accumulated_ = result_.stats; result_.stats = {}; }
        }
        ~BatchScope() {
            if (cache_ == nullptr) return;
            if (batch_.cacheId != 0U) {
                auto& record = cache_->records_[batch_.cacheId];
                record.revision = batch_.contentRevision;
                record.historyRevision = result_.detailSwitchHistoryRevision;
                record.seenBuild = cache_->buildId_;
                record.commandPrefix = firstCommand_;
                record.instancePrefix = firstInstance_;
                record.acceptedInstances = acceptedInstances_ - firstInstance_;
                record.stats = result_.stats;
                const auto decision = SceneGpuDrivenFeatureClassifier::Decide(SceneGpuDrivenFeatureRequest{
                    .gpuCullingRequested = record.stats.gpuDrivenDrawCandidateCount != 0U,
                    .indirectDrawRequested = record.stats.indirectDrawCandidateCount != 0U,
                    .meshletSubmitRequested = record.stats.meshletCullingCandidateCount != 0U,
                }, cache_->key_ ? cache_->key_->gpuSupport : SceneGpuDrivenFeatureSupport{});
                record.valid = !record.stats.HasMissingResources() && record.stats.droppedInstanceCount == 0U &&
                    (decision.state == SceneGpuDrivenFeatureState::Disabled || decision.state == SceneGpuDrivenFeatureState::CpuValidationOnly) &&
                    (diagnostics_ == nullptr || diagnosticCount_ == diagnostics_->events.size());
                for (std::size_t index = firstCommand_; index < commandCount_; ++index) {
                    auto& command = result_.commands[index];
                    command.sourceBatchId = batch_.cacheId;
                    command.sourceBatchCommandIndex = static_cast<std::uint32_t>(index - firstCommand_);
                    record.valid &= command.meshResource != nullptr &&
                        !command.currentSkinningPalette.IsValid() && !command.previousSkinningPalette.IsValid();
                }
                record.commandCount = commandCount_ - firstCommand_;
                if (record.valid) {
                    record.gpuRecords.assign(result_.gpuDrivenInputRecords.begin() + firstGpuRecord_, result_.gpuDrivenInputRecords.end());
                    for (auto& input : record.gpuRecords) input.drawCommandIndex -= static_cast<std::uint32_t>(firstCommand_);
                } else record.gpuRecords.clear();
            }
            result_.stats += accumulated_;
        }

        [[nodiscard]] bool TryReuse() {
            if (cache_ == nullptr || batch_.cacheId == 0U) return false;
            const auto found = cache_->records_.find(batch_.cacheId);
            if (found == cache_->records_.end()) return false;
            const Record& record = found->second;
            if (!cache_->key_ || record.seenBuild == cache_->buildId_ || !record.valid || record.revision != batch_.contentRevision ||
                record.historyRevision != result_.detailSwitchHistoryRevision) return false;
            if ((cache_->key_->budget.maxDrawCommands != 0U || cache_->key_->budget.maxVisibleInstances != 0U) &&
                (record.commandPrefix != firstCommand_ || record.instancePrefix != firstInstance_)) return false;
            for (const auto index : record.commandIndices) {
                if (index >= cache_->previousCommands_.size() ||
                    cache_->previousCommands_[index].sourceBatchId != batch_.cacheId ||
                    cache_->previousCommands_[index].instanceRevision == 0U ||
                    !SceneDrawCommandCache::Touch(result_.drawCommandCache, cache_->previousCommands_[index].cachedTemplateKey)) return false;
            }
            for (const auto index : record.commandIndices) {
                auto& command = cache_->previousCommands_[index];
                if (commandCount_ == result_.commands.size()) result_.commands.push_back(std::move(command));
                else result_.commands[commandCount_] = std::move(command);
                ++commandCount_;
            }
            for (auto input : record.gpuRecords) {
                input.drawCommandIndex += static_cast<std::uint32_t>(firstCommand_);
                result_.gpuDrivenInputRecords.push_back(input);
            }
            acceptedInstances_ += record.acceptedInstances;
            result_.stats = record.stats;
            result_.stats.meshDrawCommandCacheMissCount = 0U;
            result_.stats.meshDrawCommandCacheBuildCount = 0U;
            result_.stats.meshDrawCommandCachePruneCount = 0U;
            result_.stats.meshDrawCommandCacheHitCount = static_cast<std::uint32_t>(record.commandIndices.size());
            result_.stats.meshCommandReuseCount = static_cast<std::uint32_t>(record.commandIndices.size());
            return true;
        }

        void RecycleStorage() {
            if (cache_ == nullptr) return;
            const auto found = cache_->records_.find(batch_.cacheId);
            if (found == cache_->records_.end()) return;
            std::size_t writeIndex = firstCommand_;
            for (const auto index : found->second.commandIndices) {
                if (index >= cache_->previousCommands_.size()) continue;
                auto& command = cache_->previousCommands_[index];
                if (writeIndex == result_.commands.size()) result_.commands.push_back(std::move(command));
                else result_.commands[writeIndex] = std::move(command);
                ++writeIndex;
            }
        }
    private:
        SceneMeshBatchCommandCache* cache_;
        MeshPipelineBuildResult& result_;
        const SceneMeshBatch& batch_;
        std::size_t& commandCount_;
        std::uint32_t& acceptedInstances_;
        SceneRenderDiagnostics* diagnostics_;
        std::size_t firstCommand_;
        std::uint32_t firstInstance_;
        std::size_t firstGpuRecord_;
        std::size_t diagnosticCount_;
        SceneRenderSubmitStats accumulated_;
    };

    void EndBuild(MeshPipelineBuildResult& result) {
        std::erase_if(records_, [this](const auto& entry) {
            return entry.second.seenBuild != buildId_;
        });
        for (auto& [id, record] : records_) {
            static_cast<void>(id);
            record.commandIndices.assign(record.commandCount, SIZE_MAX);
        }
        for (std::size_t index = 0U; index < result.commands.size(); ++index) {
            auto& command = result.commands[index];
            const auto found = records_.find(command.sourceBatchId);
            if (found == records_.end()) continue;
            if (found->second.valid) RetainSceneMeshInstanceRevision(command);
            found->second.commandIndices.at(command.sourceBatchCommandIndex) = index;
        }
        previousCommands_.clear();
    }

private:
    std::optional<SceneMeshCommandReuseKey> key_;
    std::unordered_map<std::uint64_t, Record> records_;
    std::vector<MeshDrawCommand> previousCommands_;
    std::uint64_t buildId_ = 0U;
    MeshPassType pass_ = MeshPassType::BaseOpaque;
};

} // namespace kb::render
