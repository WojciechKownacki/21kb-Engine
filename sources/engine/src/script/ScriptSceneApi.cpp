#include "engine/script/ScriptSceneApi.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <utility>

namespace kb::script {
namespace {

[[nodiscard]] const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string StringArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? std::string{} : value->AsString();
}

[[nodiscard]] std::uint64_t HashArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? 0U : value->AsUInt64(0U);
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

ScriptFunctionCallResult NoScene() {
    return Error("scene api requires an active scene");
}

ScriptFunctionCallResult HashResult(std::string_view pin, std::uint64_t id) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ id, ScriptValueType::Hash } } },
        .errors = {},
    };
}

ScriptFunctionCallResult BoolResult(std::string_view pin, bool value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ value } } },
        .errors = {},
    };
}

// LIB-071: Scene.Load(path, additive=false) -> {id: Hash}. `id` is 0
// (never a valid handle — real ids start at 1, kb::scene::SceneState::
// nextLoadedSceneId) on any failure: unreadable file, wrong extension,
// or an empty worldPrefab. `additive=false` (the default) REPLACES the
// scene's entire content, same destructive behaviour
// kb::scene::SceneDocumentService::LoadIntoScene already had before this
// task — a script that only ever calls Load without additive=true sees
// no behavioural change.
ScriptFunctionCallResult Load(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string pathText = StringArg(arguments, "path");
    if (pathText.empty()) {
        return Error("scene path is empty");
    }
    std::filesystem::path path{ pathText };
    if (pathText.front() == '/') {
        const kb::assets::AssetMetadata* metadata =
            context.scene->Assets().Manager().Registry().FindByPath(path);
        if (metadata == nullptr || metadata->physicalPath.empty()) {
            return Error("scene asset could not be resolved: " + pathText);
        }
        path = metadata->physicalPath;
    }
    const ScriptValue* additiveValue = FindArg(arguments, "additive");
    const bool additive = additiveValue != nullptr && additiveValue->AsBool(false);
    const std::uint64_t id = context.scene->LoadedContent().Load(path, additive);
    if (id == 0U) {
        return Error("scene could not be loaded: " + pathText);
    }
    return HashResult("id", id);
}

ScriptFunctionCallResult Unload(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return BoolResult("unloaded", context.scene->LoadedContent().Unload(HashArg(arguments, "id")));
}

ScriptFunctionCallResult SetActive(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return BoolResult("set", context.scene->LoadedContent().SetActive(HashArg(arguments, "id")));
}

// LIB-071: the read-only counterpart to SetActive — without this, a
// script could set the active loaded-scene handle but never observe it
// again, making SetActive's effect unverifiable from script.
ScriptFunctionCallResult GetActive(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return HashResult("id", context.scene->LoadedContent().ActiveScene());
}

ScriptFunctionCallResult Find(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return HashResult("id", context.scene->LoadedContent().Find(StringArg(arguments, "name")));
}

// LIB-071 (documented, honest scope): loads are synchronous today (see
// kb::scene::SceneLoadedContentService's own comment) — Progress always
// reports 1.0 for a currently-loaded id and 0.0 for an unknown one. This
// is real forward-compatible API surface for a future async loader, not
// a fabricated in-between percentage.
ScriptFunctionCallResult LoadProgress(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "progress", ScriptValue{ context.scene->LoadedContent().Progress(HashArg(arguments, "id")) } } },
        .errors = {},
    };
}

bool RegisterFunction(
    ScriptRuntimeHost& host,
    std::string name,
    std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs,
    ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptSceneApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "Scene.Load",
        { ScriptFunctionPin{ "path", ScriptValueType::String, true }, ScriptFunctionPin{ "additive", ScriptValueType::Bool, false } },
        { ScriptFunctionPin{ "id", ScriptValueType::Hash, true } },
        &Load) && ok;
    ok = RegisterFunction(host, "Scene.Unload",
        { ScriptFunctionPin{ "id", ScriptValueType::Hash, true } },
        { ScriptFunctionPin{ "unloaded", ScriptValueType::Bool, true } },
        &Unload) && ok;
    ok = RegisterFunction(host, "Scene.SetActive",
        { ScriptFunctionPin{ "id", ScriptValueType::Hash, true } },
        { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } },
        &SetActive) && ok;
    ok = RegisterFunction(host, "Scene.GetActive",
        {},
        { ScriptFunctionPin{ "id", ScriptValueType::Hash, true } },
        &GetActive) && ok;
    ok = RegisterFunction(host, "Scene.Find",
        { ScriptFunctionPin{ "name", ScriptValueType::String, true } },
        { ScriptFunctionPin{ "id", ScriptValueType::Hash, true } },
        &Find) && ok;
    ok = RegisterFunction(host, "Scene.LoadProgress",
        { ScriptFunctionPin{ "id", ScriptValueType::Hash, true } },
        { ScriptFunctionPin{ "progress", ScriptValueType::Float, true } },
        &LoadProgress) && ok;
    return ok;
}

} // namespace kb::script
