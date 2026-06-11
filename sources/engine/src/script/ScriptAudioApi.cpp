#include "engine/script/ScriptAudioApi.hpp"

#include "engine/audio/AudioPlayback.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
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

[[nodiscard]] kb::assets::AssetId ResolveClipAssetId(kb::scene::Scene& scene, std::string_view clip) {
    const auto isAudioAsset = [](const kb::assets::AssetMetadata& metadata) {
        return metadata.type == "AudioClip" || (metadata.type == "ImportedAsset" && metadata.importCategory == "Audio");
    };

    kb::assets::AssetId id{};
    if (kb::assets::TryParseAssetId(clip, id) && id.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(id);
        return metadata == nullptr || !isAudioAsset(*metadata) ? kb::assets::AssetId{} : id;
    }

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath(std::filesystem::path{ clip });
    return metadata == nullptr || !isAudioAsset(*metadata) ? kb::assets::AssetId{} : metadata->id;
}

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, float fallback) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsFloat(fallback);
}

[[nodiscard]] bool BoolArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, bool fallback) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsBool(fallback);
}

[[nodiscard]] kb::scene::SceneEntity ParentEntity(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) noexcept {
    const ScriptValue* explicitEntity = FindArg(arguments, "entity");
    if (explicitEntity != nullptr) {
        return kb::scene::SceneEntity{ explicitEntity->AsUInt64() };
    }
    return context.caller;
}

[[nodiscard]] kb::scene::Vec3 PlaybackPosition(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return {};
    }
    const kb::scene::SceneEntity entity = ParentEntity(context, arguments);
    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        return {};
    }
    return context.scene->Transforms().Get(entity).worldPosition;
}

ScriptFunctionCallResult AudioPlay(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("audio api requires an active scene");
    }

    const ScriptValue* clipArgument = FindArg(arguments, "clip");
    const std::string clip = clipArgument == nullptr ? std::string{} : clipArgument->AsString();
    const kb::assets::AssetId clipAssetId = ResolveClipAssetId(*context.scene, clip);
    if (!clipAssetId.IsValid()) {
        return Error("audio clip asset could not be resolved");
    }

    const kb::audio::AudioPlayResult played = kb::audio::AudioPlayback::PlayOneShot(*context.scene, kb::audio::AudioPlayDesc{
        .clipAssetId = clipAssetId.value,
        .volume = FloatArg(arguments, "volume", 1.0F),
        .pitch = FloatArg(arguments, "pitch", 1.0F),
        .loop = BoolArg(arguments, "loop", false),
        .spatial = BoolArg(arguments, "spatial", true),
        .position = PlaybackPosition(context, arguments),
    });
    if (!played.Succeeded()) {
        return Error(played.error.empty() ? "audio playback request failed" : played.error);
    }

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "played", ScriptValue{ true } },
            ScriptFunctionArgument{ "voice", ScriptValue{ static_cast<int>(played.voiceId) } },
        },
        .errors = {},
    };
}

} // namespace

bool ScriptAudioApi::Register(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Audio.Play";
    desc.signature.inputs = {
        ScriptFunctionPin{ "clip", ScriptValueType::String, true },
        ScriptFunctionPin{ "volume", ScriptValueType::Float, false },
        ScriptFunctionPin{ "pitch", ScriptValueType::Float, false },
        ScriptFunctionPin{ "loop", ScriptValueType::Bool, false },
        ScriptFunctionPin{ "spatial", ScriptValueType::Bool, false },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    desc.signature.outputs = {
        ScriptFunctionPin{ "played", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "voice", ScriptValueType::Int, true },
    };
    desc.callback = &AudioPlay;
    return host.RegisterFunction(std::move(desc));
}

} // namespace kb::script
