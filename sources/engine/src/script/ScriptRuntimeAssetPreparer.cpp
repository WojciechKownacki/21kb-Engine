#include "engine/script/ScriptRuntimeAssetPreparer.hpp"

#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/script/NativeScriptBuildPipeline.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/visual/VisualGraphCompileCoordinator.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace kb::script {
namespace {

struct ScenePrepareContext {
    ScriptRuntimeAssetPreparer* preparer = nullptr;
    ScriptRuntimeAssetPrepareResult* result = nullptr;
    std::unordered_set<std::uint64_t> visitedAssetIds;
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
    if (!behaviour.enabled) {
        return;
    }
    const kb::assets::AssetId assetId{ behaviour.behaviourAssetId };
    if (!context.visitedAssetIds.insert(assetId.value).second) {
        return;
    }
    ScriptRuntimeAssetPrepareResult prepared = context.preparer->PrepareAsset(assetId);
    MergePrepareResult(*context.result, std::move(prepared));
}

[[nodiscard]] char IdentifierChar(char value) noexcept {
    const auto character = static_cast<unsigned char>(value);
    return std::isalnum(character) != 0 ? static_cast<char>(character) : '_';
}

[[nodiscard]] std::filesystem::path LuaModuleVirtualPath(const kb::assets::AssetMetadata& owner, std::string_view importName) {
    std::string modulePath{ importName };
    std::ranges::replace(modulePath, '.', '/');
    std::filesystem::path candidate = owner.virtualPath.parent_path() / (modulePath + ".lua");
    if (modulePath.starts_with("/")) {
        candidate = std::filesystem::path{ modulePath + ".lua" };
    }
    return candidate;
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

void ScriptRuntimeAssetPreparer::SetNativePluginManager(NativeScriptPluginManager& pluginManager) noexcept {
    nativePlugins_ = &pluginManager;
}

void ScriptRuntimeAssetPreparer::SetNativeSettings(ScriptRuntimeNativePrepareSettings settings) {
    nativeSettings_ = std::move(settings);
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
    const kb::assets::AssetHandle<LuaScriptAsset> asset = assets_.Load<LuaScriptAsset>(metadata.id);
    if (!asset.IsLoaded()) {
        AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::Lua, assets_.LastError().empty() ? "Lua script asset could not be loaded" : assets_.LastError());
        return result;
    }

    PucLuaScriptRuntime* runtime = dynamic_cast<PucLuaScriptRuntime*>(&luaScripts_);
    if (runtime != nullptr) {
        runtime->SetScriptExposedVariables(metadata.id, asset->exposedVariables, asset->exposedVariableDefaults, asset->exposedVariableHasDefault);
    }

    const bool scriptCurrent = runtime != nullptr
        ? runtime->IsScriptCurrent(metadata.id, metadata.contentHash)
        : luaScripts_.HasScript(metadata.id);
    const bool importsCurrent = LuaImportsCurrent(metadata, *asset.Get());
    if (scriptCurrent && importsCurrent) {
        ++result.preparedAssets;
        return result;
    }

    MergePrepareResult(result, PrepareLuaImports(metadata, *asset.Get()));
    if (!result.Succeeded()) {
        return result;
    }

    const std::string chunkName = kb::assets::NormalizeAssetPath(metadata.virtualPath);
    const LuaScriptLoadResult loaded = dynamic_cast<PucLuaScriptRuntime*>(&luaScripts_) == nullptr
        ? luaScripts_.LoadScript(metadata.id, asset->source, chunkName)
        : runtime->LoadScript(metadata.id, asset->source, chunkName, metadata.contentHash);
    if (!loaded.succeeded) {
        AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::Lua, loaded.error.empty() ? "Lua script could not be prepared" : loaded.error);
        return result;
    }

    ++result.preparedAssets;
    return result;
}

bool ScriptRuntimeAssetPreparer::LuaImportsCurrent(const kb::assets::AssetMetadata& owner, const LuaScriptAsset& asset) const {
    std::unordered_set<std::uint64_t> visited;
    return LuaImportsCurrentRecursive(owner, asset, visited);
}

bool ScriptRuntimeAssetPreparer::LuaImportsCurrentRecursive(
    const kb::assets::AssetMetadata& owner,
    const LuaScriptAsset& asset,
    std::unordered_set<std::uint64_t>& visited) const {
    const PucLuaScriptRuntime* runtime = dynamic_cast<const PucLuaScriptRuntime*>(&luaScripts_);
    if (runtime == nullptr || asset.imports.empty()) {
        return true;
    }
    for (const std::string& importName : asset.imports) {
        const std::filesystem::path moduleVirtualPath = LuaModuleVirtualPath(owner, importName);
        const kb::assets::AssetMetadata* moduleMetadata = assets_.Registry().FindByPath(moduleVirtualPath);
        if (moduleMetadata == nullptr || !runtime->IsModuleCurrent(importName, moduleMetadata->contentHash)) {
            return false;
        }
        if (!visited.insert(moduleMetadata->id.value).second) {
            continue;
        }
        const kb::assets::AssetHandle<LuaScriptAsset> module = assets_.Load<LuaScriptAsset>(moduleMetadata->id);
        if (!module.IsLoaded() || !LuaImportsCurrentRecursive(*moduleMetadata, *module.Get(), visited)) {
            return false;
        }
    }
    return true;
}

ScriptRuntimeAssetPrepareResult ScriptRuntimeAssetPreparer::PrepareLuaImports(const kb::assets::AssetMetadata& owner, const LuaScriptAsset& asset) {
    std::unordered_set<std::uint64_t> visiting;
    visiting.insert(owner.id.value);
    return PrepareLuaImportsRecursive(owner, asset, visiting);
}

ScriptRuntimeAssetPrepareResult ScriptRuntimeAssetPreparer::PrepareLuaImportsRecursive(
    const kb::assets::AssetMetadata& owner,
    const LuaScriptAsset& asset,
    std::unordered_set<std::uint64_t>& visiting) {
    ScriptRuntimeAssetPrepareResult result{};
    PucLuaScriptRuntime* runtime = dynamic_cast<PucLuaScriptRuntime*>(&luaScripts_);
    if (runtime == nullptr || asset.imports.empty()) {
        return result;
    }

    for (const std::string& importName : asset.imports) {
        const std::filesystem::path moduleVirtualPath = LuaModuleVirtualPath(owner, importName);
        const kb::assets::AssetMetadata* moduleMetadata = assets_.Registry().FindByPath(moduleVirtualPath);
        if (moduleMetadata == nullptr) {
            AddDiagnostic(result, owner.id, kb::scene::BehaviourBackend::Lua, "Lua import module asset could not be found: " + importName);
            continue;
        }
        if (!visiting.insert(moduleMetadata->id.value).second) {
            AddDiagnostic(result, moduleMetadata->id, kb::scene::BehaviourBackend::Lua, "Lua import cycle detected: " + importName);
            continue;
        }
        const kb::assets::AssetHandle<LuaScriptAsset> module = assets_.Load<LuaScriptAsset>(moduleMetadata->id);
        if (!module.IsLoaded()) {
            AddDiagnostic(result, moduleMetadata->id, kb::scene::BehaviourBackend::Lua, assets_.LastError().empty() ? "Lua import module could not be loaded" : assets_.LastError());
            visiting.erase(moduleMetadata->id.value);
            continue;
        }
        std::unordered_set<std::uint64_t> currentVisited;
        currentVisited.insert(moduleMetadata->id.value);
        const bool importsCurrent = LuaImportsCurrentRecursive(*moduleMetadata, *module.Get(), currentVisited);
        ScriptRuntimeAssetPrepareResult imported = PrepareLuaImportsRecursive(*moduleMetadata, *module.Get(), visiting);
        const bool importedSucceeded = imported.Succeeded();
        MergePrepareResult(result, std::move(imported));
        if (!importedSucceeded) {
            visiting.erase(moduleMetadata->id.value);
            continue;
        }
        const bool moduleCurrent = runtime->IsModuleCurrent(importName, moduleMetadata->contentHash);
        if (moduleCurrent && importsCurrent) {
            visiting.erase(moduleMetadata->id.value);
            continue;
        }
        const LuaScriptLoadResult registered = runtime->RegisterModule(importName, module->source, kb::assets::NormalizeAssetPath(moduleMetadata->virtualPath), moduleMetadata->contentHash);
        if (!registered.succeeded) {
            AddDiagnostic(result, moduleMetadata->id, kb::scene::BehaviourBackend::Lua, registered.error.empty() ? "Lua import module could not be registered" : registered.error);
        }
        visiting.erase(moduleMetadata->id.value);
    }
    return result;
}

ScriptRuntimeAssetPrepareResult ScriptRuntimeAssetPreparer::PrepareNativeBehaviourAsset(const kb::assets::AssetMetadata& metadata) {
    ScriptRuntimeAssetPrepareResult result{};
    ++result.visitedAssets;
    const auto preparedNative = preparedNativeAssetHashes_.find(metadata.id.value);
    if (preparedNative != preparedNativeAssetHashes_.end() && preparedNative->second == metadata.contentHash) {
        ++result.preparedAssets;
        return result;
    }
    const kb::assets::AssetHandle<NativeBehaviourDescriptor> descriptor = assets_.Load<NativeBehaviourDescriptor>(metadata.id);
    if (!descriptor.IsLoaded()) {
        AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::Native, assets_.LastError().empty() ? "native behaviour descriptor could not be loaded" : assets_.LastError());
        return result;
    }
    if (nativeSettings_.buildPlugins && descriptor->build.enabled) {
        NativeScriptBuildResult built = NativeScriptBuildPipeline::Build(descriptor->build);
        if (!built.Succeeded()) {
            const std::string message = built.errors.empty() ? "native script plugin build failed" : built.errors.front();
            AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::Native, message);
            return result;
        }
    }
    if (nativeSettings_.loadPlugins && nativePlugins_ != nullptr && !descriptor->modulePath.empty()) {
        const std::filesystem::path modulePath = descriptor->modulePath.is_absolute() ? descriptor->modulePath : metadata.physicalPath.parent_path() / descriptor->modulePath;
        NativeScriptPluginLoadResult loaded = nativePlugins_->LoadOrReload(NativeScriptPluginLoadDesc{
            .key = "asset:" + std::to_string(metadata.id.value),
            .modulePath = modulePath,
            .entryPoint = descriptor->entryPoint,
            .shadowCopy = descriptor->shadowCopy,
            .shadowCopyDirectory = nativeSettings_.shadowCopyDirectory,
        });
        if (!loaded.Succeeded()) {
            const std::string message = loaded.errors.empty() ? "native script plugin could not be loaded" : loaded.errors.front();
            AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::Native, message);
            return result;
        }
    }
    if (nativeBackend_ != nullptr && !nativeBackend_->BindAssetSymbol(metadata.id, descriptor->symbol)) {
        AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::Native, "native behaviour descriptor symbol could not be bound");
        return result;
    }
    preparedNativeAssetHashes_[metadata.id.value] = metadata.contentHash;
    ++result.preparedAssets;
    return result;
}

ScriptRuntimeAssetPrepareResult ScriptRuntimeAssetPreparer::PrepareVisualGraphAsset(const kb::assets::AssetMetadata& metadata) {
    ScriptRuntimeAssetPrepareResult result{};
    ++result.visitedAssets;
    const auto preparedGraph = preparedVisualGraphAssetHashes_.find(metadata.id.value);
    if (preparedGraph != preparedVisualGraphAssetHashes_.end() && preparedGraph->second == metadata.contentHash && visualGraphs_.Contains(metadata.id)) {
        ++result.preparedAssets;
        return result;
    }

    kb::visual::VisualGraphCompileCoordinatorResult compiled = kb::visual::VisualGraphCompileCoordinator::Compile(
        assets_,
        kb::visual::VisualGraphCompileCoordinatorRequest{
            .assetId = metadata.id,
            .build = kb::visual::VisualGraphBuildDesc{
                .nativeCodegen = kb::visual::VisualGraphNativeCodegenDesc{
                    .className = GeneratedClassName(metadata),
                    .namespaceName = visualGraphSettings_.generatedClassNamespace,
                    .bindings = visualGraphSettings_.nativeBindings,
                    .requireNativeBindings = visualGraphSettings_.requireNativeBindings,
                },
            },
            .generatedCodeDirectory = visualGraphSettings_.generatedCodeDirectory,
            .writeGeneratedCode = visualGraphSettings_.writeGeneratedCode,
            .nativeBuild = visualGraphSettings_.nativeBuild,
            .storeRuntimeArtifact = true,
        },
        visualGraphs_);
    if (!compiled.Succeeded()) {
        std::string message = compiled.errors.empty() ? "visual graph asset could not be compiled" : compiled.errors.front();
        AddDiagnostic(result, metadata.id, kb::scene::BehaviourBackend::VisualGraph, std::move(message));
        return result;
    }

    preparedVisualGraphAssetHashes_[metadata.id.value] = metadata.contentHash;
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
