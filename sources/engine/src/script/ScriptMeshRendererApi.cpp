#include "engine/script/ScriptMeshRendererApi.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneMaterialInstances.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <algorithm>
#include <cstdint>
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

// LIB-138: slot -> section mapping is entirely the renderer's own responsibility
// (MeshPipelineResourceResolver::MaterialAssetForSectionInstance consults
// materialSlotAssetIds[section.materialSlot], where each imported mesh section already
// declares its own materialSlot index) - kb::scene/kb::script deliberately have no mesh
// section/slot-count query of their own (kb::scene never depends on kb::render, so it has
// no way to know how many sections a given meshAssetId actually has). Setting slot N here
// "matches mesh sections" by construction: whichever sections declared materialSlot==N pick
// up this override, and slots no section references are harmless no-ops (the resolver only
// ever reads slots sections actually reference).
[[nodiscard]] bool ParseSlotIndex(std::span<const ScriptFunctionArgument> arguments, std::uint32_t& outSlot) noexcept {
    const ScriptValue* slotArgument = FindArg(arguments, "slot");
    if (slotArgument == nullptr || slotArgument->Type() != ScriptValueType::Int) {
        return false;
    }
    const int slot = slotArgument->AsInt();
    if (slot < 0 || static_cast<std::uint32_t>(slot) >= kb::scene::kMaxMeshRendererMaterialSlotOverrides) {
        return false;
    }
    outSlot = static_cast<std::uint32_t>(slot);
    return true;
}

ScriptFunctionCallResult MeshRendererSetMaterialSlot(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("mesh renderer api requires an active scene");
    }
    const kb::scene::SceneEntity entity = TargetEntity(context, arguments);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return Error("mesh renderer target entity is not alive");
    }
    std::uint32_t slot = 0;
    if (!ParseSlotIndex(arguments, slot)) {
        return Error("mesh renderer material slot index is missing or out of range");
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
        existing->materialSlotAssetIds[slot] = materialAssetId.value;
        existing->materialSlotOverrideCount = std::max(existing->materialSlotOverrideCount, slot + 1U);
        renderers.MarkModified(entity);
    } else {
        kb::scene::MeshRendererComponent component{};
        component.materialSlotAssetIds[slot] = materialAssetId.value;
        component.materialSlotOverrideCount = slot + 1U;
        renderers.Set(entity, component);
    }

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "assigned", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult MeshRendererGetMaterialSlot(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("mesh renderer api requires an active scene");
    }
    const kb::scene::SceneEntity entity = TargetEntity(context, arguments);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return Error("mesh renderer target entity is not alive");
    }
    std::uint32_t slot = 0;
    if (!ParseSlotIndex(arguments, slot)) {
        return Error("mesh renderer material slot index is missing or out of range");
    }

    const kb::scene::MeshRendererComponent* existing = context.scene->Components().MeshRenderers().TryGet(entity);
    const bool hasOverride = existing != nullptr && slot < existing->materialSlotOverrideCount && existing->materialSlotAssetIds[slot] != 0U;
    const std::string material = hasOverride ? kb::assets::ToString(kb::assets::AssetId{ existing->materialSlotAssetIds[slot] }) : std::string{};

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "material", ScriptValue{ material } },
            ScriptFunctionArgument{ "hasOverride", ScriptValue{ hasOverride } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult MeshRendererClearMaterialSlot(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("mesh renderer api requires an active scene");
    }
    const kb::scene::SceneEntity entity = TargetEntity(context, arguments);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return Error("mesh renderer target entity is not alive");
    }
    std::uint32_t slot = 0;
    if (!ParseSlotIndex(arguments, slot)) {
        return Error("mesh renderer material slot index is missing or out of range");
    }

    kb::scene::MeshRendererComponent* existing = context.scene->Components().MeshRenderers().TryGet(entity);
    if (existing == nullptr) {
        return Error("mesh renderer component does not exist on this entity");
    }
    // A zero id is exactly what MeshPipelineResourceResolver::MaterialAssetForSectionInstance
    // treats as "no override for this slot" (falls through to materialAssetId, then the
    // mesh's own default) - clearing does not need to shrink materialSlotOverrideCount, since
    // other slots below it may still be genuinely overridden.
    existing->materialSlotAssetIds[slot] = 0U;
    context.scene->Components().MeshRenderers().MarkModified(entity);

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "cleared", ScriptValue{ true } } },
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

// LIB-139: assigns a LIVE runtime MaterialInstance (kb::scene::
// SceneMaterialInstances::Create's handle - see ScriptMaterialInstanceApi.cpp),
// not an asset reference - there is no path/id string to resolve here, the
// handle is validated directly against the scene's own instance table.
// Overrides materialAssetId/materialSlotAssetIds for rendering purposes (see
// EcsRenderSceneSynchronizer::SyncMesh's resolution order) without touching
// either field's own stored value - ClearMaterialInstance reverts to
// whichever asset-based assignment was already there.
ScriptFunctionCallResult MeshRendererSetMaterialInstance(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("mesh renderer api requires an active scene");
    }
    const kb::scene::SceneEntity entity = TargetEntity(context, arguments);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return Error("mesh renderer target entity is not alive");
    }

    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const std::uint64_t instanceHandle = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    if (instanceHandle == 0U || !context.scene->MaterialInstances().Exists(instanceHandle)) {
        return Error("material instance handle does not name a live instance");
    }

    kb::scene::SceneMeshRendererComponents renderers = context.scene->Components().MeshRenderers();
    kb::scene::MeshRendererComponent* existing = renderers.TryGet(entity);
    if (existing != nullptr) {
        existing->materialInstanceHandle = instanceHandle;
        renderers.MarkModified(entity);
    } else {
        kb::scene::MeshRendererComponent component{};
        component.materialInstanceHandle = instanceHandle;
        renderers.Set(entity, component);
    }

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "assigned", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult MeshRendererClearMaterialInstance(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("mesh renderer api requires an active scene");
    }
    const kb::scene::SceneEntity entity = TargetEntity(context, arguments);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return Error("mesh renderer target entity is not alive");
    }

    kb::scene::MeshRendererComponent* existing = context.scene->Components().MeshRenderers().TryGet(entity);
    if (existing == nullptr) {
        return Error("mesh renderer component does not exist on this entity");
    }
    existing->materialInstanceHandle = 0U;
    context.scene->Components().MeshRenderers().MarkModified(entity);

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "cleared", ScriptValue{ true } } },
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
    if (!host.RegisterFunction(std::move(setMaterial))) {
        return false;
    }

    ScriptFunctionDesc setMaterialSlot;
    setMaterialSlot.signature.name = "MeshRenderer.SetMaterialSlot";
    setMaterialSlot.signature.inputs = {
        ScriptFunctionPin{ "slot", ScriptValueType::Int, true },
        ScriptFunctionPin{ "material", ScriptValueType::String, true },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    setMaterialSlot.signature.outputs = {
        ScriptFunctionPin{ "assigned", ScriptValueType::Bool, true },
    };
    setMaterialSlot.callback = &MeshRendererSetMaterialSlot;
    if (!host.RegisterFunction(std::move(setMaterialSlot))) {
        return false;
    }

    ScriptFunctionDesc getMaterialSlot;
    getMaterialSlot.signature.name = "MeshRenderer.GetMaterialSlot";
    getMaterialSlot.signature.inputs = {
        ScriptFunctionPin{ "slot", ScriptValueType::Int, true },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    getMaterialSlot.signature.outputs = {
        ScriptFunctionPin{ "material", ScriptValueType::String, true },
        ScriptFunctionPin{ "hasOverride", ScriptValueType::Bool, true },
    };
    getMaterialSlot.callback = &MeshRendererGetMaterialSlot;
    if (!host.RegisterFunction(std::move(getMaterialSlot))) {
        return false;
    }

    ScriptFunctionDesc clearMaterialSlot;
    clearMaterialSlot.signature.name = "MeshRenderer.ClearMaterialSlot";
    clearMaterialSlot.signature.inputs = {
        ScriptFunctionPin{ "slot", ScriptValueType::Int, true },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    clearMaterialSlot.signature.outputs = {
        ScriptFunctionPin{ "cleared", ScriptValueType::Bool, true },
    };
    clearMaterialSlot.callback = &MeshRendererClearMaterialSlot;
    if (!host.RegisterFunction(std::move(clearMaterialSlot))) {
        return false;
    }

    ScriptFunctionDesc setMaterialInstance;
    setMaterialInstance.signature.name = "MeshRenderer.SetMaterialInstance";
    setMaterialInstance.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    setMaterialInstance.signature.outputs = {
        ScriptFunctionPin{ "assigned", ScriptValueType::Bool, true },
    };
    setMaterialInstance.callback = &MeshRendererSetMaterialInstance;
    if (!host.RegisterFunction(std::move(setMaterialInstance))) {
        return false;
    }

    ScriptFunctionDesc clearMaterialInstance;
    clearMaterialInstance.signature.name = "MeshRenderer.ClearMaterialInstance";
    clearMaterialInstance.signature.inputs = {
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    clearMaterialInstance.signature.outputs = {
        ScriptFunctionPin{ "cleared", ScriptValueType::Bool, true },
    };
    clearMaterialInstance.callback = &MeshRendererClearMaterialInstance;
    return host.RegisterFunction(std::move(clearMaterialInstance));
}

} // namespace kb::script
