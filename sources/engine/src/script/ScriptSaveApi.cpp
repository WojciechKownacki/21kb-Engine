#include "engine/script/ScriptSaveApi.hpp"

#include "engine/save/SaveDomain.hpp"
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

ScriptFunctionCallResult EmptyKey() {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { "save key must not be empty" } };
}

// LIB-163: the same Save.* / Settings.* operations, parameterized by the
// persistence domain so a single implementation drives BOTH the game-progress
// buffer and the separate user-settings buffer. Each template instantiation
// is a distinct callback the registry gets, targeting the matching ambient
// buffer and stamping the matching save domain on disk.
template <kb::save::SaveDomain Domain>
[[nodiscard]] kb::save::SaveGame& Buffer(kb::scene::Scene& scene) noexcept {
    if constexpr (Domain == kb::save::SaveDomain::SaveGame) {
        return scene.AmbientSave();
    } else {
        return scene.AmbientSettings();
    }
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult SetBool(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string key = KeyArg(arguments);
    if (key.empty()) {
        return EmptyKey();
    }
    const ScriptValue* value = FindArg(arguments, "value");
    Buffer<Domain>(*context.scene).SetBool(std::move(key), value != nullptr && value->AsBool());
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } }, .errors = {} };
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult SetInt(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string key = KeyArg(arguments);
    if (key.empty()) {
        return EmptyKey();
    }
    const ScriptValue* value = FindArg(arguments, "value");
    Buffer<Domain>(*context.scene).SetInt(std::move(key), value == nullptr ? 0 : static_cast<std::int64_t>(value->AsInt()));
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } }, .errors = {} };
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult SetFloat(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string key = KeyArg(arguments);
    if (key.empty()) {
        return EmptyKey();
    }
    const ScriptValue* value = FindArg(arguments, "value");
    Buffer<Domain>(*context.scene).SetFloat(std::move(key), value == nullptr ? 0.0 : static_cast<double>(value->AsFloat()));
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } }, .errors = {} };
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult SetString(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string key = KeyArg(arguments);
    if (key.empty()) {
        return EmptyKey();
    }
    const ScriptValue* value = FindArg(arguments, "value");
    Buffer<Domain>(*context.scene).SetString(std::move(key), value == nullptr ? std::string{} : value->AsString());
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } }, .errors = {} };
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult GetBool(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    bool value = false;
    const bool found = Buffer<Domain>(*context.scene).GetBool(KeyArg(arguments), value);
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "found", ScriptValue{ found } }, ScriptFunctionArgument{ "value", ScriptValue{ value } } }, .errors = {} };
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult GetInt(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::int64_t value = 0;
    const bool found = Buffer<Domain>(*context.scene).GetInt(KeyArg(arguments), value);
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "found", ScriptValue{ found } }, ScriptFunctionArgument{ "value", ScriptValue{ static_cast<int>(value) } } }, .errors = {} };
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult GetFloat(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    double value = 0.0;
    const bool found = Buffer<Domain>(*context.scene).GetFloat(KeyArg(arguments), value);
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "found", ScriptValue{ found } }, ScriptFunctionArgument{ "value", ScriptValue{ static_cast<float>(value) } } }, .errors = {} };
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult GetString(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string value;
    const bool found = Buffer<Domain>(*context.scene).GetString(KeyArg(arguments), value);
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "found", ScriptValue{ found } }, ScriptFunctionArgument{ "value", ScriptValue{ std::move(value) } } }, .errors = {} };
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult Has(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const bool has = Buffer<Domain>(*context.scene).Has(KeyArg(arguments));
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "has", ScriptValue{ has } } }, .errors = {} };
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult Remove(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const bool removed = Buffer<Domain>(*context.scene).Remove(KeyArg(arguments));
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "removed", ScriptValue{ removed } } }, .errors = {} };
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult Clear(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    static_cast<void>(arguments);
    if (context.scene == nullptr) {
        return NoScene();
    }
    Buffer<Domain>(*context.scene).Clear();
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "cleared", ScriptValue{ true } } }, .errors = {} };
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult Write(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const ScriptValue* pathValue = FindArg(arguments, "path");
    const std::string path = pathValue == nullptr ? std::string{} : pathValue->AsString();
    const bool written = !path.empty() && kb::save::SaveGameService::Save(std::filesystem::path{ path }, Buffer<Domain>(*context.scene), Domain);
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "written", ScriptValue{ written } } }, .errors = {} };
}

[[nodiscard]] const char* StatusName(kb::save::SaveGameLoadStatus status) noexcept {
    switch (status) {
    case kb::save::SaveGameLoadStatus::Ok:
        return "Ok";
    case kb::save::SaveGameLoadStatus::FileNotFound:
        return "FileNotFound";
    case kb::save::SaveGameLoadStatus::BadMagic:
        return "BadMagic";
    case kb::save::SaveGameLoadStatus::UnsupportedVersion:
        return "UnsupportedVersion";
    case kb::save::SaveGameLoadStatus::Corrupt:
        return "Corrupt";
    case kb::save::SaveGameLoadStatus::MigrationFailed:
        return "MigrationFailed";
    case kb::save::SaveGameLoadStatus::WrongDomain:
        return "WrongDomain";
    }
    return "FileNotFound";
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult Read(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const ScriptValue* pathValue = FindArg(arguments, "path");
    const std::string path = pathValue == nullptr ? std::string{} : pathValue->AsString();

    const char* status = "FileNotFound";
    bool loaded = false;
    if (!path.empty()) {
        // Loading REQUIRES the matching domain — a Save.Read of a settings
        // file (or vice versa) reports WrongDomain, never silently loads the
        // wrong category of data into the wrong buffer.
        kb::save::SaveGameLoadResult result = kb::save::SaveGameService::Load(std::filesystem::path{ path }, Domain);
        loaded = result.Succeeded();
        status = StatusName(result.status);
        if (loaded) {
            Buffer<Domain>(*context.scene) = std::move(result.save);
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

// Registers the full scalar key/value surface (SetBool/Int/Float/String,
// GetBool/Int/Float/String, Has, Remove, Clear, Write, Read) under `prefix`
// (e.g. "Save" or "Settings"), all targeting the `Domain` buffer.
template <kb::save::SaveDomain Domain>
bool RegisterDomain(ScriptRuntimeHost& host, std::string_view prefix) {
    bool ok = true;
    const ScriptFunctionPin keyPin{ "key", ScriptValueType::String, true };
    const auto name = [prefix](std::string_view fn) { return std::string{ prefix } + "." + std::string{ fn }; };
    ok = RegisterFunction(host, name("SetBool"), { keyPin, ScriptFunctionPin{ "value", ScriptValueType::Bool, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetBool<Domain>) && ok;
    ok = RegisterFunction(host, name("SetInt"), { keyPin, ScriptFunctionPin{ "value", ScriptValueType::Int, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetInt<Domain>) && ok;
    ok = RegisterFunction(host, name("SetFloat"), { keyPin, ScriptFunctionPin{ "value", ScriptValueType::Float, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetFloat<Domain>) && ok;
    ok = RegisterFunction(host, name("SetString"), { keyPin, ScriptFunctionPin{ "value", ScriptValueType::String, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetString<Domain>) && ok;
    ok = RegisterFunction(host, name("GetBool"), { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::Bool, true } }, &GetBool<Domain>) && ok;
    ok = RegisterFunction(host, name("GetInt"), { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::Int, true } }, &GetInt<Domain>) && ok;
    ok = RegisterFunction(host, name("GetFloat"), { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::Float, true } }, &GetFloat<Domain>) && ok;
    ok = RegisterFunction(host, name("GetString"), { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::String, true } }, &GetString<Domain>) && ok;
    ok = RegisterFunction(host, name("Has"), { keyPin }, { ScriptFunctionPin{ "has", ScriptValueType::Bool, true } }, &Has<Domain>) && ok;
    ok = RegisterFunction(host, name("Remove"), { keyPin }, { ScriptFunctionPin{ "removed", ScriptValueType::Bool, true } }, &Remove<Domain>) && ok;
    ok = RegisterFunction(host, name("Clear"), {}, { ScriptFunctionPin{ "cleared", ScriptValueType::Bool, true } }, &Clear<Domain>) && ok;
    ok = RegisterFunction(host, name("Write"), { ScriptFunctionPin{ "path", ScriptValueType::String, true } }, { ScriptFunctionPin{ "written", ScriptValueType::Bool, true } }, &Write<Domain>) && ok;
    ok = RegisterFunction(host, name("Read"), { ScriptFunctionPin{ "path", ScriptValueType::String, true } }, { ScriptFunctionPin{ "loaded", ScriptValueType::Bool, true }, ScriptFunctionPin{ "status", ScriptValueType::String, true } }, &Read<Domain>) && ok;
    return ok;
}

} // namespace

bool ScriptSaveApi::Register(ScriptRuntimeHost& host) {
    // LIB-162 "Save" (game progress) + LIB-163 "Settings" (user preferences) —
    // two separate script surfaces over two separate ambient buffers and two
    // separate save domains.
    bool ok = RegisterDomain<kb::save::SaveDomain::SaveGame>(host, "Save");
    ok = RegisterDomain<kb::save::SaveDomain::UserSettings>(host, "Settings") && ok;
    return ok;
}

} // namespace kb::script
