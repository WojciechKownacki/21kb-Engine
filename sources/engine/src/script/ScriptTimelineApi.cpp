#include "engine/script/ScriptTimelineApi.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTimelines.hpp"
#include "engine/scene/TimelineAssetIO.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <utility>

namespace kb::script {
namespace {

const ScriptValue* Arg(
    std::span<const ScriptFunctionArgument> arguments,
    std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) return &argument.value;
    }
    return nullptr;
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{
        .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

ScriptFunctionCallResult Applied(bool applied) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { { "applied", ScriptValue{ applied } } },
        .errors = {},
    };
}

kb::assets::AssetId ResolveTimeline(
    kb::scene::Scene& scene, std::string_view reference) {
    kb::assets::AssetId id{};
    const kb::assets::AssetMetadata* metadata = nullptr;
    if (kb::assets::TryParseAssetId(reference, id) && id.IsValid()) {
        metadata = scene.Assets().Manager().Registry().Find(id);
    } else {
        metadata = scene.Assets().Manager().Registry().FindByPath(
            std::filesystem::path{ reference });
    }
    return metadata != nullptr &&
            metadata->type == kb::scene::kTimelineAssetType
        ? metadata->id
        : kb::assets::AssetId{};
}

std::uint64_t Instance(
    std::span<const ScriptFunctionArgument> arguments) {
    const ScriptValue* value = Arg(arguments, "instance");
    return value == nullptr ? 0U : value->AsUInt64();
}

ScriptFunctionCallResult Create(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("Timeline.Create requires an active scene");
    }
    const ScriptValue* assetValue = Arg(arguments, "asset");
    const kb::assets::AssetId asset = ResolveTimeline(
        *context.scene,
        assetValue == nullptr ? std::string{} : assetValue->AsString());
    const ScriptValue* entityValue = Arg(arguments, "entity");
    const kb::scene::SceneEntity owner = entityValue == nullptr
        ? context.caller
        : kb::scene::SceneEntity{ entityValue->AsUInt64() };
    if (!asset.IsValid() ||
        !context.scene->Entities().IsAlive(owner)) {
        return Error("Timeline.Create requires a timeline asset and live owner");
    }
    const std::uint64_t instance =
        context.scene->Timelines().Create(asset.value, owner);
    if (instance == 0U) {
        return Error("Timeline.Create could not resolve all authored bindings");
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            { "instance",
              ScriptValue{ instance, ScriptValueType::Hash } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult Release(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    return context.scene == nullptr
        ? Error("Timeline.Release requires an active scene")
        : Applied(context.scene->Timelines().Release(Instance(arguments)));
}

ScriptFunctionCallResult Play(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    return context.scene == nullptr
        ? Error("Timeline.Play requires an active scene")
        : Applied(context.scene->Timelines().Play(Instance(arguments)));
}

ScriptFunctionCallResult Pause(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    return context.scene == nullptr
        ? Error("Timeline.Pause requires an active scene")
        : Applied(context.scene->Timelines().Pause(Instance(arguments)));
}

ScriptFunctionCallResult Seek(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    return context.scene == nullptr
        ? Error("Timeline.Seek requires an active scene")
        : Applied(context.scene->Timelines().Seek(
              Instance(arguments), Arg(arguments, "time")->AsFloat()));
}

ScriptFunctionCallResult Skip(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("Timeline.Skip requires an active scene");
    }
    const bool emitMarkers =
        Arg(arguments, "emitMarkers") != nullptr &&
        Arg(arguments, "emitMarkers")->AsBool();
    return Applied(context.scene->Timelines().Skip(
        Instance(arguments), Arg(arguments, "time")->AsFloat(),
        emitMarkers
            ? kb::scene::TimelineSkipMarkerPolicy::EmitCrossed
            : kb::scene::TimelineSkipMarkerPolicy::Suppress));
}

ScriptFunctionCallResult Bind(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("Timeline.Bind requires an active scene");
    }
    return Applied(context.scene->Timelines().Bind(
        Instance(arguments), Arg(arguments, "binding")->AsString(),
        kb::scene::SceneEntity{ Arg(arguments, "entity")->AsUInt64() }));
}

ScriptFunctionCallResult IsPlaying(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("Timeline.IsPlaying requires an active scene");
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            { "playing",
              ScriptValue{ context.scene->Timelines().IsPlaying(
                  Instance(arguments)) } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult Time(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return Error("Timeline.Time requires an active scene");
    }
    if (!context.scene->Timelines().Exists(Instance(arguments))) {
        return Error("Timeline.Time instance does not exist");
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            { "time",
              ScriptValue{ context.scene->Timelines().Time(
                  Instance(arguments)) } },
        },
        .errors = {},
    };
}

bool RegisterFunction(
    ScriptRuntimeHost& host, std::string name,
    std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs,
    ScriptFunctionCallback callback) {
    ScriptFunctionDesc function{};
    function.signature.name = std::move(name);
    function.signature.inputs = std::move(inputs);
    function.signature.outputs = std::move(outputs);
    function.callback = std::move(callback);
    return host.RegisterFunction(std::move(function));
}

} // namespace

bool ScriptTimelineApi::Register(ScriptRuntimeHost& host) {
    const ScriptFunctionPin instance{
        "instance", ScriptValueType::Hash, true };
    const std::vector<ScriptFunctionPin> applied{
        { "applied", ScriptValueType::Bool, true } };
    return RegisterFunction(host, "Timeline.Create", {
            { "asset", ScriptValueType::String, true },
            { "entity", ScriptValueType::Entity, false },
        }, { instance }, &Create) &&
        RegisterFunction(host, "Timeline.Release", { instance }, applied, &Release) &&
        RegisterFunction(host, "Timeline.Play", { instance }, applied, &Play) &&
        RegisterFunction(host, "Timeline.Pause", { instance }, applied, &Pause) &&
        RegisterFunction(host, "Timeline.Seek", {
            instance, { "time", ScriptValueType::Float, true },
        }, applied, &Seek) &&
        RegisterFunction(host, "Timeline.Skip", {
            instance,
            { "time", ScriptValueType::Float, true },
            { "emitMarkers", ScriptValueType::Bool, true },
        }, applied, &Skip) &&
        RegisterFunction(host, "Timeline.Bind", {
            instance,
            { "binding", ScriptValueType::String, true },
            { "entity", ScriptValueType::Entity, true },
        }, applied, &Bind) &&
        RegisterFunction(host, "Timeline.IsPlaying", { instance }, {
            { "playing", ScriptValueType::Bool, true },
        }, &IsPlaying) &&
        RegisterFunction(host, "Timeline.Time", { instance }, {
            { "time", ScriptValueType::Float, true },
        }, &Time);
}

} // namespace kb::script
