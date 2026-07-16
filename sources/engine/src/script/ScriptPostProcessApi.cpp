#include "engine/script/ScriptPostProcessApi.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/ScenePostProcessAccess.hpp"
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

[[nodiscard]] bool IsPostProcessProfileAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "PostProcessProfile";
}

// Mirrors ScriptMeshRendererApi.cpp's ResolveAssetId exactly (each script API file keeps its
// own small copy of this helper rather than sharing one across translation units - the same
// convention ScriptAudioApi.cpp/ScriptMeshRendererApi.cpp/ScriptMaterialInstanceApi.cpp
// already established).
[[nodiscard]] kb::assets::AssetId ResolveAssetId(kb::scene::Scene& scene, std::string_view reference, bool (*isExpectedType)(const kb::assets::AssetMetadata&)) {
    kb::assets::AssetId id{};
    if (kb::assets::TryParseAssetId(reference, id) && id.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(id);
        return metadata == nullptr || !isExpectedType(*metadata) ? kb::assets::AssetId{} : id;
    }

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath(std::filesystem::path{ reference });
    return metadata == nullptr || !isExpectedType(*metadata) ? kb::assets::AssetId{} : metadata->id;
}

ScriptFunctionCallResult PostProcessSetProfile(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("post process api requires an active scene");
    }
    const ScriptValue* profileArgument = FindArg(arguments, "profile");
    const std::string profile = profileArgument == nullptr ? std::string{} : profileArgument->AsString();
    const kb::assets::AssetId profileAssetId = ResolveAssetId(*context.scene, profile, &IsPostProcessProfileAsset);
    if (!profileAssetId.IsValid()) {
        return Error("post process profile asset could not be resolved");
    }

    kb::scene::ScenePostProcessAccess::SetActiveProfile(*context.scene, profileAssetId.value);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "assigned", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult PostProcessClearProfile(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    static_cast<void>(arguments);
    if (context.scene == nullptr) {
        return Error("post process api requires an active scene");
    }
    kb::scene::ScenePostProcessAccess::SetActiveProfile(*context.scene, 0U);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "cleared", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult PostProcessActiveProfile(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    static_cast<void>(arguments);
    if (context.scene == nullptr) {
        return Error("post process api requires an active scene");
    }
    const std::uint64_t profileAssetId = kb::scene::ScenePostProcessAccess::ActiveProfile(*context.scene);
    const std::string profile = profileAssetId == 0U ? std::string{} : kb::assets::ToString(kb::assets::AssetId{ profileAssetId });
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "profile", ScriptValue{ profile } } },
        .errors = {},
    };
}

} // namespace

bool ScriptPostProcessApi::Register(ScriptRuntimeHost& host) {
    ScriptFunctionDesc setProfile;
    setProfile.signature.name = "PostProcess.SetProfile";
    setProfile.signature.inputs = {
        ScriptFunctionPin{ "profile", ScriptValueType::String, true },
    };
    setProfile.signature.outputs = {
        ScriptFunctionPin{ "assigned", ScriptValueType::Bool, true },
    };
    setProfile.callback = &PostProcessSetProfile;
    if (!host.RegisterFunction(std::move(setProfile))) {
        return false;
    }

    ScriptFunctionDesc clearProfile;
    clearProfile.signature.name = "PostProcess.ClearProfile";
    clearProfile.signature.outputs = {
        ScriptFunctionPin{ "cleared", ScriptValueType::Bool, true },
    };
    clearProfile.callback = &PostProcessClearProfile;
    if (!host.RegisterFunction(std::move(clearProfile))) {
        return false;
    }

    ScriptFunctionDesc activeProfile;
    activeProfile.signature.name = "PostProcess.ActiveProfile";
    activeProfile.signature.outputs = {
        ScriptFunctionPin{ "profile", ScriptValueType::String, true },
    };
    activeProfile.callback = &PostProcessActiveProfile;
    return host.RegisterFunction(std::move(activeProfile));
}

} // namespace kb::script
