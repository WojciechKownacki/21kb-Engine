#pragma once

#include "engine/scene/SceneSystem.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeAssetPreparer.hpp"

namespace kb::script {

class ScriptRuntimeSceneSystem final : public kb::scene::SceneSystem {
public:
    explicit ScriptRuntimeSceneSystem(ScriptRuntime& runtime) noexcept;
    ScriptRuntimeSceneSystem(ScriptRuntime& runtime, ScriptRuntimeAssetPreparer& assetPreparer) noexcept;

    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;

    [[nodiscard]] const ScriptRuntimeExecutionResult& LastResult() const noexcept;
    [[nodiscard]] const ScriptRuntimeAssetPrepareResult& LastPrepareResult() const noexcept;

private:
    void PrepareScene(kb::scene::Scene& scene);

    ScriptRuntime& runtime_;
    ScriptRuntimeAssetPreparer* assetPreparer_ = nullptr;
    ScriptRuntimeExecutionResult lastResult_;
    ScriptRuntimeAssetPrepareResult lastPrepareResult_;
};

} // namespace kb::script
