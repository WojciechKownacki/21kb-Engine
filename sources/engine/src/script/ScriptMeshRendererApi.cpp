#include "engine/script/ScriptMeshRendererApi.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace kb::script {
namespace {

const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

[[nodiscard]] kb::scene::SceneEntity TargetEntity(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) noexcept {
    const ScriptValue* explicitEntity = FindArg(arguments, "entity");
    if (explicitEntity != nullptr) {
        return kb::scene::SceneEntity{ explicitEntity->AsUInt64() };
    }
    return context.caller;
}

// Mirrors ScriptAudioApi.cpp's ResolveClipAssetId exactly - accept either a numeric asset
// id or a virtual path, resolve through the SAME AssetRegistry the real import pipeline
// populates, and only succeed for an asset of the expected type. There is no other way to
// reach an asset id from script: no code path here can fabricate a metadata entry, only
// look one up.
[[nodiscard]] kb::assets::AssetId ResolveAssetId(kb::scene::Scene& scene, std::string_view reference, bool (*isExpectedType)(const kb::assets::AssetMetadata&)) {
    kb::assets::AssetId id{};
    if (kb::assets::TryParseAssetId(reference, id) && id.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(id);
        return metadata == nullptr || !isExpectedType(*metadata) ? kb::assets::AssetId{} : id;
    }

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath(std::filesystem::path{ reference });
    return metadata == nullptr || !isExpectedType(*metadata) ? kb::assets::AssetId{} : metadata->id;
}

[[nodiscard]] bool IsRenderMeshAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMesh";
}

[[nodiscard]] bool IsRenderMaterialAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance";
}

ScriptFunctionCallResult MeshRendererSetMesh(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("mesh renderer api requires an active scene");
    }
    const kb::scene::SceneEntity entity = TargetEntity(context, arguments);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return Error("mesh renderer target entity is not alive");
    }

    const ScriptValue* meshArgument = FindArg(arguments, "mesh");
    const std::string mesh = meshArgument == nullptr ? std::string{} : meshArgument->AsString();
    const kb::assets::AssetId meshAssetId = ResolveAssetId(*context.scene, mesh, &IsRenderMeshAsset);
    if (!meshAssetId.IsValid()) {
        return Error("mesh asset could not be resolved");
    }

    kb::scene::SceneMeshRendererComponents renderers = context.scene->Components().MeshRenderers();
    kb::scene::MeshRendererComponent* existing = renderers.TryGet(entity);
    if (existing != nullptr) {
        existing->meshAssetId = meshAssetId.value;
        renderers.MarkModified(entity);
    } else {
        kb::scene::MeshRendererComponent component{};
        component.meshAssetId = meshAssetId.value;
        renderers.Set(entity, component);
    }

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "assigned", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult MeshRendererSetMaterial(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("mesh renderer api requires an active scene");
    }
    const kb::scene::SceneEntity entity = TargetEntity(context, arguments);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return Error("mesh renderer target entity is not alive");
    }

    const ScriptValue* materialArgument = FindArg(arguments, "material");
    const std::string material = materialArgument == nullptr ? std::string{} : materialArgument->AsString();
    const kb::assets::AssetId materialAssetId = ResolveAssetId(*context.scene, material, &IsRenderMaterialAsset);
    if (!materialAssetId.IsValid()) {
        return Error("material asset could not be resolved");
    }

    kb::scene::SceneMeshRendererComponents renderers = context.scene->Components().MeshRenderers();
    kb::scene::MeshRendererComponent* existing = renderers.TryGet(entity);
    if (existing != nullptr) {
        existing->materialAssetId = materialAssetId.value;
        renderers.MarkModified(entity);
    } else {
        kb::scene::MeshRendererComponent component{};
        component.materialAssetId = materialAssetId.value;
        renderers.Set(entity, component);
    }

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "assigned", ScriptValue{ true } } },
        .errors = {},
    };
}

} // namespace

bool ScriptMeshRendererApi::Register(ScriptRuntimeHost& host) {
    ScriptFunctionDesc setMesh;
    setMesh.signature.name = "MeshRenderer.SetMesh";
    setMesh.signature.inputs = {
        ScriptFunctionPin{ "mesh", ScriptValueType::String, true },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    setMesh.signature.outputs = {
        ScriptFunctionPin{ "assigned", ScriptValueType::Bool, true },
    };
    setMesh.callback = &MeshRendererSetMesh;
    if (!host.RegisterFunction(std::move(setMesh))) {
        return false;
    }

    ScriptFunctionDesc setMaterial;
    setMaterial.signature.name = "MeshRenderer.SetMaterial";
    setMaterial.signature.inputs = {
        ScriptFunctionPin{ "material", ScriptValueType::String, true },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    setMaterial.signature.outputs = {
        ScriptFunctionPin{ "assigned", ScriptValueType::Bool, true },
    };
    setMaterial.callback = &MeshRendererSetMaterial;
    return host.RegisterFunction(std::move(setMaterial));
}

} // namespace kb::script
