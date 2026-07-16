#include "engine/script/ScriptAudioApi.hpp"

#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

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

[[nodiscard]] kb::audio::AudioAttenuationModel AttenuationModelArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, kb::audio::AudioAttenuationModel fallback) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    if (value == nullptr) {
        return fallback;
    }
    const int rawValue = value->AsInt(static_cast<int>(fallback));
    if (rawValue < static_cast<int>(kb::audio::AudioAttenuationModel::None) || rawValue > static_cast<int>(kb::audio::AudioAttenuationModel::Exponential)) {
        return fallback;
    }
    return static_cast<kb::audio::AudioAttenuationModel>(rawValue);
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

    // LIB-147: optional mixer-bus routing (empty/unknown = implicit master, see
    // AudioPlayDesc::outputBus).
    const ScriptValue* outputBusArgument = FindArg(arguments, "outputBus");
    const kb::audio::AudioPlayDesc playDesc{
        .clipAssetId = clipAssetId.value,
        .outputBus = outputBusArgument == nullptr ? std::string{} : outputBusArgument->AsString(),
        .volume = FloatArg(arguments, "volume", 1.0F),
        .pitch = FloatArg(arguments, "pitch", 1.0F),
        .mute = BoolArg(arguments, "mute", false),
        .loop = BoolArg(arguments, "loop", false),
        .spatial = BoolArg(arguments, "spatial", true),
        .pan = FloatArg(arguments, "pan", 0.0F),
        .spatialBlend = FloatArg(arguments, "spatialBlend", 1.0F),
        .attenuationModel = AttenuationModelArg(arguments, "attenuationModel", kb::audio::AudioAttenuationModel::Inverse),
        .minDistance = FloatArg(arguments, "minDistance", 1.0F),
        .maxDistance = FloatArg(arguments, "maxDistance", 500.0F),
        .rolloff = FloatArg(arguments, "rolloff", 1.0F),
        .dopplerFactor = FloatArg(arguments, "dopplerFactor", 1.0F),
        .position = PlaybackPosition(context, arguments),
    };
    const kb::audio::AudioPlayResult played = kb::audio::AudioPlayback::PlayOneShot(*context.scene, playDesc);
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

// LIB-147: mirrors ScriptPostProcessApi's ResolveAssetId exactly (each script API file
// keeps its own small copy of this helper - established convention).
[[nodiscard]] kb::assets::AssetId ResolveMixerAssetId(kb::scene::Scene& scene, std::string_view reference) {
    const auto isMixerAsset = [](const kb::assets::AssetMetadata& metadata) {
        return metadata.type == kb::audio::kAudioMixerAssetType;
    };

    kb::assets::AssetId id{};
    if (kb::assets::TryParseAssetId(reference, id) && id.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(id);
        return metadata == nullptr || !isMixerAsset(*metadata) ? kb::assets::AssetId{} : id;
    }

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath(std::filesystem::path{ reference });
    return metadata == nullptr || !isMixerAsset(*metadata) ? kb::assets::AssetId{} : metadata->id;
}

ScriptFunctionCallResult AudioSetMixer(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("audio api requires an active scene");
    }
    const ScriptValue* mixerArgument = FindArg(arguments, "mixer");
    const std::string mixer = mixerArgument == nullptr ? std::string{} : mixerArgument->AsString();
    if (mixer.empty()) {
        // Explicitly clearing the mixer is a valid request (back to the implicit master),
        // mirroring PostProcess.ClearProfile's semantics without a second function.
        kb::scene::SceneAudioMixerAccess::SetActiveMixer(*context.scene, 0U);
        kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(*context.scene, {});
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = { ScriptFunctionArgument{ "assigned", ScriptValue{ true } } },
            .errors = {},
        };
    }
    const kb::assets::AssetId mixerAssetId = ResolveMixerAssetId(*context.scene, mixer);
    if (!mixerAssetId.IsValid()) {
        return Error("audio mixer asset could not be resolved");
    }

    kb::scene::SceneAudioMixerAccess::SetActiveMixer(*context.scene, mixerAssetId.value);
    // A different mixer's snapshot names are unrelated - reset the active snapshot to the
    // authored volumes instead of silently carrying a stale name across mixers.
    kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(*context.scene, {});
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "assigned", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult AudioActiveMixer(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    static_cast<void>(arguments);
    if (context.scene == nullptr) {
        return Error("audio api requires an active scene");
    }
    const std::uint64_t mixerAssetId = kb::scene::SceneAudioMixerAccess::ActiveMixer(*context.scene);
    const std::string mixer = mixerAssetId == 0U ? std::string{} : kb::assets::ToString(kb::assets::AssetId{ mixerAssetId });
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "mixer", ScriptValue{ mixer } } },
        .errors = {},
    };
}

ScriptFunctionCallResult AudioSetSnapshot(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("audio api requires an active scene");
    }
    const ScriptValue* snapshotArgument = FindArg(arguments, "snapshot");
    const std::string snapshot = snapshotArgument == nullptr ? std::string{} : snapshotArgument->AsString();
    const std::uint64_t mixerAssetId = kb::scene::SceneAudioMixerAccess::ActiveMixer(*context.scene);
    if (mixerAssetId == 0U) {
        return Error("audio snapshot requires an active audio mixer");
    }
    if (!snapshot.empty()) {
        // The mixer asset lives engine-side, so unlike material parameters the snapshot
        // name CAN be validated right here against the real asset - an unknown name is an
        // honest error, not a silently-ignored no-op.
        const kb::assets::AssetHandle<kb::audio::AudioMixerAsset> mixer =
            context.scene->Assets().Manager().Load<kb::audio::AudioMixerAsset>(kb::assets::AssetId{ mixerAssetId });
        if (!mixer.IsLoaded()) {
            return Error("active audio mixer asset could not be loaded");
        }
        if (mixer->FindSnapshot(snapshot) == nullptr) {
            return Error("audio snapshot name is not declared by the active mixer");
        }
    }

    kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(*context.scene, snapshot);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "applied", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult AudioActiveSnapshot(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    static_cast<void>(arguments);
    if (context.scene == nullptr) {
        return Error("audio api requires an active scene");
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "snapshot", ScriptValue{ kb::scene::SceneAudioMixerAccess::ActiveSnapshot(*context.scene) } } },
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
        ScriptFunctionPin{ "mute", ScriptValueType::Bool, false },
        ScriptFunctionPin{ "loop", ScriptValueType::Bool, false },
        ScriptFunctionPin{ "spatial", ScriptValueType::Bool, false },
        ScriptFunctionPin{ "pan", ScriptValueType::Float, false },
        ScriptFunctionPin{ "spatialBlend", ScriptValueType::Float, false },
        ScriptFunctionPin{ "attenuationModel", ScriptValueType::Int, false },
        ScriptFunctionPin{ "minDistance", ScriptValueType::Float, false },
        ScriptFunctionPin{ "maxDistance", ScriptValueType::Float, false },
        ScriptFunctionPin{ "rolloff", ScriptValueType::Float, false },
        ScriptFunctionPin{ "dopplerFactor", ScriptValueType::Float, false },
        ScriptFunctionPin{ "outputBus", ScriptValueType::String, false },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    desc.signature.outputs = {
        ScriptFunctionPin{ "played", ScriptValueType::Bool, true },
        ScriptFunctionPin{ "voice", ScriptValueType::Int, true },
    };
    desc.callback = &AudioPlay;
    if (!host.RegisterFunction(std::move(desc))) {
        return false;
    }

    ScriptFunctionDesc setMixer;
    setMixer.signature.name = "Audio.SetMixer";
    setMixer.signature.inputs = {
        ScriptFunctionPin{ "mixer", ScriptValueType::String, false },
    };
    setMixer.signature.outputs = {
        ScriptFunctionPin{ "assigned", ScriptValueType::Bool, true },
    };
    setMixer.callback = &AudioSetMixer;
    if (!host.RegisterFunction(std::move(setMixer))) {
        return false;
    }

    ScriptFunctionDesc activeMixer;
    activeMixer.signature.name = "Audio.ActiveMixer";
    activeMixer.signature.outputs = {
        ScriptFunctionPin{ "mixer", ScriptValueType::String, true },
    };
    activeMixer.callback = &AudioActiveMixer;
    if (!host.RegisterFunction(std::move(activeMixer))) {
        return false;
    }

    ScriptFunctionDesc setSnapshot;
    setSnapshot.signature.name = "Audio.SetSnapshot";
    setSnapshot.signature.inputs = {
        ScriptFunctionPin{ "snapshot", ScriptValueType::String, false },
    };
    setSnapshot.signature.outputs = {
        ScriptFunctionPin{ "applied", ScriptValueType::Bool, true },
    };
    setSnapshot.callback = &AudioSetSnapshot;
    if (!host.RegisterFunction(std::move(setSnapshot))) {
        return false;
    }

    ScriptFunctionDesc activeSnapshot;
    activeSnapshot.signature.name = "Audio.ActiveSnapshot";
    activeSnapshot.signature.outputs = {
        ScriptFunctionPin{ "snapshot", ScriptValueType::String, true },
    };
    activeSnapshot.callback = &AudioActiveSnapshot;
    return host.RegisterFunction(std::move(activeSnapshot));
}

} // namespace kb::script
