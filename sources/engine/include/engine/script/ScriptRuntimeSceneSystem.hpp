#pragma once

#include "engine/scene/SceneSystem.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeAssetPreparer.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kb::script {

struct ScriptRuntimeFrameSettings {
    float fixedDeltaSeconds = 1.0F / 60.0F;
    std::size_t maxFixedStepsPerFrame = 64U;
};

class ScriptRuntimeSceneSystem final : public kb::scene::SceneSystem {
public:
    explicit ScriptRuntimeSceneSystem(ScriptRuntime& runtime) noexcept;
    ScriptRuntimeSceneSystem(ScriptRuntime& runtime, ScriptRuntimeAssetPreparer& assetPreparer) noexcept;

    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;

    [[nodiscard]] const ScriptRuntimeExecutionResult& ExecuteStartup(kb::scene::Scene& scene, float deltaSeconds);
    [[nodiscard]] const ScriptRuntimeExecutionResult& ExecuteFrame(kb::scene::Scene& scene, float deltaSeconds);
    [[nodiscard]] const ScriptRuntimeExecutionResult& ExecuteShutdown(kb::scene::Scene& scene, float deltaSeconds);
    [[nodiscard]] const ScriptRuntimeExecutionResult& ExecutePhase(kb::scene::Scene& scene, ScriptLifecycleEvent event, float deltaSeconds);

    void SetFrameSettings(ScriptRuntimeFrameSettings settings) noexcept;
    [[nodiscard]] ScriptRuntimeFrameSettings FrameSettings() const noexcept;

    [[nodiscard]] const ScriptRuntimeExecutionResult& LastResult() const noexcept;
    [[nodiscard]] const ScriptRuntimeAssetPrepareResult& LastPrepareResult() const noexcept;

private:
    struct BehaviourLifecycleKey {
        std::uint64_t entityId = 0;
        std::uint64_t assetId = 0;
        kb::scene::BehaviourBackend backend = kb::scene::BehaviourBackend::Native;

        [[nodiscard]] friend constexpr bool operator==(BehaviourLifecycleKey lhs, BehaviourLifecycleKey rhs) noexcept = default;
    };

    struct BehaviourLifecycleKeyHasher {
        [[nodiscard]] std::size_t operator()(BehaviourLifecycleKey key) const noexcept;
    };

    struct BehaviourLifecycleRecord {
        kb::scene::SceneEntity entity{};
        kb::scene::BehaviourComponent behaviour{};
        bool active = false;
        bool created = false;
    };

    void PrepareScene(kb::scene::Scene& scene);
    void SyncBehaviourLifecycles(kb::scene::Scene& scene, float deltaSeconds);
    void ShutdownTrackedBehaviours(kb::scene::Scene& scene, float deltaSeconds);
    void DispatchDeactivateAndDestroyInOrder(kb::scene::Scene& scene, std::vector<BehaviourLifecycleRecord>& records, float deltaSeconds);
    void ExecuteBehaviourPhase(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptLifecycleEvent event,
        float deltaSeconds);
    [[nodiscard]] std::vector<BehaviourLifecycleRecord> CollectBehaviourRecords(kb::scene::Scene& scene) const;
    [[nodiscard]] static BehaviourLifecycleKey MakeKey(kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour) noexcept;

    ScriptRuntime& runtime_;
    ScriptRuntimeAssetPreparer* assetPreparer_ = nullptr;
    ScriptRuntimeFrameSettings frameSettings_{};
    float fixedAccumulatorSeconds_ = 0.0F;
    ScriptRuntimeExecutionResult lastResult_;
    ScriptRuntimeAssetPrepareResult lastPrepareResult_;
    std::unordered_map<BehaviourLifecycleKey, BehaviourLifecycleRecord, BehaviourLifecycleKeyHasher> lifecycleRecords_;
};

} // namespace kb::script
