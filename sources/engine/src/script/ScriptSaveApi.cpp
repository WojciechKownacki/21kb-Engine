#include "engine/script/ScriptSaveApi.hpp"

#include "engine/save/SaveGame.hpp"
#include "engine/save/SaveGameService.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

[[nodiscard]] std::string KeyArg(std::span<const ScriptFunctionArgument> arguments) {
    const ScriptValue* value = FindArg(arguments, "key");
    return value == nullptr ? std::string{} : value->AsString();
}

ScriptFunctionCallResult NoScene() {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { "save api requires an active scene" } };
}

// A save key must be a non-empty string — an empty key is a malformed
// request, honestly rejected rather than silently stored under "".
ScriptFunctionCallResult EmptyKey() {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { "save key must not be empty" } };
}

ScriptFunctionCallResult SetBool(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string key = KeyArg(arguments);
    if (key.empty()) {
        return EmptyKey();
    }
    const ScriptValue* value = FindArg(arguments, "value");
    context.scene->AmbientSave().SetBool(std::move(key), value != nullptr && value->AsBool());
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } }, .errors = {} };
}

ScriptFunctionCallResult SetInt(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string key = KeyArg(arguments);
    if (key.empty()) {
        return EmptyKey();
    }
    const ScriptValue* value = FindArg(arguments, "value");
    context.scene->AmbientSave().SetInt(std::move(key), value == nullptr ? 0 : static_cast<std::int64_t>(value->AsInt()));
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } }, .errors = {} };
}

ScriptFunctionCallResult SetFloat(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string key = KeyArg(arguments);
    if (key.empty()) {
        return EmptyKey();
    }
    const ScriptValue* value = FindArg(arguments, "value");
    context.scene->AmbientSave().SetFloat(std::move(key), value == nullptr ? 0.0 : static_cast<double>(value->AsFloat()));
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } }, .errors = {} };
}

ScriptFunctionCallResult SetString(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string key = KeyArg(arguments);
    if (key.empty()) {
        return EmptyKey();
    }
    const ScriptValue* value = FindArg(arguments, "value");
    context.scene->AmbientSave().SetString(std::move(key), value == nullptr ? std::string{} : value->AsString());
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } }, .errors = {} };
}

ScriptFunctionCallResult GetBool(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    bool value = false;
    const bool found = context.scene->AmbientSave().GetBool(KeyArg(arguments), value);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "found", ScriptValue{ found } }, ScriptFunctionArgument{ "value", ScriptValue{ value } } },
        .errors = {},
    };
}

ScriptFunctionCallResult GetInt(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::int64_t value = 0;
    const bool found = context.scene->AmbientSave().GetInt(KeyArg(arguments), value);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "found", ScriptValue{ found } }, ScriptFunctionArgument{ "value", ScriptValue{ static_cast<int>(value) } } },
        .errors = {},
    };
}

ScriptFunctionCallResult GetFloat(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    double value = 0.0;
    const bool found = context.scene->AmbientSave().GetFloat(KeyArg(arguments), value);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "found", ScriptValue{ found } }, ScriptFunctionArgument{ "value", ScriptValue{ static_cast<float>(value) } } },
        .errors = {},
    };
}

ScriptFunctionCallResult GetString(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string value;
    const bool found = context.scene->AmbientSave().GetString(KeyArg(arguments), value);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "found", ScriptValue{ found } }, ScriptFunctionArgument{ "value", ScriptValue{ std::move(value) } } },
        .errors = {},
    };
}

ScriptFunctionCallResult Has(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const bool has = context.scene->AmbientSave().Has(KeyArg(arguments));
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "has", ScriptValue{ has } } }, .errors = {} };
}

ScriptFunctionCallResult Remove(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const bool removed = context.scene->AmbientSave().Remove(KeyArg(arguments));
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "removed", ScriptValue{ removed } } }, .errors = {} };
}

ScriptFunctionCallResult Clear(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    static_cast<void>(arguments);
    if (context.scene == nullptr) {
        return NoScene();
    }
    context.scene->AmbientSave().Clear();
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "cleared", ScriptValue{ true } } }, .errors = {} };
}

// Serializes the scene's ambient save buffer to `path` atomically at the
// current schema version.
ScriptFunctionCallResult Write(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const ScriptValue* pathValue = FindArg(arguments, "path");
    const std::string path = pathValue == nullptr ? std::string{} : pathValue->AsString();
    const bool written = !path.empty() && kb::save::SaveGameService::Save(std::filesystem::path{ path }, context.scene->AmbientSave());
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "written", ScriptValue{ written } } }, .errors = {} };
}

// Loads a save file from `path` into the scene's ambient buffer (replacing
// it), migrating an older schema up to current. `loaded` is the success flag;
// `status` names the precise outcome ("Ok"/"FileNotFound"/"BadMagic"/...).
ScriptFunctionCallResult Read(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const ScriptValue* pathValue = FindArg(arguments, "path");
    const std::string path = pathValue == nullptr ? std::string{} : pathValue->AsString();

    const char* status = "FileNotFound";
    bool loaded = false;
    if (!path.empty()) {
        kb::save::SaveGameLoadResult result = kb::save::SaveGameService::Load(std::filesystem::path{ path });
        loaded = result.Succeeded();
        switch (result.status) {
        case kb::save::SaveGameLoadStatus::Ok:
            status = "Ok";
            break;
        case kb::save::SaveGameLoadStatus::FileNotFound:
            status = "FileNotFound";
            break;
        case kb::save::SaveGameLoadStatus::BadMagic:
            status = "BadMagic";
            break;
        case kb::save::SaveGameLoadStatus::UnsupportedVersion:
            status = "UnsupportedVersion";
            break;
        case kb::save::SaveGameLoadStatus::Corrupt:
            status = "Corrupt";
            break;
        case kb::save::SaveGameLoadStatus::MigrationFailed:
            status = "MigrationFailed";
            break;
        }
        if (loaded) {
            context.scene->AmbientSave() = std::move(result.save);
        }
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "loaded", ScriptValue{ loaded } }, ScriptFunctionArgument{ "status", ScriptValue{ std::string{ status } } } },
        .errors = {},
    };
}

bool RegisterFunction(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> inputs, std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptSaveApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    const ScriptFunctionPin keyPin{ "key", ScriptValueType::String, true };
    ok = RegisterFunction(host, "Save.SetBool", { keyPin, ScriptFunctionPin{ "value", ScriptValueType::Bool, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetBool) && ok;
    ok = RegisterFunction(host, "Save.SetInt", { keyPin, ScriptFunctionPin{ "value", ScriptValueType::Int, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetInt) && ok;
    ok = RegisterFunction(host, "Save.SetFloat", { keyPin, ScriptFunctionPin{ "value", ScriptValueType::Float, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetFloat) && ok;
    ok = RegisterFunction(host, "Save.SetString", { keyPin, ScriptFunctionPin{ "value", ScriptValueType::String, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetString) && ok;
    ok = RegisterFunction(host, "Save.GetBool", { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::Bool, true } }, &GetBool) && ok;
    ok = RegisterFunction(host, "Save.GetInt", { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::Int, true } }, &GetInt) && ok;
    ok = RegisterFunction(host, "Save.GetFloat", { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::Float, true } }, &GetFloat) && ok;
    ok = RegisterFunction(host, "Save.GetString", { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::String, true } }, &GetString) && ok;
    ok = RegisterFunction(host, "Save.Has", { keyPin }, { ScriptFunctionPin{ "has", ScriptValueType::Bool, true } }, &Has) && ok;
    ok = RegisterFunction(host, "Save.Remove", { keyPin }, { ScriptFunctionPin{ "removed", ScriptValueType::Bool, true } }, &Remove) && ok;
    ok = RegisterFunction(host, "Save.Clear", {}, { ScriptFunctionPin{ "cleared", ScriptValueType::Bool, true } }, &Clear) && ok;
    ok = RegisterFunction(host, "Save.Write", { ScriptFunctionPin{ "path", ScriptValueType::String, true } }, { ScriptFunctionPin{ "written", ScriptValueType::Bool, true } }, &Write) && ok;
    ok = RegisterFunction(host, "Save.Read", { ScriptFunctionPin{ "path", ScriptValueType::String, true } }, { ScriptFunctionPin{ "loaded", ScriptValueType::Bool, true }, ScriptFunctionPin{ "status", ScriptValueType::String, true } }, &Read) && ok;
    return ok;
}

} // namespace kb::script
