#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/script/LuaScriptBackend.hpp"
#include "engine/script/NativeScriptBackend.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeAssetPreparer.hpp"
#include "engine/script/VisualGraphScriptBackend.hpp"
#include "engine/visual/VisualGraphBehaviourInstanceRegistry.hpp"
#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <memory>
#include <string>
#include <vector>

namespace kb::script {

struct ScriptRuntimeHostState;

struct ScriptRuntimeHostOptions {
    ScriptRuntimeVisualGraphPrepareSettings visualGraphPrepareSettings{};
    bool installSceneSystem = false;
};

class ScriptRuntimeHost final {
public:
    explicit ScriptRuntimeHost(kb::scene::Scene& scene, ScriptRuntimeHostOptions options = {});
    ~ScriptRuntimeHost();

    ScriptRuntimeHost(const ScriptRuntimeHost&) = delete;
    ScriptRuntimeHost& operator=(const ScriptRuntimeHost&) = delete;
    ScriptRuntimeHost(ScriptRuntimeHost&&) = delete;
    ScriptRuntimeHost& operator=(ScriptRuntimeHost&&) = delete;

    [[nodiscard]] bool Succeeded() const noexcept;
    [[nodiscard]] const std::vector<std::string>& Diagnostics() const noexcept;

    [[nodiscard]] bool InstallSceneSystem();

    [[nodiscard]] ScriptRuntime& Runtime() noexcept;
    [[nodiscard]] const ScriptRuntime& Runtime() const noexcept;
    [[nodiscard]] ScriptRuntimeAssetPreparer& AssetPreparer() noexcept;
    [[nodiscard]] const ScriptRuntimeAssetPreparer& AssetPreparer() const noexcept;
    [[nodiscard]] PucLuaScriptRuntime& LuaRuntime() noexcept;
    [[nodiscard]] const PucLuaScriptRuntime& LuaRuntime() const noexcept;
    [[nodiscard]] NativeScriptBackend& NativeBackend() noexcept;
    [[nodiscard]] const NativeScriptBackend& NativeBackend() const noexcept;
    [[nodiscard]] VisualGraphScriptBackend& VisualGraphBackend() noexcept;
    [[nodiscard]] const VisualGraphScriptBackend& VisualGraphBackend() const noexcept;
    [[nodiscard]] kb::visual::VisualGraphRuntimeRegistry& VisualGraphs() noexcept;
    [[nodiscard]] const kb::visual::VisualGraphRuntimeRegistry& VisualGraphs() const noexcept;
    [[nodiscard]] kb::visual::VisualGraphRuntimeBindingRegistry& VisualGraphRuntimeBindings() noexcept;
    [[nodiscard]] const kb::visual::VisualGraphRuntimeBindingRegistry& VisualGraphRuntimeBindings() const noexcept;
    [[nodiscard]] kb::visual::VisualGraphNativeBindingRegistry& VisualGraphNativeBindings() noexcept;
    [[nodiscard]] const kb::visual::VisualGraphNativeBindingRegistry& VisualGraphNativeBindings() const noexcept;
    [[nodiscard]] kb::visual::VisualGraphBehaviourInstanceRegistry& VisualGraphInstances() noexcept;
    [[nodiscard]] const kb::visual::VisualGraphBehaviourInstanceRegistry& VisualGraphInstances() const noexcept;

private:
    void RegisterDefaultBackends();
    void AddDiagnostic(std::string message);

    std::shared_ptr<ScriptRuntimeHostState> state_;
    bool sceneSystemInstalled_ = false;
    std::vector<std::string> diagnostics_;
};

} // namespace kb::script
