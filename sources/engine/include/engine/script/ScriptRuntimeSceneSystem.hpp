#pragma once

#include "engine/scene/SceneSystem.hpp"
#include "engine/script/ScriptRuntime.hpp"

namespace kb::script {

class ScriptRuntimeSceneSystem final : public kb::scene::SceneSystem {
public:
    explicit ScriptRuntimeSceneSystem(ScriptRuntime& runtime) noexcept;

    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;

    [[nodiscard]] const ScriptRuntimeExecutionResult& LastResult() const noexcept;

private:
    ScriptRuntime& runtime_;
    ScriptRuntimeExecutionResult lastResult_;
};

} // namespace kb::script
