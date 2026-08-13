#include "engine/script/ScriptParticleSystemApi.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneParticleSystems.hpp"
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

[[nodiscard]] std::string ParticleRuntimeError(kb::particles::ParticleRuntimeStatus status) {
    switch (status) {
    case kb::particles::ParticleRuntimeStatus::BackendUnavailable: return "particle simulation backend is unavailable; ensure Rendering.21kbParticle is enabled and loaded";
    case kb::particles::ParticleRuntimeStatus::InvalidAsset: return "particle effect asset is invalid";
    case kb::particles::ParticleRuntimeStatus::InvalidOwner: return "particles owner entity is invalid";
    case kb::particles::ParticleRuntimeStatus::InvalidInstance: return "particle system instance is invalid";
    case kb::particles::ParticleRuntimeStatus::InvalidParameter: return "particle parameter is invalid";
    case kb::particles::ParticleRuntimeStatus::InstanceLimitReached: return "particle system instance limit was reached";
    case kb::particles::ParticleRuntimeStatus::ParticleCapacityReached: return "particle capacity was reached";
    case kb::particles::ParticleRuntimeStatus::SpawnBudgetExceeded: return "particle spawn budget was exceeded";
    case kb::particles::ParticleRuntimeStatus::UnsupportedOutput: return "particle effect output is unsupported";
    case kb::particles::ParticleRuntimeStatus::InvalidRequest: return "particle runtime request is invalid";
    case kb::particles::ParticleRuntimeStatus::Success: break;
    }
    return "particle runtime request failed";
}

[[nodiscard]] kb::scene::SceneEntity TargetEntity(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) noexcept {
    const ScriptValue* explicitEntity = FindArg(arguments, "entity");
    if (explicitEntity != nullptr) {
        return kb::scene::SceneEntity{ explicitEntity->AsUInt64() };
    }
    return context.caller;
}

// Mirrors ScriptMeshRendererApi.cpp/ScriptMaterialInstanceApi.cpp's ResolveAssetId exactly -
// each script API file keeps its own small copy of this helper rather than sharing one
// across translation units (established convention, see ScriptMaterialInstanceApi.cpp's own
// comment on this).
[[nodiscard]] kb::assets::AssetId ResolveAssetId(kb::scene::Scene& scene, std::string_view reference, bool (*isExpectedType)(const kb::assets::AssetMetadata&)) {
    kb::assets::AssetId id{};
    if (kb::assets::TryParseAssetId(reference, id) && id.IsValid()) {
        const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(id);
        return metadata == nullptr || !isExpectedType(*metadata) ? kb::assets::AssetId{} : id;
    }

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath(std::filesystem::path{ reference });
    return metadata == nullptr || !isExpectedType(*metadata) ? kb::assets::AssetId{} : metadata->id;
}

[[nodiscard]] bool IsParticleEffectAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == kb::scene::kParticleEffectAssetType;
}

ScriptFunctionCallResult ParticlesCreate(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("particles api requires an active scene");
    }
    const kb::scene::SceneEntity owner = TargetEntity(context, arguments);
    if (!owner.IsValid() || !context.scene->Entities().IsAlive(owner)) {
        return Error("particles owner entity is not alive");
    }

    const ScriptValue* effectArgument = FindArg(arguments, "effect");
    const std::string effect = effectArgument == nullptr ? std::string{} : effectArgument->AsString();
    const kb::assets::AssetId effectAssetId = ResolveAssetId(*context.scene, effect, &IsParticleEffectAsset);
    if (!effectAssetId.IsValid()) {
        return Error("particle effect asset could not be resolved");
    }

    const kb::particles::ParticleRuntimeResult result = context.scene->Particles().CreateDetailed(effectAssetId.value, owner);
    if (!result.Succeeded()) return Error(ParticleRuntimeError(result.status));

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "instance", ScriptValue{ result.instanceId, ScriptValueType::Hash } } },
        .errors = {},
    };
}

ScriptFunctionCallResult ParticlesRelease(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("particles api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const kb::particles::ParticleRuntimeResult result = context.scene->Particles().ReleaseDetailed(instance);
    if (!result.Succeeded()) return Error(ParticleRuntimeError(result.status));

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "released", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult ParticlesExists(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("particles api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const kb::particles::ParticleRuntimeQueryResult result = kb::particles::ParticlePlayback::Query(*context.scene, instance);
    if (result.status == kb::particles::ParticleRuntimeStatus::BackendUnavailable) return Error(ParticleRuntimeError(result.status));
    const bool exists = result.Succeeded();

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "exists", ScriptValue{ exists } } },
        .errors = {},
    };
}

ScriptFunctionCallResult ParticlesPlay(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("particles api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const kb::particles::ParticleRuntimeResult result = context.scene->Particles().PlayDetailed(instance);
    if (!result.Succeeded()) return Error(ParticleRuntimeError(result.status));

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult ParticlesStop(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("particles api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const kb::particles::ParticleRuntimeResult result = context.scene->Particles().StopDetailed(instance);
    if (!result.Succeeded()) return Error(ParticleRuntimeError(result.status));

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult ParticlesIsPlaying(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("particles api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const kb::particles::ParticleRuntimeQueryResult result = kb::particles::ParticlePlayback::Query(*context.scene, instance);
    if (!result.Succeeded()) return Error(ParticleRuntimeError(result.status));
    const bool playing = result.state;

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "playing", ScriptValue{ playing } } },
        .errors = {},
    };
}

ScriptFunctionCallResult ParticlesSetSeed(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("particles api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const ScriptValue* seedArgument = FindArg(arguments, "seed");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const std::uint64_t seed = seedArgument == nullptr ? 0U : seedArgument->AsUInt64();
    const kb::particles::ParticleRuntimeResult result = context.scene->Particles().SetSeedDetailed(instance, seed);
    if (!result.Succeeded()) return Error(ParticleRuntimeError(result.status));

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult ParticlesSetParameterScalar(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("particles api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const ScriptValue* nameArgument = FindArg(arguments, "name");
    const ScriptValue* valueArgument = FindArg(arguments, "value");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const std::string name = nameArgument == nullptr ? std::string{} : nameArgument->AsString();
    const float value = valueArgument == nullptr ? 0.0F : valueArgument->AsFloat();
    const kb::particles::ParticleRuntimeResult result = context.scene->Particles().SetParameterScalarDetailed(instance, name, value);
    if (!result.Succeeded()) return Error(ParticleRuntimeError(result.status));

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "applied", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult ParticlesClearParameter(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("particles api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const ScriptValue* nameArgument = FindArg(arguments, "name");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const std::string name = nameArgument == nullptr ? std::string{} : nameArgument->AsString();
    const kb::particles::ParticleRuntimeResult result = context.scene->Particles().ClearParameterDetailed(instance, name);
    if (!result.Succeeded()) return Error(ParticleRuntimeError(result.status));

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "cleared", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult ParticlesEmit(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("particles api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const ScriptValue* countArgument = FindArg(arguments, "count");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const int count = countArgument == nullptr ? 0 : countArgument->AsInt();
    if (instance == 0U || count <= 0) return Error("particle emit request is invalid");
    const kb::particles::ParticleRuntimeResult result = context.scene->Particles().EmitDetailed(instance, static_cast<std::uint32_t>(count));
    if (!result.Succeeded()) return Error(ParticleRuntimeError(result.status));

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "emitted", ScriptValue{ true } } },
        .errors = {},
    };
}

ScriptFunctionCallResult ParticlesLiveCount(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("particles api requires an active scene");
    }
    const ScriptValue* instanceArgument = FindArg(arguments, "instance");
    const std::uint64_t instance = instanceArgument == nullptr ? 0U : instanceArgument->AsUInt64();
    const kb::particles::ParticleRuntimeQueryResult result = kb::particles::ParticlePlayback::Query(*context.scene, instance);
    if (!result.Succeeded()) return Error(ParticleRuntimeError(result.status));
    const int count = static_cast<int>(result.liveParticleCount);

    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "count", ScriptValue{ count } } },
        .errors = {},
    };
}

} // namespace

bool ScriptParticleSystemApi::Register(ScriptRuntimeHost& host) {
    ScriptFunctionDesc create;
    create.signature.name = "Particles.Create";
    create.signature.inputs = {
        ScriptFunctionPin{ "effect", ScriptValueType::String, true },
        ScriptFunctionPin{ "entity", ScriptValueType::Entity, false },
    };
    create.signature.outputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
    };
    create.callback = &ParticlesCreate;
    if (!host.RegisterFunction(std::move(create))) {
        return false;
    }

    ScriptFunctionDesc release;
    release.signature.name = "Particles.Release";
    release.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
    };
    release.signature.outputs = {
        ScriptFunctionPin{ "released", ScriptValueType::Bool, true },
    };
    release.callback = &ParticlesRelease;
    if (!host.RegisterFunction(std::move(release))) {
        return false;
    }

    ScriptFunctionDesc exists;
    exists.signature.name = "Particles.Exists";
    exists.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
    };
    exists.signature.outputs = {
        ScriptFunctionPin{ "exists", ScriptValueType::Bool, true },
    };
    exists.callback = &ParticlesExists;
    if (!host.RegisterFunction(std::move(exists))) {
        return false;
    }

    ScriptFunctionDesc play;
    play.signature.name = "Particles.Play";
    play.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
    };
    play.signature.outputs = {
        ScriptFunctionPin{ "set", ScriptValueType::Bool, true },
    };
    play.callback = &ParticlesPlay;
    if (!host.RegisterFunction(std::move(play))) {
        return false;
    }

    ScriptFunctionDesc stop;
    stop.signature.name = "Particles.Stop";
    stop.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
    };
    stop.signature.outputs = {
        ScriptFunctionPin{ "set", ScriptValueType::Bool, true },
    };
    stop.callback = &ParticlesStop;
    if (!host.RegisterFunction(std::move(stop))) {
        return false;
    }

    ScriptFunctionDesc isPlaying;
    isPlaying.signature.name = "Particles.IsPlaying";
    isPlaying.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
    };
    isPlaying.signature.outputs = {
        ScriptFunctionPin{ "playing", ScriptValueType::Bool, true },
    };
    isPlaying.callback = &ParticlesIsPlaying;
    if (!host.RegisterFunction(std::move(isPlaying))) {
        return false;
    }

    ScriptFunctionDesc setSeed;
    setSeed.signature.name = "Particles.SetSeed";
    setSeed.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
        ScriptFunctionPin{ "seed", ScriptValueType::Hash, true },
    };
    setSeed.signature.outputs = {
        ScriptFunctionPin{ "set", ScriptValueType::Bool, true },
    };
    setSeed.callback = &ParticlesSetSeed;
    if (!host.RegisterFunction(std::move(setSeed))) {
        return false;
    }

    ScriptFunctionDesc setParameterScalar;
    setParameterScalar.signature.name = "Particles.SetParameterScalar";
    setParameterScalar.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
        ScriptFunctionPin{ "name", ScriptValueType::String, true },
        ScriptFunctionPin{ "value", ScriptValueType::Float, true },
    };
    setParameterScalar.signature.outputs = {
        ScriptFunctionPin{ "applied", ScriptValueType::Bool, true },
    };
    setParameterScalar.callback = &ParticlesSetParameterScalar;
    if (!host.RegisterFunction(std::move(setParameterScalar))) {
        return false;
    }

    ScriptFunctionDesc clearParameter;
    clearParameter.signature.name = "Particles.ClearParameter";
    clearParameter.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
        ScriptFunctionPin{ "name", ScriptValueType::String, true },
    };
    clearParameter.signature.outputs = {
        ScriptFunctionPin{ "cleared", ScriptValueType::Bool, true },
    };
    clearParameter.callback = &ParticlesClearParameter;
    if (!host.RegisterFunction(std::move(clearParameter))) {
        return false;
    }

    ScriptFunctionDesc emit;
    emit.signature.name = "Particles.Emit";
    emit.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
        ScriptFunctionPin{ "count", ScriptValueType::Int, true },
    };
    emit.signature.outputs = {
        ScriptFunctionPin{ "emitted", ScriptValueType::Bool, true },
    };
    emit.callback = &ParticlesEmit;
    if (!host.RegisterFunction(std::move(emit))) {
        return false;
    }

    ScriptFunctionDesc liveCount;
    liveCount.signature.name = "Particles.LiveCount";
    liveCount.signature.inputs = {
        ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
    };
    liveCount.signature.outputs = {
        ScriptFunctionPin{ "count", ScriptValueType::Int, true },
    };
    liveCount.callback = &ParticlesLiveCount;
    return host.RegisterFunction(std::move(liveCount));
}

} // namespace kb::script
