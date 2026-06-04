#include "engine/script/ScriptRuntimeAssetPreparer.hpp"

#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/visual/VisualGraphCompileService.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

namespace kb::script {
namespace {

struct ScenePrepareContext {
    ScriptRuntimeAssetPreparer* preparer = nullptr;
    ScriptRuntimeAssetPrepareResult* result = nullptr;
};

void MergePrepareResult(ScriptRuntimeAssetPrepareResult& target, ScriptRuntimeAssetPrepareResult source) {
    target.visitedAssets += source.visitedAssets;
    target.preparedAssets += source.preparedAssets;
    target.diagnostics.reserve(target.diagnostics.size() + source.diagnostics.size());
    for (ScriptRuntimeAssetPrepareDiagnostic& diagnostic : source.diagnostics) {
        target.diagnostics.push_back(std::move(diagnostic));
    }
}

void PrepareBehaviourVisitor(kb::scene::SceneEntity, const kb::scene::BehaviourComponent& behaviour, void* rawContext) {
    auto& context = *static_cast<ScenePrepareContext*>(rawContext);
    ScriptRuntimeAssetPrepareResult prepared = context.preparer->PrepareBehaviour(behaviour);
    MergePrepareResult(*context.result, std::move(prepared));
}

[[nodiscard]] char IdentifierChar(char value) noexcept {
    const auto character = static_cast<unsigned char>(value);
    return std::isalnum(character) != 0 ? static_cast<char>(character) : '_';
}

} // namespace

ScriptRuntimeAssetPreparer::ScriptRuntimeAssetPreparer(
    kb::assets::AssetManager& assets,
    ILuaScriptAssetStore& luaScripts,
    kb::visual::VisualGraphRuntimeRegistry& visualGraphs) noexcept
    : assets_(assets)
    , luaScripts_(luaScripts)
    , visualGraphs_(visualGraphs) {}

void ScriptRuntimeAssetPreparer::SetVisualGraphSettings(ScriptRuntimeVisualGraphPrepareSettings settings) {
    visualGraphSettings_ = std::move(settings);
}

void ScriptRuntimeAssetPreparer::SetNativeBackend(NativeScriptBackend& nativeBackend) noexcept {
    nativeBackend_ = &nativeBackend;
}

ScriptRuntimeAssetPrepareResult ScriptRuntimeAssetPreparer::PrepareAsset(kb::assets::AssetId assetId) {
    ScriptRuntimeAssetPrepareResult result{};
    ++result.visitedAssets;
    if (!assetId.IsValid()) {
        AddDiagnostic(result, assetId, kb::scene::BehaviourBackend::Native, "script runtime prepare request has invalid asset id");
        return result;
    }

    const kb::assets::AssetMetadata* metadata = assets_.Registry().Find(assetId);
    if (metadata == nullptr) {
        AddDiagnostic(result, assetId, kb::scene::BehaviourBackend::Native, "script runtime prepare asset is not registered");
        return result;
    }

    const std::optional<kb::scene::BehaviourBackend> backend = ScriptBehaviourAsset::BackendForAssetType(metadata->type);
    if (!backend.has_value()) {
        AddDiagnostic(result, assetId, kb::scene::BehaviourBackend::Native, "asset is not a script behaviour asset");
        return result;
    }

    switch (*backend) {
    case kb::scene::BehaviourBackend::Lua:
        return PrepareLuaAsset(*metadata);
    case kb::scene::BehaviourBackend::Native:
        return PrepareNativeBehaviourAsset(*metadata);
    case kb::scene::BehaviourBackend::VisualGraph:
        return PrepareVisualGraphAsset(*metadata);
    default:
        AddDiagnostic(result, assetId, *backend, "unsupported script behaviour backend");
        return result;
    }
}

ScriptRuntimeAssetPrepareResult ScriptRuntimeAssetPreparer::PrepareBehaviour(const kb::scene::BehaviourComponent& behaviour) {
    if (!behaviour.enabled) {
        ScriptRuntimeAssetPrepareResult result{};
        ++result.visitedAssets;
        return result;
    }
    return PrepareAsset(kb::assets::AssetId{ behaviour.behaviourAssetId });
}

ScriptRuntimeAssetPrepareResult ScriptRuntimeAssetPreparer::PrepareSceneBehaviours(kb::scene::Scene& scene) {
    ScriptRuntimeAssetPrepareResult result{};
    ScenePrepareContext context{
        .preparer = this,
        .result = &result,
    };
    scene.Components().Behaviours().ForEach(&PrepareBehaviourVisitor, &context);
    return result;
}

ScriptRuntimeAssetPrepareResult ScriptRuntimeAssetPreparer::PrepareLuaAsset(const kb::assets::AssetMetadata& metadata) {
    ScriptRuntimeAssetPrepareResult result{};
    ++result.visitedAssets;
    if (luaScripts_.HasScript(metadata.id)) {
        ++result.preparedAssets;
        return result;
    }

    const kb::assets::AssetHandle<LuaScriptAsset> asset = assets_.Load<LuaScriptAsset>(metadata.id);
    if (!asset.IsLoaded()) {
        AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::Lua, assets_.LastError().empty() ? "Lua script asset could not be loaded" : assets_.LastError());
        return result;
    }

    const LuaScriptLoadResult loaded = luaScripts_.LoadScript(metadata.id, asset->source, kb::assets::NormalizeAssetPath(metadata.virtualPath));
    if (!loaded.succeeded) {
        AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::Lua, loaded.error.empty() ? "Lua script could not be prepared" : loaded.error);
        return result;
    }

    ++result.preparedAssets;
    return result;
}

ScriptRuntimeAssetPrepareResult ScriptRuntimeAssetPreparer::PrepareNativeBehaviourAsset(const kb::assets::AssetMetadata& metadata) {
    ScriptRuntimeAssetPrepareResult result{};
    ++result.visitedAssets;
    const kb::assets::AssetHandle<NativeBehaviourDescriptor> descriptor = assets_.Load<NativeBehaviourDescriptor>(metadata.id);
    if (!descriptor.IsLoaded()) {
        AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::Native, assets_.LastError().empty() ? "native behaviour descriptor could not be loaded" : assets_.LastError());
        return result;
    }
    if (nativeBackend_ != nullptr && !nativeBackend_->BindAssetSymbol(metadata.id, descriptor->symbol)) {
        AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::Native, "native behaviour descriptor symbol could not be bound");
        return result;
    }
    ++result.preparedAssets;
    return result;
}

ScriptRuntimeAssetPrepareResult ScriptRuntimeAssetPreparer::PrepareVisualGraphAsset(const kb::assets::AssetMetadata& metadata) {
    ScriptRuntimeAssetPrepareResult result{};
    ++result.visitedAssets;
    if (visualGraphs_.Contains(metadata.id)) {
        ++result.preparedAssets;
        return result;
    }

    kb::visual::VisualGraphCompileServiceResult compiled = kb::visual::VisualGraphCompileService::CompileAsset(
        assets_,
        metadata.id,
        kb::visual::VisualGraphCompileRequest{
            .build = kb::visual::VisualGraphBuildDesc{
                .nativeCodegen = kb::visual::VisualGraphNativeCodegenDesc{
                    .className = GeneratedClassName(metadata),
                    .namespaceName = visualGraphSettings_.generatedClassNamespace,
                    .bindings = visualGraphSettings_.nativeBindings,
                    .requireNativeBindings = visualGraphSettings_.requireNativeBindings,
                },
            },
            .writeGeneratedCode = false,
        },
        visualGraphs_);
    if (!compiled.Succeeded()) {
        std::string message = compiled.errors.empty() ? "visual graph asset could not be compiled" : compiled.errors.front();
        AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::VisualGraph, std::move(message));
        return result;
    }

    ++result.preparedAssets;
    return result;
}

void ScriptRuntimeAssetPreparer::AddDiagnostic(
    ScriptRuntimeAssetPrepareResult& result,
    kb::assets::AssetId assetId,
    kb::scene::BehaviourBackend backend,
    std::string message) {
    result.diagnostics.push_back(ScriptRuntimeAssetPrepareDiagnostic{
        .assetId = assetId,
        .backend = backend,
        .message = std::move(message),
    });
}

std::string ScriptRuntimeAssetPreparer::GeneratedClassName(const kb::assets::AssetMetadata& metadata) {
    std::string className = metadata.name.empty() ? metadata.virtualPath.stem().string() : metadata.name;
    std::ranges::transform(className, className.begin(), &IdentifierChar);
    if (className.empty()) {
        return "GeneratedVisualGraph";
    }
    if (std::isdigit(static_cast<unsigned char>(className.front())) != 0) {
        className.insert(className.begin(), '_');
    }
    return className;
}

} // namespace kb::script
