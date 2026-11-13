#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/LuaScriptBackend.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/script/NativeScriptBackend.hpp"
#include "engine/script/NativeScriptBuildPipeline.hpp"
#include "engine/script/NativeScriptPluginManager.hpp"
#include "engine/visual/VisualGraphNativeBuildPipeline.hpp"
#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <cstddef>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::script {

struct ScriptRuntimeAssetPrepareDiagnostic {
    kb::assets::AssetId assetId{};
    kb::scene::BehaviourBackend backend = kb::scene::BehaviourBackend::Native;
    std::string message;
};

struct ScriptRuntimeAssetPrepareResult {
    std::size_t visitedAssets = 0;
    std::size_t preparedAssets = 0;
    // Native assets whose build is still running in the background: their previous module (if any) keeps
    // running, and a later pass applies the new one once the build ends.
    std::size_t buildingAssets = 0;
    // Assets whose live implementation was successfully replaced during this
    // preparation pass. The scene system restarts only those behaviours at a
    // safe lifecycle synchronization boundary.
    std::vector<kb::assets::AssetId> reloadedAssets;
    std::vector<ScriptRuntimeAssetPrepareDiagnostic> diagnostics;
    // Things worth telling the author that do not stop the script from running.
    std::vector<ScriptRuntimeAssetPrepareDiagnostic> warnings;

    [[nodiscard]] bool Succeeded() const noexcept {
        return diagnostics.empty();
    }
};

struct ScriptRuntimeVisualGraphPrepareSettings {
    std::string generatedClassNamespace = "kb::generated";
    std::filesystem::path generatedCodeDirectory;
    const kb::visual::VisualGraphNativeBindingRegistry* nativeBindings = nullptr;
    kb::visual::VisualGraphNativeBuildDesc nativeBuild;
    bool writeGeneratedCode = false;
    bool requireNativeBindings = false;
};

struct ScriptRuntimeNativePrepareSettings {
    std::filesystem::path shadowCopyDirectory;
    bool buildPlugins = true;
    bool loadPlugins = true;
    std::filesystem::path runtimeModuleRoot;
    // A native script that needs a build is built on its own thread and process while the caller goes on.
    // The module that is loaded meanwhile stays in use until the new one is ready. A script with no module
    // yet is built in the background only when backgroundFirstBuild is set (nothing runs it yet, as in the
    // editor outside Play); otherwise the caller waits, because its first lifecycle calls need the module.
    bool buildInBackground = false;
    bool backgroundFirstBuild = false;
};

// One native build running in the background.
struct ScriptRuntimeNativeBuildStatus {
    kb::assets::AssetId assetId{};
    std::string name;
    NativeScriptBuildProgress progress;
};

class ScriptRuntimeAssetPreparer final {
public:
    ScriptRuntimeAssetPreparer(
        kb::assets::AssetManager& assets,
        ILuaScriptAssetStore& luaScripts,
        kb::visual::VisualGraphRuntimeRegistry& visualGraphs) noexcept;

    void SetVisualGraphSettings(ScriptRuntimeVisualGraphPrepareSettings settings);
    void SetNativeBackend(NativeScriptBackend& nativeBackend) noexcept;
    void SetNativePluginManager(NativeScriptPluginManager& pluginManager) noexcept;
    void SetNativeSettings(ScriptRuntimeNativePrepareSettings settings);
    void InvalidateNativeSourceObservations() noexcept;
    // Changes how native builds run without touching the other native settings.
    void SetNativeBuildMode(bool inBackground, bool firstBuildInBackground) noexcept;
    [[nodiscard]] std::vector<ScriptRuntimeNativeBuildStatus> NativeBuildsInProgress() const;
    // Stops the running build of `assetId`; the script is not rebuilt until its sources change or the
    // observations are invalidated, and the console is told that the build was cancelled.
    bool CancelNativeBuild(kb::assets::AssetId assetId);
    // Waits for the builds that run in the background and applies their modules; for the moment the
    // caller cannot go on with the old ones (Play starts).
    [[nodiscard]] ScriptRuntimeAssetPrepareResult FinishNativeBuilds();

    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareAsset(kb::assets::AssetId assetId);
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareBehaviour(const kb::scene::BehaviourComponent& behaviour);
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareSceneBehaviours(kb::scene::Scene& scene);
    // The same pass for the native scripts of the scene only: the editor keeps them built and loaded
    // while nothing plays.
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareNativeSceneBehaviours(kb::scene::Scene& scene);

private:
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareLuaAsset(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareLuaImports(const kb::assets::AssetMetadata& owner, const LuaScriptAsset& asset);
    [[nodiscard]] bool LuaImportsCurrent(const kb::assets::AssetMetadata& owner, const LuaScriptAsset& asset) const;
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareLuaImportsRecursive(
        const kb::assets::AssetMetadata& owner,
        const LuaScriptAsset& asset,
        std::unordered_set<std::uint64_t>& visiting);
    [[nodiscard]] bool LuaImportsCurrentRecursive(
        const kb::assets::AssetMetadata& owner,
        const LuaScriptAsset& asset,
        std::unordered_set<std::uint64_t>& visited) const;
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareNativeBehaviourAsset(const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareVisualGraphAsset(const kb::assets::AssetMetadata& metadata);

    static void AddDiagnostic(
        ScriptRuntimeAssetPrepareResult& result,
        kb::assets::AssetId assetId,
        kb::scene::BehaviourBackend backend,
        std::string message);
    [[nodiscard]] static std::string GeneratedClassName(const kb::assets::AssetMetadata& metadata);

    kb::assets::AssetManager& assets_;
    ILuaScriptAssetStore& luaScripts_;
    kb::visual::VisualGraphRuntimeRegistry& visualGraphs_;
    NativeScriptBackend* nativeBackend_ = nullptr;
    NativeScriptPluginManager* nativePlugins_ = nullptr;
    ScriptRuntimeVisualGraphPrepareSettings visualGraphSettings_{};
    ScriptRuntimeNativePrepareSettings nativeSettings_{};
    std::unordered_map<std::uint64_t, std::uint64_t> preparedNativeAssetHashes_;
    // Native scripts the author was told keep no data across a rebuild; told again only after they had a Serialize.
    std::unordered_set<std::uint64_t> unsavedStateWarned_;
    struct NativeSourceObservation {
        std::uint64_t descriptorHash = 0;
        std::uint64_t signature = 0;
        std::chrono::steady_clock::time_point nextCheck{};
    };
    std::unordered_map<std::uint64_t, NativeSourceObservation> nativeSourceObservations_;
    std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::string>> failedNativeAssetBuilds_;
    struct NativeBuildRecord {
        std::uint64_t signature = 0;
        std::string name;
        std::shared_ptr<NativeScriptBuildJob> job;
    };
    std::unordered_map<std::uint64_t, NativeBuildRecord> nativeBuildJobs_;
    std::unordered_map<std::uint64_t, std::uint64_t> preparedVisualGraphAssetHashes_;
};

} // namespace kb::script
