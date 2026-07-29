#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/LuaScriptBackend.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/script/NativeScriptBackend.hpp"
#include "engine/script/NativeScriptPluginManager.hpp"
#include "engine/visual/VisualGraphNativeBuildPipeline.hpp"
#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    // Assets whose live implementation was successfully replaced during this
    // preparation pass. The scene system restarts only those behaviours at a
    // safe lifecycle synchronization boundary.
    std::vector<kb::assets::AssetId> reloadedAssets;
    std::vector<ScriptRuntimeAssetPrepareDiagnostic> diagnostics;

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

    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareAsset(kb::assets::AssetId assetId);
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareBehaviour(const kb::scene::BehaviourComponent& behaviour);
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareSceneBehaviours(kb::scene::Scene& scene);

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
    std::unordered_map<std::uint64_t, std::uint64_t> preparedVisualGraphAssetHashes_;
};

} // namespace kb::script
