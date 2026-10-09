#pragma once

#include "engine/library/EngineLibraryModule.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneSystemHandle.hpp"
#include "engine/script/LuaScriptBackend.hpp"
#include "engine/script/NativeScriptBackend.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptApiNameRegistry.hpp"
#include "engine/script/ScriptExecutionBudget.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeAssetPreparer.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"
#include "engine/script/VisualGraphScriptBackend.hpp"
#include "engine/visual/VisualGraphBehaviourInstanceRegistry.hpp"
#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"
#include "engine/visual/VisualGraphNodeCatalog.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace kb::platform {
class UserStorage;
}

namespace kb::script {

// Default size cap for everything scripts persist through Save.Write and
// Settings.Write under one runtime's user storage root.
inline constexpr std::uintmax_t kDefaultScriptUserStorageQuotaBytes = 64ULL << 20U;

struct ScriptRuntimeHostState;

struct ScriptRuntimeHostOptions {
    ScriptRuntimeVisualGraphPrepareSettings visualGraphPrepareSettings{};
    ScriptRuntimeNativePrepareSettings nativePrepareSettings{};
    ScriptRuntimeFrameSettings frameSettings{};
    ScriptExecutionBudgetSettings executionBudgetSettings{};
    // The per-game user directory scripts persist into. Save.Write/Read and
    // Settings.Write/Read address it only by slot name; when it is empty they
    // report that persistent storage is not configured.
    std::filesystem::path userStorageRoot;
    std::uintmax_t userStorageQuotaBytes = kDefaultScriptUserStorageQuotaBytes;
    // A behaviour whose script reports an error is disabled (its
    // BehaviourComponent::enabled is cleared) so one faulty script instance
    // stops instead of failing every frame while the rest of the scene runs.
    // Shipped game hosts enable it; the editor keeps the behaviour enabled so
    // a fixed script hot-reloads into it.
    bool disableFailingBehaviours = false;
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
    // Per-frame script diagnostics (compile/behaviour errors) captured while the
    // installed scene system ran, drained (and cleared) for the host to surface.
    [[nodiscard]] std::vector<std::string> DrainSceneSystemDiagnostics();
    // Read-only frame telemetry from the scheduler-owned scene system. The
    // pointers remain valid until the next scene-system phase runs or the
    // scene system is destroyed.
    [[nodiscard]] const ScriptRuntimeExecutionResult* InstalledSceneSystemLastResult() const noexcept;
    [[nodiscard]] const ScriptRuntimeAssetPrepareResult* InstalledSceneSystemLastPrepareResult() const noexcept;
    // LIB-028: the kb::library startup report (one entry per catalog
    // module: installed/disabled, version, reason) produced by the real
    // EngineLibraryModule::Install() call this host made while
    // constructing — not recomputed/guessed, the actual outcome.
    [[nodiscard]] const std::vector<kb::library::EngineLibraryModuleReportEntry>& LibraryStartupReport() const noexcept;

    [[nodiscard]] bool InstallSceneSystem();
    // Drives the installed script scene system's shutdown lifecycle NOW — fires
    // Deactivated + Destroyed on every tracked behaviour and clears tracking,
    // without removing the system. The editor calls this when play stops so a
    // behaviour gets its Destroyed (OnDestroy-equivalent) hook before the scene
    // snapshot is restored. Returns false if no scene system is installed.
    bool DispatchShutdownLifecycle(float deltaSeconds);
    [[nodiscard]] kb::visual::VisualGraphNodeCatalog CreateVisualGraphNodeCatalog() const;

    [[nodiscard]] ScriptRuntime& Runtime() noexcept;
    [[nodiscard]] const ScriptRuntime& Runtime() const noexcept;
    [[nodiscard]] ScriptSharedState& SharedState() noexcept;
    [[nodiscard]] const ScriptSharedState& SharedState() const noexcept;
    [[nodiscard]] ScriptFunctionRegistry& Functions() noexcept;
    [[nodiscard]] const ScriptFunctionRegistry& Functions() const noexcept;
    [[nodiscard]] ScriptApiNameRegistry& ApiNames() noexcept;
    [[nodiscard]] const ScriptApiNameRegistry& ApiNames() const noexcept;
    [[nodiscard]] bool RegisterFunction(ScriptFunctionDesc function);
    // The sandboxed storage built from ScriptRuntimeHostOptions::userStorageRoot,
    // or null when the host configured none.
    [[nodiscard]] std::shared_ptr<kb::platform::UserStorage> UserStorage() const noexcept;
    [[nodiscard]] ScriptRuntimeAssetPreparer& AssetPreparer() noexcept;
    [[nodiscard]] const ScriptRuntimeAssetPreparer& AssetPreparer() const noexcept;
    [[nodiscard]] PucLuaScriptRuntime& LuaRuntime() noexcept;
    [[nodiscard]] const PucLuaScriptRuntime& LuaRuntime() const noexcept;
    [[nodiscard]] NativeScriptBackend& NativeBackend() noexcept;
    [[nodiscard]] const NativeScriptBackend& NativeBackend() const noexcept;
    [[nodiscard]] NativeScriptPluginManager& NativePlugins() noexcept;
    [[nodiscard]] const NativeScriptPluginManager& NativePlugins() const noexcept;
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
    [[nodiscard]] kb::visual::VisualGraphDebugSession& VisualGraphDebugger() noexcept;
    [[nodiscard]] const kb::visual::VisualGraphDebugSession& VisualGraphDebugger() const noexcept;

private:
    void RegisterDefaultBackends();
    void AddDiagnostic(std::string message);

    std::shared_ptr<ScriptRuntimeHostState> state_;
    kb::scene::SceneSystemHandle sceneSystemHandle_{};
    bool sceneSystemInstalled_ = false;
    std::vector<std::string> diagnostics_;
    std::vector<kb::library::EngineLibraryModuleReportEntry> libraryStartupReport_;
};

} // namespace kb::script
