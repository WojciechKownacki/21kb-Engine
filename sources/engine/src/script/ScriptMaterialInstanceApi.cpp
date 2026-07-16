#include "engine/script/ScriptMaterialInstanceApi.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneMaterialInstances.hpp"
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

[[nodiscard]] bool IsRenderMaterialAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance";
}

// Mirrors ScriptMeshRendererApi.cpp's ResolveAssetId exactly (each script API
// file keeps its own small copy of this helper rather than sharing one
// across translation units - same convention ScriptAudioApi.cpp/
// ScriptMeshRendererApi.cpp already established).
[[nodiscard]] kb::assets::AssetId ResolveAssetId(kb::scene::Scene& scene, std::string_view reference, bool (*isExpectedType)(const kb::assets::AssetMetadata&)) {
    kb::assets::AssetId id{};
    if (kb::assets::TryParseAssetId(reference, id) && id.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(id);
        return metadata == nullptr || !isExpectedType(*metadata) ? kb::assets::AssetId{} : id;
    }

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath(std::filesystem::path{ reference });
    return metadata == nullptr || !isExpectedType(*metadata) ? kb::assets::AssetId{} : metadata->id;
}

ScriptFunctionCallResult MaterialInstanceCreate(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("material instance api requires an active scene");
    }
    const ScriptValue* materialArgument = FindArg(arguments, "material");
    const std::string material = materialArgument == nullptr ? std::string{} : materialArgument->AsString();
    const kb::assets::AssetId parentMaterialAssetId = ResolveAssetId(*context.scene, material, &IsRenderMaterialAsset);
    if (!parentMaterialAssetId.IsValid()) {
        return Error("parent material asset could not be resolved");
    }

    const std::uint64_t instance = context.scene->MaterialInstances().Create(parentMaterialAssetId.value);
    if (instance == 0U) {
        return Error("material instance limit reached");
    }

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "instance", ScriptValue{ instance, ScriptValueType::Hash } } },
        .errors = {},
    };
}

ScriptFunctionCallResult MaterialInstanceRelease(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("material instance api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const bool released = instance != 0U && context.scene->MaterialInstances().Release(instance);

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "released", ScriptValue{ released } } },
        .errors = {},
    };
}

ScriptFunctionCallResult MaterialInstanceExists(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("material instance api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const bool exists = instance != 0U && context.scene->MaterialInstances().Exists(instance);

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "exists", ScriptValue{ exists } } },
        .errors = {},
    };
}

ScriptFunctionCallResult MaterialInstanceParent(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("material instance api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const std::uint64_t parentMaterialAssetId = instance == 0U ? 0U : context.scene->MaterialInstances().Parent(instance);
    const std::string material = parentMaterialAssetId == 0U ? std::string{} : kb::assets::ToString(kb::assets::AssetId{ parentMaterialAssetId });

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "material", ScriptValue{ material } } },
        .errors = {},
    };
}

ScriptFunctionCallResult MaterialInstanceSetParameterScalar(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("material instance api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const ScriptValue* nameArgument = FindArg(arguments, "name");
    const ScriptValue* valueArgument = FindArg(arguments, "value");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const std::string name = nameArgument == nullptr ? std::string{} : nameArgument->AsString();
    const float value = valueArgument == nullptr ? 0.0F : valueArgument->AsFloat();
    const bool applied = instance != 0U && context.scene->MaterialInstances().SetParameterScalar(instance, name, value);

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "applied", ScriptValue{ applied } } },
        .errors = {},
    };
}

ScriptFunctionCallResult MaterialInstanceSetParameterBool(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("material instance api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const ScriptValue* nameArgument = FindArg(arguments, "name");
    const ScriptValue* valueArgument = FindArg(arguments, "value");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const std::string name = nameArgument == nullptr ? std::string{} : nameArgument->AsString();
    const bool value = valueArgument != nullptr && valueArgument->AsBool();
    const bool applied = instance != 0U && context.scene->MaterialInstances().SetParameterBool(instance, name, value);

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "applied", ScriptValue{ applied } } },
        .errors = {},
    };
}

ScriptFunctionCallResult MaterialInstanceClearParameter(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("material instance api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const ScriptValue* nameArgument = FindArg(arguments, "name");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const std::string name = nameArgument == nullptr ? std::string{} : nameArgument->AsString();
    const bool cleared = instance != 0U && context.scene->MaterialInstances().ClearParameter(instance, name);

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "cleared", ScriptValue{ cleared } } },
        .errors = {},
    };
}

} // namespace

bool ScriptMaterialInstanceApi::Register(ScriptRuntimeHost& host) {
    ScriptFunctionDesc create;
    create.signature.name = "MaterialInstance.Create";
    create.signature.inputs = {
        ScriptFunctionPin{ "material", ScriptValueType::String, true },
    };
    create.signature.outputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
    };
    create.callback = &MaterialInstanceCreate;
    if (!host.RegisterFunction(std::move(create))) {
        return false;
    }

    ScriptFunctionDesc release;
    release.signature.name = "MaterialInstance.Release";
    release.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
    };
    release.signature.outputs = {
        ScriptFunctionPin{ "released", ScriptValueType::Bool, true },
    };
    release.callback = &MaterialInstanceRelease;
    if (!host.RegisterFunction(std::move(release))) {
        return false;
    }

    ScriptFunctionDesc exists;
    exists.signature.name = "MaterialInstance.Exists";
    exists.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
    };
    exists.signature.outputs = {
        ScriptFunctionPin{ "exists", ScriptValueType::Bool, true },
    };
    exists.callback = &MaterialInstanceExists;
    if (!host.RegisterFunction(std::move(exists))) {
        return false;
    }

    ScriptFunctionDesc parent;
    parent.signature.name = "MaterialInstance.Parent";
    parent.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
    };
    parent.signature.outputs = {
        ScriptFunctionPin{ "material", ScriptValueType::String, true },
    };
    parent.callback = &MaterialInstanceParent;
    if (!host.RegisterFunction(std::move(parent))) {
        return false;
    }

    ScriptFunctionDesc setParameterScalar;
    setParameterScalar.signature.name = "MaterialInstance.SetParameterScalar";
    setParameterScalar.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
        ScriptFunctionPin{ "name", ScriptValueType::String, true },
        ScriptFunctionPin{ "value", ScriptValueType::Float, true },
    };
    setParameterScalar.signature.outputs = {
        ScriptFunctionPin{ "applied", ScriptValueType::Bool, true },
    };
    setParameterScalar.callback = &MaterialInstanceSetParameterScalar;
    if (!host.RegisterFunction(std::move(setParameterScalar))) {
        return false;
    }

    ScriptFunctionDesc setParameterBool;
    setParameterBool.signature.name = "MaterialInstance.SetParameterBool";
    setParameterBool.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
        ScriptFunctionPin{ "name", ScriptValueType::String, true },
        ScriptFunctionPin{ "value", ScriptValueType::Bool, true },
    };
    setParameterBool.signature.outputs = {
        ScriptFunctionPin{ "applied", ScriptValueType::Bool, true },
    };
    setParameterBool.callback = &MaterialInstanceSetParameterBool;
    if (!host.RegisterFunction(std::move(setParameterBool))) {
        return false;
    }

    ScriptFunctionDesc clearParameter;
    clearParameter.signature.name = "MaterialInstance.ClearParameter";
    clearParameter.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
        ScriptFunctionPin{ "name", ScriptValueType::String, true },
    };
    clearParameter.signature.outputs = {
        ScriptFunctionPin{ "cleared", ScriptValueType::Bool, true },
    };
    clearParameter.callback = &MaterialInstanceClearParameter;
    return host.RegisterFunction(std::move(clearParameter));
}

} // namespace kb::script
