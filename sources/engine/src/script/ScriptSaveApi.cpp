#include "engine/script/ScriptSaveApi.hpp"

#include "engine/platform/UserStorage.hpp"
#include "engine/platform/CrashReportConsent.hpp"
#include "engine/platform/CrashReporting.hpp"
#include "engine/save/SaveDomain.hpp"
#include "engine/save/SaveGame.hpp"
#include "engine/save/SaveGameService.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
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
ScriptFunctionCallResult SetAsset(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string key = KeyArg(arguments);
    if (key.empty()) {
        return EmptyKey();
    }
    const ScriptValue* value = FindArg(arguments, "value");
    Buffer<Domain>(*context.scene).SetAssetRef(std::move(key), kb::assets::AssetId{ value == nullptr ? 0U : value->AsUInt64() });
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
ScriptFunctionCallResult GetAsset(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    kb::assets::AssetId value;
    const bool found = Buffer<Domain>(*context.scene).GetAssetRef(KeyArg(arguments), value);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ found } },
            ScriptFunctionArgument{ "value", ScriptValue{ value.value, ScriptValueType::Hash } },
        },
        .errors = {},
    };
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

using StoragePtr = std::shared_ptr<kb::platform::UserStorage>;

// Scripts never name a file: a slot is a bare name inside the host's per-game
// user storage, and the domain picks the extension, so a save slot and a
// settings slot of the same name are different files.
template <kb::save::SaveDomain Domain>
[[nodiscard]] std::string SlotStorageKey(std::string_view slot) {
    return std::string{ slot } + (Domain == kb::save::SaveDomain::SaveGame ? ".kbsave" : ".kbsettings");
}

// Returns the error that refuses this call, or nothing when `slot` may be used.
[[nodiscard]] std::optional<ScriptFunctionCallResult> RefuseSlot(const StoragePtr& storage, std::span<const ScriptFunctionArgument> arguments, std::string& slot) {
    const ScriptValue* slotValue = FindArg(arguments, "slot");
    slot = slotValue == nullptr ? std::string{} : slotValue->AsString();
    if (!kb::platform::IsUserStorageSlotName(slot)) {
        return ScriptFunctionCallResult{ .executed = false, .outputs = {},
            .errors = { "save slot '" + slot + "' is not a slot name: use 1-" + std::to_string(kb::platform::kMaxUserStorageSlotNameBytes) +
                " letters, digits, '_' or '-' (paths, separators, drive letters and device names are refused)" } };
    }
    if (storage == nullptr) {
        return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { "persistent user storage is not configured for this script runtime" } };
    }
    return std::nullopt;
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult Write(const StoragePtr& storage, const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string slot;
    if (std::optional<ScriptFunctionCallResult> refused = RefuseSlot(storage, arguments, slot)) {
        return *std::move(refused);
    }
    const std::optional<std::vector<std::uint8_t>> bytes = kb::save::SaveGameService::Serialize(Buffer<Domain>(*context.scene), Domain);
    const bool written = bytes.has_value() &&
        storage->Write(SlotStorageKey<Domain>(slot), std::string_view{ reinterpret_cast<const char*>(bytes->data()), bytes->size() });
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
    case kb::save::SaveGameLoadStatus::TooLarge:
        return "TooLarge";
    case kb::save::SaveGameLoadStatus::IntegrityMismatch:
        return "IntegrityMismatch";
    case kb::save::SaveGameLoadStatus::Tampered:
        return "Tampered";
    }
    return "FileNotFound";
}

template <kb::save::SaveDomain Domain>
ScriptFunctionCallResult Read(const StoragePtr& storage, const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string slot;
    if (std::optional<ScriptFunctionCallResult> refused = RefuseSlot(storage, arguments, slot)) {
        return *std::move(refused);
    }

    const char* status = "FileNotFound";
    std::string diagnostic = "save slot '" + slot + "' has not been written";
    bool loaded = false;
    if (const std::optional<std::string> stored = storage->Read(SlotStorageKey<Domain>(slot))) {
        // Loading REQUIRES the matching domain — a Save.Read of a settings
        // file (or vice versa) reports WrongDomain, never silently loads the
        // wrong category of data into the wrong buffer.
        kb::save::SaveGameLoadResult result = kb::save::SaveGameService::Deserialize(
            std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(stored->data()), stored->size() }, Domain);
        loaded = result.Succeeded();
        status = StatusName(result.status);
        diagnostic = std::move(result.diagnostic);
        if (loaded) {
            Buffer<Domain>(*context.scene) = std::move(result.save);
        }
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "loaded", ScriptValue{ loaded } },
            ScriptFunctionArgument{ "status", ScriptValue{ std::string{ status } } },
            ScriptFunctionArgument{ "diagnostic", ScriptValue{ std::move(diagnostic) } },
        },
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
// (e.g. "Save" or "Settings"), all targeting the `Domain` buffer. Write/Read
// persist through the host's user storage, captured once here.
template <kb::save::SaveDomain Domain>
bool RegisterDomain(ScriptRuntimeHost& host, std::string_view prefix) {
    bool ok = true;
    const StoragePtr storage = host.UserStorage();
    const ScriptFunctionPin keyPin{ "key", ScriptValueType::String, true };
    const ScriptFunctionPin slotPin{ "slot", ScriptValueType::String, true };
    const auto name = [prefix](std::string_view fn) { return std::string{ prefix } + "." + std::string{ fn }; };
    ok = RegisterFunction(host, name("SetBool"), { keyPin, ScriptFunctionPin{ "value", ScriptValueType::Bool, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetBool<Domain>) && ok;
    ok = RegisterFunction(host, name("SetInt"), { keyPin, ScriptFunctionPin{ "value", ScriptValueType::Int, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetInt<Domain>) && ok;
    ok = RegisterFunction(host, name("SetFloat"), { keyPin, ScriptFunctionPin{ "value", ScriptValueType::Float, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetFloat<Domain>) && ok;
    ok = RegisterFunction(host, name("SetString"), { keyPin, ScriptFunctionPin{ "value", ScriptValueType::String, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetString<Domain>) && ok;
    ok = RegisterFunction(host, name("SetAsset"), { keyPin, ScriptFunctionPin{ "value", ScriptValueType::Hash, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetAsset<Domain>) && ok;
    ok = RegisterFunction(host, name("GetBool"), { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::Bool, true } }, &GetBool<Domain>) && ok;
    ok = RegisterFunction(host, name("GetInt"), { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::Int, true } }, &GetInt<Domain>) && ok;
    ok = RegisterFunction(host, name("GetFloat"), { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::Float, true } }, &GetFloat<Domain>) && ok;
    ok = RegisterFunction(host, name("GetString"), { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::String, true } }, &GetString<Domain>) && ok;
    ok = RegisterFunction(host, name("GetAsset"), { keyPin }, { ScriptFunctionPin{ "found", ScriptValueType::Bool, true }, ScriptFunctionPin{ "value", ScriptValueType::Hash, true } }, &GetAsset<Domain>) && ok;
    ok = RegisterFunction(host, name("Has"), { keyPin }, { ScriptFunctionPin{ "has", ScriptValueType::Bool, true } }, &Has<Domain>) && ok;
    ok = RegisterFunction(host, name("Remove"), { keyPin }, { ScriptFunctionPin{ "removed", ScriptValueType::Bool, true } }, &Remove<Domain>) && ok;
    ok = RegisterFunction(host, name("Clear"), {}, { ScriptFunctionPin{ "cleared", ScriptValueType::Bool, true } }, &Clear<Domain>) && ok;
    ok = RegisterFunction(host, name("Write"), { slotPin }, { ScriptFunctionPin{ "written", ScriptValueType::Bool, true } },
        [storage](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) { return Write<Domain>(storage, context, arguments); }) && ok;
    ok = RegisterFunction(host, name("Read"), { slotPin },
        { ScriptFunctionPin{ "loaded", ScriptValueType::Bool, true }, ScriptFunctionPin{ "status", ScriptValueType::String, true },
            ScriptFunctionPin{ "diagnostic", ScriptValueType::String, true } },
        [storage](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) { return Read<Domain>(storage, context, arguments); }) && ok;
    return ok;
}

// Crash reports belong with the player's settings: whether they may leave the
// machine, what they hold, and removing the ones already written. All of it acts
// on the reporter the host installed; without one (a host that writes no
// reports) consent reads as off and nothing is deleted. With user storage the
// answer is kept there too, where the game's own consent prompt reads it, and
// it counts only while both agree.
ScriptFunctionCallResult CrashReportUploadConsent(const std::shared_ptr<kb::platform::UserStorage>& storage) {
    bool consent = false;
#if defined(_WIN32)
    const std::filesystem::path directory = kb::platform::CrashReporter::ReportDirectory();
    consent = !directory.empty() && kb::platform::HasCrashUploadConsent(directory) &&
        (storage == nullptr ||
            kb::platform::ReadCrashUploadConsentChoice(*storage) == kb::platform::CrashUploadConsentChoice::Granted);
#else
    static_cast<void>(storage);
#endif
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "consent", ScriptValue{ consent } } }, .errors = {} };
}

// The answer takes effect for reports sent from the next launch on; this call never sends anything.
ScriptFunctionCallResult SetCrashReportUploadConsent(const std::shared_ptr<kb::platform::UserStorage>& storage,
    std::span<const ScriptFunctionArgument> arguments) {
    const ScriptValue* value = FindArg(arguments, "consent");
    bool set = false;
#if defined(_WIN32)
    const std::filesystem::path directory = kb::platform::CrashReporter::ReportDirectory();
    if (value != nullptr && !directory.empty()) {
        const bool consent = value->AsBool();
        set = (storage == nullptr || kb::platform::WriteCrashUploadConsentChoice(*storage, consent)) &&
            kb::platform::SetCrashUploadConsent(directory, consent);
    }
#else
    static_cast<void>(storage);
    static_cast<void>(value);
#endif
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ set } } }, .errors = {} };
}

ScriptFunctionCallResult CrashReportPrivacyNotice(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument>) {
    std::string text;
#if defined(_WIN32)
    text = kb::platform::ReadCrashReportPrivacyNotice();
#endif
    return ScriptFunctionCallResult{ .executed = true, .outputs = { ScriptFunctionArgument{ "text", ScriptValue{ std::move(text) } } }, .errors = {} };
}

ScriptFunctionCallResult DeleteCrashReports(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument>) {
    std::size_t deleted = 0U;
#if defined(_WIN32)
    const std::filesystem::path directory = kb::platform::CrashReporter::ReportDirectory();
    deleted = directory.empty() ? 0U : kb::platform::DeleteCrashReports(directory);
#endif
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "deleted", ScriptValue{ static_cast<int>(std::min<std::size_t>(deleted, 1'000'000U)) } } },
        .errors = {},
    };
}

bool RegisterCrashReportSettings(ScriptRuntimeHost& host) {
    bool ok = true;
    const std::shared_ptr<kb::platform::UserStorage> storage = host.UserStorage();
    ok = RegisterFunction(host, "Settings.CrashReportUploadConsent", {}, { ScriptFunctionPin{ "consent", ScriptValueType::Bool, true } },
        [storage](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument>) { return CrashReportUploadConsent(storage); }) && ok;
    ok = RegisterFunction(host, "Settings.SetCrashReportUploadConsent", { ScriptFunctionPin{ "consent", ScriptValueType::Bool, true } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } },
        [storage](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) { return SetCrashReportUploadConsent(storage, arguments); }) && ok;
    ok = RegisterFunction(host, "Settings.CrashReportPrivacyNotice", {}, { ScriptFunctionPin{ "text", ScriptValueType::String, true } }, &CrashReportPrivacyNotice) && ok;
    ok = RegisterFunction(host, "Settings.DeleteCrashReports", {}, { ScriptFunctionPin{ "deleted", ScriptValueType::Int, true } }, &DeleteCrashReports) && ok;
    return ok;
}

} // namespace

bool ScriptSaveApi::Register(ScriptRuntimeHost& host) {
    // LIB-162 "Save" (game progress) + LIB-163 "Settings" (user preferences) —
    // two separate script surfaces over two separate ambient buffers and two
    // separate save domains.
    bool ok = RegisterDomain<kb::save::SaveDomain::SaveGame>(host, "Save");
    ok = RegisterDomain<kb::save::SaveDomain::UserSettings>(host, "Settings") && ok;
    ok = RegisterCrashReportSettings(host) && ok;
    return ok;
}

} // namespace kb::script
