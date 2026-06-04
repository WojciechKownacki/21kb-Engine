#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/LuaScriptBackend.hpp"
#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <cstddef>
#include <string>
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
    std::vector<ScriptRuntimeAssetPrepareDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return diagnostics.empty();
    }
};

struct ScriptRuntimeVisualGraphPrepareSettings {
    std::string generatedClassNamespace = "kb::generated";
    const kb::visual::VisualGraphNativeBindingRegistry* nativeBindings = nullptr;
    bool requireNativeBindings = false;
};

class ScriptRuntimeAssetPreparer final {
public:
    ScriptRuntimeAssetPreparer(
        kb::assets::AssetManager& assets,
        ILuaScriptAssetStore& luaScripts,
        kb::visual::VisualGraphRuntimeRegistry& visualGraphs) noexcept;

    void SetVisualGraphSettings(ScriptRuntimeVisualGraphPrepareSettings settings);

    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareAsset(kb::assets::AssetId assetId);
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareBehaviour(const kb::scene::BehaviourComponent& behaviour);
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareSceneBehaviours(kb::scene::Scene& scene);

private:
    [[nodiscard]] ScriptRuntimeAssetPrepareResult PrepareLuaAsset(const kb::assets::AssetMetadata& metadata);
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
    ScriptRuntimeVisualGraphPrepareSettings visualGraphSettings_{};
};

} // namespace kb::script
