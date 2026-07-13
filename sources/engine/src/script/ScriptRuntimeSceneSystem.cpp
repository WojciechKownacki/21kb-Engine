#include "engine/script/ScriptRuntimeSceneSystem.hpp"

#include "engine/scene/BehaviourExecutionOrder.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneSystemContext.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace kb::script {
namespace {

void MergeResult(ScriptRuntimeExecutionResult& target, ScriptRuntimeExecutionResult source) {
    target.visitedBehaviours += source.visitedBehaviours;
    target.executedBehaviours += source.executedBehaviours;
    target.emittedEvents.reserve(target.emittedEvents.size() + source.emittedEvents.size());
    for (ScriptEvent& event : source.emittedEvents) {
        target.emittedEvents.push_back(std::move(event));
    }
    target.diagnostics.reserve(target.diagnostics.size() + source.diagnostics.size());
    for (ScriptDiagnostic& diagnostic : source.diagnostics) {
        target.diagnostics.push_back(std::move(diagnostic));
    }
}

struct RawBehaviourRecord {
    kb::scene::SceneEntity entity{};
    kb::scene::BehaviourComponent behaviour{};
};

struct BehaviourCollectContext {
    std::vector<RawBehaviourRecord>* records = nullptr;
};

void CollectBehaviour(kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour, void* rawContext) {
    auto& context = *static_cast<BehaviourCollectContext*>(rawContext);
    context.records->push_back(RawBehaviourRecord{
        .entity = entity,
        .behaviour = behaviour,
    });
}

} // namespace

std::size_t ScriptRuntimeSceneSystem::BehaviourLifecycleKeyHasher::operator()(BehaviourLifecycleKey key) const noexcept {
    std::uint64_t hash = key.entityId;
    hash ^= key.assetId + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    hash ^= static_cast<std::uint64_t>(key.backend) + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return static_cast<std::size_t>(hash);
}

ScriptRuntimeSceneSystem::ScriptRuntimeSceneSystem(ScriptRuntime& runtime) noexcept
    : runtime_(runtime) {}

ScriptRuntimeSceneSystem::ScriptRuntimeSceneSystem(ScriptRuntime& runtime, ScriptRuntimeAssetPreparer& assetPreparer) noexcept
    : runtime_(runtime)
    , assetPreparer_(&assetPreparer) {}

void ScriptRuntimeSceneSystem::OnCreate(kb::scene::SceneSystemContext& context) {
    static_cast<void>(ExecuteStartup(context.GetScene(), context.DeltaSeconds()));
}

void ScriptRuntimeSceneSystem::OnUpdate(kb::scene::SceneSystemContext& context) {
    static_cast<void>(ExecuteFrame(context.GetScene(), context.DeltaSeconds()));
}

void ScriptRuntimeSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    static_cast<void>(ExecuteShutdown(context.GetScene(), context.DeltaSeconds()));
}

const ScriptRuntimeExecutionResult& ScriptRuntimeSceneSystem::ExecuteStartup(kb::scene::Scene& scene, float deltaSeconds) {
    lastResult_ = {};
    PrepareScene(scene);
    SyncBehaviourLifecycles(scene, deltaSeconds);
    return lastResult_;
}

const ScriptRuntimeExecutionResult& ScriptRuntimeSceneSystem::ExecuteFrame(kb::scene::Scene& scene, float deltaSeconds) {
    lastResult_ = {};
    PrepareScene(scene);
    const float clampedDeltaSeconds = std::max(deltaSeconds, 0.0F);
    SyncBehaviourLifecycles(scene, clampedDeltaSeconds);
    fixedAccumulatorSeconds_ += clampedDeltaSeconds;
    std::size_t fixedSteps = 0U;
    while (frameSettings_.fixedDeltaSeconds > 0.0F && fixedAccumulatorSeconds_ >= frameSettings_.fixedDeltaSeconds && fixedSteps < frameSettings_.maxFixedStepsPerFrame) {
        fixedAccumulatorSeconds_ -= frameSettings_.fixedDeltaSeconds;
        ++fixedSteps;
        MergeResult(lastResult_, runtime_.ExecuteLifecycleAndDispatchEvents(scene, ScriptLifecycleEvent::FixedTick, frameSettings_.fixedDeltaSeconds));
    }
    if (frameSettings_.fixedDeltaSeconds <= 0.0F || frameSettings_.maxFixedStepsPerFrame == 0U ||
        (fixedSteps == frameSettings_.maxFixedStepsPerFrame && fixedAccumulatorSeconds_ >= frameSettings_.fixedDeltaSeconds)) {
        fixedAccumulatorSeconds_ = 0.0F;
    }
    MergeResult(lastResult_, runtime_.ExecuteLifecycleAndDispatchEvents(scene, ScriptLifecycleEvent::Tick, clampedDeltaSeconds));
    MergeResult(lastResult_, runtime_.ExecuteLifecycleAndDispatchEvents(scene, ScriptLifecycleEvent::LateTick, clampedDeltaSeconds));
    MergeResult(lastResult_, runtime_.ExecuteLifecycleAndDispatchEvents(scene, ScriptLifecycleEvent::BeforeRender, clampedDeltaSeconds));
    MergeResult(lastResult_, runtime_.ExecuteLifecycleAndDispatchEvents(scene, ScriptLifecycleEvent::AfterRender, clampedDeltaSeconds));
    return lastResult_;
}

const ScriptRuntimeExecutionResult& ScriptRuntimeSceneSystem::ExecuteShutdown(kb::scene::Scene& scene, float deltaSeconds) {
    lastResult_ = {};
    ShutdownTrackedBehaviours(scene, deltaSeconds);
    return lastResult_;
}

const ScriptRuntimeExecutionResult& ScriptRuntimeSceneSystem::ExecutePhase(kb::scene::Scene& scene, ScriptLifecycleEvent event, float deltaSeconds) {
    PrepareScene(scene);
    lastResult_ = runtime_.ExecuteLifecycleAndDispatchEvents(scene, event, deltaSeconds);
    return lastResult_;
}

void ScriptRuntimeSceneSystem::SetFrameSettings(ScriptRuntimeFrameSettings settings) noexcept {
    frameSettings_ = settings;
}

ScriptRuntimeFrameSettings ScriptRuntimeSceneSystem::FrameSettings() const noexcept {
    return frameSettings_;
}

const ScriptRuntimeExecutionResult& ScriptRuntimeSceneSystem::LastResult() const noexcept {
    return lastResult_;
}

const ScriptRuntimeAssetPrepareResult& ScriptRuntimeSceneSystem::LastPrepareResult() const noexcept {
    return lastPrepareResult_;
}

void ScriptRuntimeSceneSystem::PrepareScene(kb::scene::Scene& scene) {
    lastPrepareResult_ = assetPreparer_ == nullptr ? ScriptRuntimeAssetPrepareResult{} : assetPreparer_->PrepareSceneBehaviours(scene);
}

void ScriptRuntimeSceneSystem::SyncBehaviourLifecycles(kb::scene::Scene& scene, float deltaSeconds) {
    const std::vector<BehaviourLifecycleRecord> currentRecords = CollectBehaviourRecords(scene);
    std::unordered_set<BehaviourLifecycleKey, BehaviourLifecycleKeyHasher> seen;
    seen.reserve(currentRecords.size());

    for (const BehaviourLifecycleRecord& current : currentRecords) {
        const BehaviourLifecycleKey key = MakeKey(current.entity, current.behaviour);
        static_cast<void>(seen.insert(key));
        BehaviourLifecycleRecord& tracked = lifecycleRecords_[key];
        if (!tracked.entity.IsValid()) {
            tracked = BehaviourLifecycleRecord{
                .entity = current.entity,
                .behaviour = current.behaviour,
                .active = false,
                .created = false,
            };
        }

        if (current.behaviour.enabled) {
            tracked.entity = current.entity;
            tracked.behaviour = current.behaviour;
            if (!tracked.created) {
                ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Created, deltaSeconds);
                tracked.created = true;
            }
            if (!tracked.active) {
                ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Activated, deltaSeconds);
                ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Ready, deltaSeconds);
                tracked.active = true;
            }
            continue;
        }

        if (tracked.active) {
            ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Deactivated, deltaSeconds);
            tracked.active = false;
        }
        tracked.entity = current.entity;
        tracked.behaviour = current.behaviour;
    }

    // lifecycleRecords_ is an unordered_map: collect the departing records
    // first and dispatch Deactivated/Destroyed in the guaranteed execution
    // order instead of unordered_map's iteration order, which is not part of
    // the ordering contract behaviours may rely on. MakeKey() is pure, so
    // the key for erase can be recomputed after sorting instead of carrying
    // a parallel key alongside each record.
    std::vector<BehaviourLifecycleRecord> removed;
    for (const auto& [key, tracked] : lifecycleRecords_) {
        if (seen.contains(key)) {
            continue;
        }
        removed.push_back(tracked);
    }
    DispatchDeactivateAndDestroyInOrder(scene, removed, deltaSeconds);
    for (const BehaviourLifecycleRecord& tracked : removed) {
        static_cast<void>(lifecycleRecords_.erase(MakeKey(tracked.entity, tracked.behaviour)));
    }
}

void ScriptRuntimeSceneSystem::ShutdownTrackedBehaviours(kb::scene::Scene& scene, float deltaSeconds) {
    SyncBehaviourLifecycles(scene, deltaSeconds);

    std::vector<BehaviourLifecycleRecord> remaining;
    remaining.reserve(lifecycleRecords_.size());
    for (const auto& [key, tracked] : lifecycleRecords_) {
        (void)key;
        remaining.push_back(tracked);
    }
    DispatchDeactivateAndDestroyInOrder(scene, remaining, deltaSeconds);
    lifecycleRecords_.clear();
}

// Shared by SyncBehaviourLifecycles (departing records) and
// ShutdownTrackedBehaviours (all tracked records): sorts in the guaranteed
// execution order (see kb::scene::BehaviourExecutionOrderLess), then
// dispatches Deactivated to every still-active record and Destroyed to
// every still-created one. Kept as the single place that pairs "sort in
// execution order" with "tear down a batch of records", so the two callers
// cannot drift back into unordered dispatch the way they did before this
// was extracted.
void ScriptRuntimeSceneSystem::DispatchDeactivateAndDestroyInOrder(
    kb::scene::Scene& scene,
    std::vector<BehaviourLifecycleRecord>& records,
    float deltaSeconds) {
    std::ranges::sort(records, [](const BehaviourLifecycleRecord& lhs, const BehaviourLifecycleRecord& rhs) {
        return kb::scene::BehaviourExecutionOrderLess(lhs.entity, lhs.behaviour, rhs.entity, rhs.behaviour);
    });
    for (const BehaviourLifecycleRecord& tracked : records) {
        if (tracked.active) {
            ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Deactivated, deltaSeconds);
        }
        if (tracked.created) {
            ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Destroyed, deltaSeconds);
        }
    }
}

void ScriptRuntimeSceneSystem::ExecuteBehaviourPhase(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const kb::scene::BehaviourComponent& behaviour,
    ScriptLifecycleEvent event,
    float deltaSeconds) {
    MergeResult(lastResult_, runtime_.ExecuteLifecycleForBehaviourAndDispatchEvents(scene, entity, behaviour, event, deltaSeconds));
}

std::vector<ScriptRuntimeSceneSystem::BehaviourLifecycleRecord> ScriptRuntimeSceneSystem::CollectBehaviourRecords(kb::scene::Scene& scene) const {
    std::vector<RawBehaviourRecord> rawRecords;
    BehaviourCollectContext context{
        .records = &rawRecords,
    };
    scene.Components().Behaviours().ForEach(&CollectBehaviour, &context);
    std::vector<BehaviourLifecycleRecord> records;
    records.reserve(rawRecords.size());
    for (const RawBehaviourRecord& raw : rawRecords) {
        records.push_back(BehaviourLifecycleRecord{
            .entity = raw.entity,
            .behaviour = raw.behaviour,
            .active = raw.behaviour.enabled,
            .created = false,
        });
    }
    std::ranges::sort(records, [](const BehaviourLifecycleRecord& lhs, const BehaviourLifecycleRecord& rhs) {
        return kb::scene::BehaviourExecutionOrderLess(lhs.entity, lhs.behaviour, rhs.entity, rhs.behaviour);
    });
    return records;
}

ScriptRuntimeSceneSystem::BehaviourLifecycleKey ScriptRuntimeSceneSystem::MakeKey(kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour) noexcept {
    return BehaviourLifecycleKey{
        .entityId = entity.Id(),
        .assetId = behaviour.behaviourAssetId,
        .backend = behaviour.backend,
    };
}

} // namespace kb::script
