#include "app/EditorAutomationScenarioRunner.hpp"

#if defined(_WIN32)
#include "app/EditorHeadlessAutomation.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputHaptics.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/gameplay/GameInstance.hpp"
#include "engine/core/EngineLog.hpp"
#include "engine/core/EngineAssertions.hpp"
#include "engine/core/DebugDraw.hpp"
#include "engine/core/ProfilerCounters.hpp"
#include "engine/network/NetworkModel.hpp"
#include "engine/network/NetworkBudget.hpp"
#include "engine/network/NetworkObject.hpp"
#include "engine/network/NetworkPrediction.hpp"
#include "engine/network/NetworkSecurity.hpp"
#include "engine/network/NetworkSimulation.hpp"
#include "engine/network/NetworkVariable.hpp"
#include "engine/network/Rpc.hpp"
#include "engine/network/NetworkSession.hpp"
#include "engine/platform/PlatformServices.hpp"
#include "engine/platform/PlatformAdapters.hpp"
#include "engine/platform/SettingsTransaction.hpp"
#include "engine/platform/UserStorage.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/PhysicsDebugDraw.hpp"
#include "engine/scene/PhysicsLayersAssetIO.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneUIDocuments.hpp"
#include "engine/script/ScriptAgentProjectFiles.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptValue.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/EditorPluginCatalog.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/material_preview/EditorMaterialGraphCookService.hpp"

#include <Windows.h>

#ifdef DrawText
#undef DrawText
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

namespace kb::editor {
namespace {

using kb::core::JsonValue;

constexpr std::string_view kSchema =
    "21kb.editor-automation/v1";
constexpr std::uintmax_t kMaxScenarioBytes = 64U * 1024U * 1024U;
constexpr double kPi = 3.14159265358979323846;

struct StepOutcome {
    bool succeeded = false;
    std::string detail;
};

struct EntityAlias {
    kb::scene::SceneEntity entity{};
    std::string name;
};

struct ScenarioState {
    explicit ScenarioState(
        EditorSceneContext& sceneContext,
        const std::filesystem::path& artifacts,
        std::filesystem::path fixtures)
        : context(sceneContext)
        , automation(sceneContext, artifacts / "automation")
        , fixtureRoot(std::move(fixtures)) {}

    EditorSceneContext& context;
    EditorHeadlessAutomation automation;
    std::filesystem::path fixtureRoot;
    std::unordered_map<std::string, EntityAlias> entities;
    std::unordered_map<std::string, kb::assets::AssetId> assets;
    std::unordered_map<std::string, std::uint32_t> materialNodes;
};

class ScopedProjectFile final {
public:
    explicit ScopedProjectFile(std::filesystem::path projectFile)
        : previous_(EditorProjectPaths::ProjectFile()) {
        EditorProjectPaths::SetProjectFile(std::move(projectFile));
    }

    ~ScopedProjectFile() {
        EditorProjectPaths::SetProjectFile(previous_);
    }

    ScopedProjectFile(const ScopedProjectFile&) = delete;
    ScopedProjectFile& operator=(const ScopedProjectFile&) = delete;

private:
    std::filesystem::path previous_;
};

[[nodiscard]] std::string ReadText(
    const std::filesystem::path& path, std::string& error) {
    std::error_code fileError;
    const std::uintmax_t size =
        std::filesystem::file_size(path, fileError);
    if (fileError || size > kMaxScenarioBytes) {
        error = fileError
            ? "scenario file is not readable"
            : "scenario exceeds 64 MiB";
        return {};
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "scenario file could not be opened";
        return {};
    }
    return std::string{
        std::istreambuf_iterator<char>{ input },
        std::istreambuf_iterator<char>{} };
}

[[nodiscard]] const JsonValue* Member(
    const JsonValue& object, std::string_view name,
    JsonValue::Kind kind, std::string& error,
    bool required = true) {
    const JsonValue* value = object.Find(name);
    if (value == nullptr) {
        if (required) {
            error = "missing '" + std::string{ name } + "'";
        }
        return nullptr;
    }
    if (value->GetKind() != kind) {
        error = "'" + std::string{ name } + "' has the wrong type";
        return nullptr;
    }
    return value;
}

[[nodiscard]] std::optional<std::string> StringMember(
    const JsonValue& object, std::string_view name,
    std::string& error, bool required = true) {
    const JsonValue* value = Member(
        object, name, JsonValue::Kind::String, error, required);
    if (value == nullptr) return std::nullopt;
    return value->AsString();
}

[[nodiscard]] std::optional<double> NumberMember(
    const JsonValue& object, std::string_view name,
    std::string& error, bool required = true) {
    const JsonValue* value = Member(
        object, name, JsonValue::Kind::Number, error, required);
    if (value == nullptr) return std::nullopt;
    const double number = value->AsNumber();
    if (!std::isfinite(number)) {
        error = "'" + std::string{ name } + "' must be finite";
        return std::nullopt;
    }
    return number;
}

[[nodiscard]] std::optional<bool> BoolMember(
    const JsonValue& object, std::string_view name,
    std::string& error, bool required = true) {
    const JsonValue* value = Member(
        object, name, JsonValue::Kind::Bool, error, required);
    if (value == nullptr) return std::nullopt;
    return value->AsBool();
}

[[nodiscard]] bool IsInside(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    const std::filesystem::path relative =
        candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& part : relative) {
        if (part == "..") return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::filesystem::path>
ResolveProjectPath(
    std::string_view relativeText, std::string& error) {
    const std::filesystem::path relative{
        std::string{ relativeText } };
    if (relative.empty() || relative.is_absolute()) {
        error = "project path must be non-empty and relative";
        return std::nullopt;
    }
    const std::filesystem::path root =
        std::filesystem::absolute(
            EditorProjectPaths::ProjectRoot()).lexically_normal();
    const std::filesystem::path candidate =
        std::filesystem::absolute(root / relative).lexically_normal();
    if (!IsInside(root, candidate)) {
        error = "project path escapes the isolated project";
        return std::nullopt;
    }
    return candidate;
}

void ReplaceAll(
    std::string& text, std::string_view token,
    std::string_view replacement) {
    std::size_t position = 0U;
    while ((position = text.find(token, position)) !=
           std::string::npos) {
        text.replace(position, token.size(), replacement);
        position += replacement.size();
    }
}

[[nodiscard]] std::optional<std::string> ExpandContentTokens(
    const ScenarioState& state, std::string content, std::string& error) {
    const std::string projectRoot =
        std::filesystem::absolute(EditorProjectPaths::ProjectRoot())
            .lexically_normal()
            .generic_string();
    const std::string artifactRoot =
        state.automation.ArtifactRoot().parent_path()
            .lexically_normal()
            .generic_string();
    ReplaceAll(content, "{{PROJECT_ROOT}}", projectRoot);
    ReplaceAll(content, "{{ARTIFACT_ROOT}}", artifactRoot);
    constexpr std::string_view prefix = "{{ASSET_ID:";
    std::size_t position = 0U;
    while ((position = content.find(prefix, position)) != std::string::npos) {
        const std::size_t end = content.find("}}", position + prefix.size());
        if (end == std::string::npos) {
            error = "unterminated ASSET_ID token";
            return std::nullopt;
        }
        const std::string path = content.substr(
            position + prefix.size(), end - position - prefix.size());
        const kb::assets::AssetMetadata* metadata =
            state.context.Scene().Assets().Manager().Registry().FindByPath(
                std::filesystem::path{ path });
        if (metadata == nullptr) {
            error = "ASSET_ID token references an undiscovered asset";
            return std::nullopt;
        }
        const std::string id = std::to_string(metadata->id.value);
        content.replace(position, end + 2U - position, id);
        position += id.size();
    }
    return content;
}

void WriteLittleEndian16(std::ostream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xffU));
    output.put(static_cast<char>((value >> 8U) & 0xffU));
}

void WriteLittleEndian32(std::ostream& output, std::uint32_t value) {
    output.put(static_cast<char>(value & 0xffU));
    output.put(static_cast<char>((value >> 8U) & 0xffU));
    output.put(static_cast<char>((value >> 16U) & 0xffU));
    output.put(static_cast<char>((value >> 24U) & 0xffU));
}

[[nodiscard]] bool WritePcmWave(
    const std::filesystem::path& path, std::uint32_t sampleRate,
    std::uint32_t durationMilliseconds, double frequency,
    double amplitude) {
    const std::uint32_t sampleCount = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(sampleRate) *
         durationMilliseconds) /
        1000U);
    const std::uint32_t dataBytes = sampleCount * 2U;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write("RIFF", 4);
    WriteLittleEndian32(output, 36U + dataBytes);
    output.write("WAVEfmt ", 8);
    WriteLittleEndian32(output, 16U);
    WriteLittleEndian16(output, 1U);
    WriteLittleEndian16(output, 1U);
    WriteLittleEndian32(output, sampleRate);
    WriteLittleEndian32(output, sampleRate * 2U);
    WriteLittleEndian16(output, 2U);
    WriteLittleEndian16(output, 16U);
    output.write("data", 4);
    WriteLittleEndian32(output, dataBytes);
    for (std::uint32_t sample = 0U; sample < sampleCount; ++sample) {
        const double phase =
            2.0 * kPi * frequency *
            (static_cast<double>(sample) /
             static_cast<double>(sampleRate));
        const auto value = static_cast<std::int16_t>(
            std::sin(phase) * amplitude * 32767.0);
        WriteLittleEndian16(
            output, static_cast<std::uint16_t>(value));
    }
    return output.good();
}

[[nodiscard]] kb::scene::SceneEntity ResolveEntity(
    const ScenarioState& state, std::string_view alias) {
    const auto found = state.entities.find(std::string{ alias });
    if (found == state.entities.end()) {
        return {};
    }
    if (state.context.Scene().Entities().IsAlive(found->second.entity)) {
        return found->second.entity;
    }

    kb::scene::SceneEntity resolved{};
    for (const EditorHierarchyRow& row : state.context.HierarchyRows()) {
        if (state.context.Scene().Entities().Name(row.entity) !=
            found->second.name) {
            continue;
        }
        if (resolved.IsValid()) {
            return {};
        }
        resolved = row.entity;
    }
    return resolved;
}

[[nodiscard]] bool HasAuthoredComponent(
    kb::scene::Scene& scene, kb::scene::SceneEntity entity,
    std::string_view component) {
    if (component == "AudioSource") {
        return scene.Components().AudioSources().Has(entity);
    }
    if (component == "AudioListener") {
        return scene.Components().AudioListeners().Has(entity);
    }
    if (component == "Animator") {
        return scene.Components().Animators().Has(entity);
    }
    if (component == "UIDocument") {
        return scene.Components().UIDocuments().Has(entity);
    }
    return kb::script::ScriptSceneComponentApi::HasComponent(
        scene, entity, component);
}

[[nodiscard]] kb::assets::AssetId ResolveAsset(
    const ScenarioState& state, std::string_view aliasOrPath) {
    if (const auto found =
            state.assets.find(std::string{ aliasOrPath });
        found != state.assets.end()) {
        return found->second;
    }
    const kb::assets::AssetMetadata* metadata =
        state.context.Scene().Assets().Manager().Registry()
            .FindByPath(std::filesystem::path{
                std::string{ aliasOrPath } });
    return metadata == nullptr
        ? kb::assets::AssetId{}
        : metadata->id;
}

[[nodiscard]] const kb::script::ScriptSceneComponentPropertyDesc*
FindProperty(
    std::string_view component, std::string_view property) {
    for (const auto& candidate :
         kb::script::ScriptSceneComponentApi::ComponentProperties(
             component)) {
        if (candidate.name == property) return &candidate;
    }
    return nullptr;
}

[[nodiscard]] std::optional<kb::script::ScriptValue>
ReadScriptValue(
    const JsonValue& value,
    kb::script::ScriptValueType type,
    const ScenarioState& state,
    std::string& error) {
    using kb::script::ScriptValue;
    using kb::script::ScriptValueType;
    switch (type) {
    case ScriptValueType::Bool:
        if (value.GetKind() == JsonValue::Kind::Bool) {
            return ScriptValue{ value.AsBool() };
        }
        break;
    case ScriptValueType::Int: {
        if (value.GetKind() != JsonValue::Kind::Number) break;
        const double number = value.AsNumber();
        if (std::isfinite(number) && std::floor(number) == number &&
            number >= std::numeric_limits<int>::min() &&
            number <= std::numeric_limits<int>::max()) {
            return ScriptValue{ static_cast<int>(number) };
        }
        break;
    }
    case ScriptValueType::Float:
        if (value.GetKind() == JsonValue::Kind::Number &&
            std::isfinite(value.AsNumber())) {
            return ScriptValue{
                static_cast<float>(value.AsNumber()) };
        }
        break;
    case ScriptValueType::String:
        if (value.GetKind() == JsonValue::Kind::String) {
            return ScriptValue{ value.AsString() };
        }
        break;
    case ScriptValueType::Entity:
        if (value.GetKind() == JsonValue::Kind::String) {
            const kb::scene::SceneEntity entity =
                ResolveEntity(state, value.AsString());
            if (entity.IsValid()) {
                return ScriptValue{
                    entity.Id(), ScriptValueType::Entity };
            }
        }
        break;
    default:
        error = "property type is not supported by scene components";
        return std::nullopt;
    }
    error = "value does not match component property type " +
        std::string{ kb::script::ToString(type) };
    return std::nullopt;
}

[[nodiscard]] bool ValuesEqual(
    const kb::script::ScriptValue& actual,
    const kb::script::ScriptValue& expected,
    double tolerance) {
    if (actual.Type() != expected.Type()) return false;
    switch (actual.Type()) {
    case kb::script::ScriptValueType::Float:
        return std::abs(
                   static_cast<double>(actual.AsFloat()) -
                   static_cast<double>(expected.AsFloat())) <=
            tolerance;
    case kb::script::ScriptValueType::Double:
        return std::abs(
                   actual.AsDouble() - expected.AsDouble()) <=
            tolerance;
    default:
        return actual == expected;
    }
}

[[nodiscard]] std::string ScriptValueText(
    const kb::script::ScriptValue& value) {
    switch (value.Type()) {
    case kb::script::ScriptValueType::Bool:
        return value.AsBool() ? "true" : "false";
    case kb::script::ScriptValueType::Int:
        return std::to_string(value.AsInt());
    case kb::script::ScriptValueType::Float:
        return std::to_string(value.AsFloat());
    case kb::script::ScriptValueType::String:
        return value.AsString();
    case kb::script::ScriptValueType::Entity:
        return std::to_string(value.AsUInt64());
    default:
        return std::string{
            kb::script::ToString(value.Type()) };
    }
}

[[nodiscard]] kb::input::InputActionValueType ParseActionType(
    std::string_view type, bool& valid) {
    valid = true;
    if (type == "Bool") return kb::input::InputActionValueType::Bool;
    if (type == "Axis1D") {
        return kb::input::InputActionValueType::Axis1D;
    }
    if (type == "Axis2D") {
        return kb::input::InputActionValueType::Axis2D;
    }
    if (type == "Axis3D") {
        return kb::input::InputActionValueType::Axis3D;
    }
    valid = false;
    return kb::input::InputActionValueType::Bool;
}

[[nodiscard]] std::optional<std::uintptr_t> ParseVirtualKey(
    std::string_view key) noexcept {
    if (key == "Enter") return VK_RETURN;
    if (key == "Escape") return VK_ESCAPE;
    if (key == "Backspace") return VK_BACK;
    if (key == "Delete") return VK_DELETE;
    if (key == "Tab") return VK_TAB;
    if (key == "Left") return VK_LEFT;
    if (key == "Right") return VK_RIGHT;
    if (key == "Up") return VK_UP;
    if (key == "Down") return VK_DOWN;
    return std::nullopt;
}

[[nodiscard]] StepOutcome ExecuteStep(
    ScenarioState& state, const JsonValue& step) {
    std::string error;
    if (step.GetKind() != JsonValue::Kind::Object) {
        return { false, "step must be an object" };
    }
    const auto operation = StringMember(step, "op", error);
    if (!operation.has_value()) return { false, error };

    if (*operation == "init_agent_project") {
        kb::script::ScriptRuntimeHost host{ state.context.Scene() };
        if (!host.Succeeded()) {
            return { false, "script runtime host could not be created" };
        }
        const kb::script::ScriptApiCatalog catalog =
            kb::script::ScriptApiCatalog::Build(host);
        const kb::script::ScriptAgentProjectFilesResult provisioned =
            kb::script::ScriptAgentProjectFiles::Write(
                state.context.ProjectFile().parent_path(), catalog);
        return {
            provisioned.succeeded,
            provisioned.succeeded
                ? "agent project provisioned"
                : provisioned.error };
    }

    if (*operation == "assert_first_release_network_model") {
        kb::script::ScriptRuntimeHost host{ state.context.Scene() };
        if (!host.Succeeded()) {
            return { false, "script runtime host could not be created" };
        }
        const kb::script::ScriptApiCatalog catalog =
            kb::script::ScriptApiCatalog::Build(host);
        const bool exposesNetworkApi = std::ranges::any_of(
            catalog.functions,
            [](const kb::script::ScriptApiCatalogFunction& function) {
                return function.name.starts_with("Network.");
            });
        const bool offline =
            kb::network::kFirstReleaseNetwork.model ==
                kb::network::NetworkModel::OfflineOnly &&
            !kb::network::kFirstReleaseNetwork.HasTransport() &&
            !kb::network::CanOpenNetworkSession(
                kb::network::kFirstReleaseNetwork) &&
            !exposesNetworkApi;
        return {
            offline,
            offline ? "offline-only; no transport or script API"
                    : "first-release network contract is open" };
    }

    if (*operation == "assert_network_object_lifecycle") {
        kb::network::NetworkObjects objects;
        const bool lifecycle =
            !objects.Spawn({}) &&
            objects.Spawn({ .id = 7U, .owner = 11U,
                .role = kb::network::NetworkRole::Authority }) &&
            !objects.Spawn({ .id = 7U, .owner = 12U,
                .role = kb::network::NetworkRole::Authority }) &&
            objects.CanAcceptOwnerCommand(7U, 11U) &&
            !objects.CanAcceptOwnerCommand(7U, 12U) &&
            objects.AssignOwner(7U, 12U) &&
            objects.Spawn({ .id = 8U, .owner = 12U,
                .role = kb::network::NetworkRole::Proxy }) &&
            !objects.AssignOwner(8U, 13U) && objects.Despawn(7U) &&
            !objects.Find(7U).has_value() &&
            !objects.CanAcceptOwnerCommand(7U, 12U) && !objects.Despawn(7U);
        return {
            lifecycle,
            lifecycle ? "authority, ownership, spawn and despawn"
                      : "network object lifecycle rejected" };
    }

    if (*operation == "assert_replication_schema") {
        const kb::network::ReplicationSchema schema{
            .version = 1U,
            .fields = {
                { .id = 1U, .name = "health",
                    .type = kb::network::ReplicatedFieldType::QuantizedFloat,
                    .minimum = 0.0F, .maximum = 100.0F,
                    .quantizationBits = 10U },
                { .id = 2U, .name = "alive",
                    .type = kb::network::ReplicatedFieldType::Boolean },
            } };
        const kb::network::ReplicationSchema incompatible{
            .version = 1U,
            .fields = {
                { .id = 1U, .name = "health",
                    .type = kb::network::ReplicatedFieldType::UnsignedInteger },
                { .id = 2U, .name = "alive",
                    .type = kb::network::ReplicatedFieldType::Boolean },
            } };
        const std::optional<std::uint32_t> quantized =
            kb::network::QuantizeFloat(schema.fields.front(), 50.0F);
        const std::optional<float> restored = quantized.has_value()
            ? kb::network::DequantizeFloat(schema.fields.front(), *quantized)
            : std::nullopt;
        const bool valid = kb::network::ValidateReplicationSchema(schema) &&
            quantized.has_value() && restored.has_value() &&
            std::abs(*restored - 50.0F) <= 0.1F &&
            kb::network::ComputeDeltaFields(schema, { 1U, 0U }, { 1U, 1U }) ==
                std::vector<std::uint16_t>{ 2U } &&
            kb::network::AreSchemasCompatible(schema, schema) &&
            !kb::network::AreSchemasCompatible(schema, incompatible);
        return {
            valid,
            valid ? "versioned schema, quantization and delta"
                  : "replication schema contract rejected" };
    }

    if (*operation == "assert_rpc_contract") {
        kb::network::NetworkObjects objects;
        const bool valid =
            objects.Spawn({ .id = 8U, .owner = 20U,
                .role = kb::network::NetworkRole::Authority }) &&
            kb::network::ValidateRpc(objects, { .object = 8U, .sender = 20U,
                .reliability = kb::network::RpcReliability::Reliable,
                .direction = kb::network::RpcDirection::ClientToServer }) &&
            kb::network::ValidateRpc(objects, { .object = 8U, .sender = 20U,
                .reliability = kb::network::RpcReliability::Unreliable,
                .direction = kb::network::RpcDirection::ClientToServer }) &&
            !kb::network::ValidateRpc(objects, { .object = 8U, .sender = 21U,
                .direction = kb::network::RpcDirection::ClientToServer }) &&
            objects.Spawn({ .id = 9U, .owner = 1U,
                .role = kb::network::NetworkRole::Proxy }) &&
            kb::network::ValidateRpc(objects, { .object = 9U, .sender = 1U,
                .reliability = kb::network::RpcReliability::Reliable,
                .direction = kb::network::RpcDirection::ServerToClient }) &&
            !kb::network::ValidateRpc(objects, { .object = 9U, .sender = 2U,
                .direction = kb::network::RpcDirection::ServerToClient });
        return {
            valid,
            valid ? "reliable/unreliable RPC ownership directions"
                  : "RPC ownership contract rejected" };
    }

    if (*operation == "assert_network_variable") {
        std::int32_t delta = 0;
        kb::network::NetworkVariable<std::int32_t> value{ 2 };
        value.SetChangedCallback(
            [](void* context, std::int32_t previous,
                std::int32_t current) noexcept {
                *static_cast<std::int32_t*>(context) = current - previous;
            },
            &delta);
        kb::network::NetworkVariable<std::int32_t> saturated{ 1 };
        const bool valid = value.Set(5) && value.Revision() == 1U &&
            delta == 3 && !value.Apply(7, 1U) && value.Apply(7, 2U) &&
            value.Value() == 7 && delta == 2 &&
            saturated.Apply(2, std::numeric_limits<std::uint64_t>::max()) &&
            !saturated.Set(3) && saturated.Value() == 2;
        return {
            valid,
            valid ? "typed revision, callback and stale update rejection"
                  : "network variable contract rejected" };
    }

    if (*operation == "assert_network_prediction") {
        const kb::network::NetworkSnapshot predicted{
            .tick = 5U, .acknowledgedInput = 3U,
            .position = { 0.0F, 0.0F, 0.0F } };
        const kb::network::NetworkSnapshot authoritative{
            .tick = 5U, .acknowledgedInput = 3U,
            .position = { 4.0F, 0.0F, 0.0F } };
        const kb::network::NetworkSnapshot interpolationNext{
            .tick = 6U, .acknowledgedInput = 3U,
            .position = { 4.0F, 0.0F, 0.0F } };
        const kb::network::NetworkSnapshot mismatchedAcknowledgement{
            .tick = 5U, .acknowledgedInput = 4U };
        const kb::network::NetworkSnapshot mismatchedVelocity{
            .tick = 5U, .acknowledgedInput = 3U,
            .velocity = { 2.0F, 0.0F, 0.0F } };
        const std::optional<kb::network::NetworkSnapshot> interpolated =
            kb::network::Interpolate(
                { .previous = predicted, .next = interpolationNext }, 0.5F);
        const bool valid =
            kb::network::IsValidInputCommand(
                { .tick = 5U, .sequence = 3U, .moveX = 1.0F }) &&
            kb::network::IsValidInputCommand(
                { .tick = 5U, .sequence = 0U, .moveX = 1.0F }) &&
            !kb::network::IsValidInputCommand(
                { .tick = 0U, .sequence = 3U, .moveX = 1.0F }) &&
            interpolated.has_value() && interpolated->position.x == 2.0F &&
            !kb::network::Interpolate(
                 { .previous = predicted, .next = predicted }, 0.5F)
                 .has_value() &&
            kb::network::RequiresReconciliation(
                predicted, authoritative, 1.0F) &&
            kb::network::RequiresReconciliation(
                predicted, mismatchedAcknowledgement, 1.0F) &&
            kb::network::RequiresReconciliation(
                predicted, mismatchedVelocity, 1.0F);
        return {
            valid,
            valid ? "input, snapshots, interpolation and reconciliation"
                  : "network prediction contract rejected" };
    }

    if (*operation == "assert_network_budget") {
        constexpr kb::network::NetworkBudget budget{};
        kb::network::NetworkTickClock clock{ budget };
        kb::network::NetworkTickClock invalidClock{ { .tickRate = 1U } };
        const bool valid = kb::network::IsValidNetworkBudget(budget) &&
            kb::network::AcceptsPacket(budget, 0U, budget.packetBytes) &&
            kb::network::AcceptsPacket(
                budget, budget.maxQueuedBytes - budget.packetBytes,
                budget.packetBytes) &&
            !kb::network::AcceptsPacket(
                budget, budget.maxQueuedBytes - budget.packetBytes + 1U,
                budget.packetBytes) &&
            clock.Advance(33333U) == 0U && clock.Advance(1U) == 1U &&
            clock.Tick() == 1U && clock.Advance(1'000'000U) == 30U &&
            clock.Tick() == 31U && clock.RemainderMicroticks() == 20U &&
            invalidClock.Advance(1'000'000U) == 0U;
        return {
            valid,
            valid ? "tick clock, packet budget and backpressure"
                  : "network budget contract rejected" };
    }

    if (*operation == "assert_network_security") {
        kb::network::NetworkObjects objects;
        constexpr kb::network::NetworkSecurityLimits limits{};
        const bool valid =
            objects.Spawn({ .id = 31U, .owner = 41U,
                .role = kb::network::NetworkRole::Authority }) &&
            kb::network::ValidateIncomingMessage(
                objects, limits, 31U, 41U, 10U, 0U, 10U) &&
            !kb::network::ValidateIncomingMessage(
                objects, limits, 0U, 41U, 10U, 0U, 10U) &&
            !kb::network::ValidateIncomingMessage(
                objects, limits, 31U, 0U, 10U, 0U, 10U) &&
            !kb::network::ValidateIncomingMessage(
                objects, limits, 31U, 42U, 10U, 0U, 10U) &&
            !kb::network::ValidateIncomingMessage(
                objects, limits, 31U, 41U, limits.maxPayloadBytes + 1U,
                0U, limits.maxPayloadBytes + 1U) &&
            !kb::network::ValidateIncomingMessage(
                objects, limits, 31U, 41U, 10U,
                limits.maxMessagesPerTick, 10U) &&
            !kb::network::ValidateIncomingMessage(
                objects, limits, 31U, 41U, 10U, 0U, 11U);
        return {
            valid,
            valid ? "server validation, rate and deserialization bounds"
                  : "network security contract rejected" };
    }

    if (*operation == "assert_network_simulation") {
        constexpr kb::network::NetworkSimulationConfig reordered{
            .latencyMilliseconds = 40U, .jitterMilliseconds = 5U,
            .reorderPermille = 1000U, .seed = 8U };
        constexpr kb::network::NetworkSimulationConfig lossAndDisconnect{
            .latencyMilliseconds = 40U, .jitterMilliseconds = 5U,
            .lossPermille = 1000U, .disconnectAtTick = 10U, .seed = 8U };
        const std::optional<kb::network::NetworkSimulationDecision> delayed =
            kb::network::SimulateNetworkPacket(reordered, 2U, 3U);
        const std::optional<kb::network::NetworkSimulationDecision> dropped =
            kb::network::SimulateNetworkPacket(lossAndDisconnect, 2U, 3U);
        const std::optional<kb::network::NetworkSimulationDecision> disconnected =
            kb::network::SimulateNetworkPacket(lossAndDisconnect, 10U, 3U);
        const bool valid = delayed.has_value() && dropped.has_value() &&
            disconnected.has_value() && delayed->reordered &&
            !delayed->dropped && delayed->deliveryDelayMilliseconds >= 35U &&
            delayed->deliveryDelayMilliseconds <= 45U && dropped->dropped &&
            dropped->deliveryDelayMilliseconds == 0U &&
            disconnected->disconnected && !disconnected->dropped &&
            kb::network::NetworkSimulationRandom(reordered, 2U, 3U) ==
                kb::network::NetworkSimulationRandom(reordered, 2U, 3U) &&
            !kb::network::SimulatedDeliveryDelayMilliseconds(
                 { .latencyMilliseconds = 5U, .jitterMilliseconds = 6U },
                 1U, 1U)
                 .has_value();
        return {
            valid,
            valid ? "latency, jitter, loss, reorder and disconnect"
                  : "network simulation contract rejected" };
    }

    if (*operation == "assert_offline_network_sessions") {
        const kb::network::ReplicationSchema schema{
            .version = 1U,
            .fields = { { .id = 1U, .name = "state",
                .type = kb::network::ReplicatedFieldType::Boolean } } };
        const kb::network::ReplicationSchema mismatch{
            .version = 2U,
            .fields = { { .id = 1U, .name = "state",
                .type = kb::network::ReplicatedFieldType::Boolean } } };
        kb::network::NetworkObjects objects;
        const bool valid =
            !kb::network::CanOpenNetworkSession(
                kb::network::kFirstReleaseNetwork) &&
            objects.Spawn({ .id = 71U, .owner = 81U,
                .role = kb::network::NetworkRole::Authority }) &&
            objects.Despawn(71U) &&
            !kb::network::ValidateRpc(objects, { .object = 71U,
                .sender = 81U,
                .direction = kb::network::RpcDirection::ClientToServer }) &&
            !kb::network::AreSchemasCompatible(schema, mismatch);
        return {
            valid,
            valid ? "offline host/client, despawned RPC and schema mismatch"
                  : "offline network-session contract rejected" };
    }

    if (*operation == "assert_platform_capabilities") {
        struct Probe {
            bool clipboard = false;
            bool url = false;
            bool vibration = false;
            [[nodiscard]] static bool Clipboard(void* context,
                std::string_view text) noexcept {
                auto& probe = *static_cast<Probe*>(context);
                probe.clipboard = text == "copied";
                return probe.clipboard;
            }
            [[nodiscard]] static bool Url(void* context,
                std::string_view value) noexcept {
                auto& probe = *static_cast<Probe*>(context);
                probe.url = value == "https://21kb.dev";
                return probe.url;
            }
            [[nodiscard]] static bool Vibration(void* context, float low,
                float high) noexcept {
                auto& probe = *static_cast<Probe*>(context);
                probe.vibration = low == 0.25F && high == 0.75F;
                return probe.vibration;
            }
        };
        constexpr kb::platform::PlatformCapabilities allCapabilities{
            .flags = static_cast<std::uint32_t>(
                         kb::platform::PlatformCapability::Locale) |
                static_cast<std::uint32_t>(
                    kb::platform::PlatformCapability::UserDataPath) |
                static_cast<std::uint32_t>(
                    kb::platform::PlatformCapability::Clipboard) |
                static_cast<std::uint32_t>(
                    kb::platform::PlatformCapability::OpenUrl) |
                static_cast<std::uint32_t>(
                    kb::platform::PlatformCapability::Vibration) };
        Probe probe;
        kb::platform::PlatformServices services{ allCapabilities,
            { .language = "pl", .region = "PL", .utcOffsetMinutes = 120 },
            "UserData", { .clipboard = &Probe::Clipboard, .openUrl = &Probe::Url,
                              .vibration = &Probe::Vibration,
                              .context = &probe } };
        kb::platform::PlatformServices restricted{ {},
            { .language = "pl", .region = "PL", .utcOffsetMinutes = 120 },
            "UserData" };
        const std::optional<kb::platform::PlatformLocale> locale =
            services.Locale();
        const bool valid = locale.has_value() && locale->language == "pl" &&
            locale->region == "PL" && services.UserDataPath().has_value() &&
            services.SetClipboardText("copied") &&
            services.OpenUrl("https://21kb.dev") &&
            !services.OpenUrl("file:///outside") &&
            services.SetVibration(0.25F, 0.75F) && probe.clipboard && probe.url &&
            probe.vibration && !restricted.Locale().has_value() &&
            !restricted.UserDataPath().has_value() &&
            !restricted.SetClipboardText("blocked") &&
            !restricted.OpenUrl("https://21kb.dev") &&
            !restricted.SetVibration(0.25F, 0.75F);
        return {
            valid,
            valid ? "capability-gated platform data and actions"
                  : "platform capability contract rejected" };
    }

    if (*operation == "assert_engine_log") {
        kb::core::EngineLog log{ 5U };
        const kb::core::LogRecord record{ .category = "AI", .message = "event",
            .entity = 1U, .world = 2U, .fields = {{ "state", "alert" }} };
        const bool valid = log.Trace(record, 1U, 1U) &&
            log.Debug(record, 2U, 1U) && log.Info(record, 3U, 1U) &&
            log.Warn(record, 4U, 1U) && log.Error(record, 5U, 1U) &&
            !log.Warn(record, 4U, 1U) && log.Records().size() == 5U &&
            log.Records().front().level == kb::core::LogLevel::Trace &&
            log.Records().back().level == kb::core::LogLevel::Error &&
            log.Records().front().entity == 1U && log.Records().front().world == 2U;
        return { valid, valid ? "five log levels with context and rate limiting"
                              : "engine log contract rejected" };
    }

    if (*operation == "assert_structured_log_fields") {
        kb::core::EngineLog log{ 1U };
        const kb::core::LogRecord record{ .category = "Combat",
            .message = "damage applied", .entity = 7U, .world = 9U,
            .fields = {{ "target", std::uint64_t{ 11U } },
                { "damage", 12.5 }, { "critical", true }} };
        const bool written = log.Info(record, 1U, 1U);
        const auto& fields = log.Records().front().fields;
        const bool valid = written && fields.size() == 3U &&
            fields[0U].key == "target" &&
            std::get<std::uint64_t>(fields[0U].value) == 11U &&
            std::get<double>(fields[1U].value) == 12.5 &&
            std::get<bool>(fields[2U].value);
        return { valid, valid ? "typed structured log fields"
                              : "structured log field contract rejected" };
    }

    if (*operation == "assert_assertion_policy") {
        const auto development = kb::core::Assert(false,
            kb::core::AssertionPolicy::Development, "broken", { "script.lua:12" });
        const auto release = kb::core::Assert(false,
            kb::core::AssertionPolicy::Release, "broken");
        const auto required = kb::core::Require(false,
            kb::core::AssertionPolicy::Release, "broken");
        const auto soft = kb::core::SoftFail(false,
            kb::core::AssertionPolicy::Development, "broken");
        const bool valid = development.fatal && !release.fatal && required.fatal &&
            !soft.fatal && development.scriptStackTrace ==
                std::vector<std::string>{ "script.lua:12" };
        return { valid, valid ? "assert, require, soft-fail and script stack trace"
                              : "assertion policy contract rejected" };
    }

    if (*operation == "assert_debug_draw") {
        kb::core::DebugDrawBuffer draw{ 5U };
        const bool added = draw.DrawLine({}, { 1.0F, 0.0F, 0.0F }, 1.0F, 3U) &&
            draw.DrawRay({}, { 0.0F, 1.0F, 0.0F }, 1.0F, 3U) &&
            draw.DrawBox({}, { 1.0F, 1.0F, 1.0F }, 1.0F, 3U) &&
            draw.DrawSphere({}, 1.0F, 1.0F, 3U) &&
            draw.DrawText({}, "probe", 1.0F, 3U);
        const bool valid = kb::core::DebugDrawBuffer::Enabled() && added &&
            draw.Commands().size() == 5U && draw.Commands().back().text == "probe" &&
            draw.Commands().front().channel == 3U;
        draw.Advance(1.0F);
        return { valid && draw.Commands().empty(), valid ? "debug draw primitives, duration and channel"
                                                           : "debug draw contract rejected" };
    }

    if (*operation == "assert_profiler") {
        kb::core::ProfilerCounters profiler;
        { auto scope = profiler.BeginScope("tick"); profiler.Counter("entities", 42U); profiler.Allocation(); }
        const bool valid = kb::core::ProfilerCounters::Enabled() && profiler.scopes == 1U &&
            profiler.timelineEvents == 1U && profiler.allocations == 1U &&
            profiler.Counters().size() == 1U && profiler.Counters().front().value == 42U &&
            profiler.TimelineEvents().size() == 1U;
        return { valid, valid ? "debug-only profiler scopes, counters, timeline and allocations"
                              : "profiler contract rejected" };
    }

    if (*operation == "assert_user_storage") {
        const std::filesystem::path root =
            EditorProjectPaths::ProjectRoot() / "UserStorageProbe";
        std::error_code storageError;
        std::filesystem::remove_all(root, storageError);
        kb::platform::UserStorage storage{ root, 8U };
        const bool valid = storage.Write("save/a", "data") &&
            storage.Read("save/a") == std::optional<std::string>{ "data" } &&
            storage.Write("save/a", "new") &&
            storage.Read("save/a") == std::optional<std::string>{ "new" } &&
            storage.WriteAsync("save/b", "ok").get() &&
            storage.List() == std::vector<std::string>{ "save/a", "save/b" } &&
            storage.Delete("save/a") && !storage.Write("../outside", "bad") &&
            !storage.Write("C:\\outside", "bad") &&
            !storage.Write("save/c", "too-large") &&
            storage.Read("save/b") == std::optional<std::string>{ "ok" };
        std::filesystem::remove_all(root, storageError);
        return {
            valid,
            valid ? "sandboxed atomic user storage with quota"
                  : "user storage contract rejected" };
    }

    if (*operation == "assert_user_storage_failures") {
        const std::filesystem::path root =
            EditorProjectPaths::ProjectRoot() / "UserStorageFailureProbe";
        std::error_code storageError;
        std::filesystem::remove_all(root, storageError);
        kb::platform::UserStorage storage{ root, 8U };
        const bool sandboxRejected = !storage.Write("../outside", "bad") &&
            !storage.Write("C:\\outside", "bad");
        const bool quotaRejected = !storage.Write("save/too-large", "too-large");
        const bool atomicFailurePreserved = storage.Write("save/atomic", "old") &&
            std::filesystem::create_directories(root / "save/atomic.tmp", storageError) &&
            !storage.Write("save/atomic", "new") &&
            storage.Read("save/atomic") == std::optional<std::string>{ "old" };
        kb::platform::PlatformServices unavailable{ {},
            { .language = "pl", .region = "PL", .utcOffsetMinutes = 120 },
            "UserData" };
        const bool capabilityRejected = !unavailable.Locale().has_value() &&
            !unavailable.UserDataPath().has_value() &&
            !unavailable.SetClipboardText("blocked");
        std::filesystem::remove_all(root, storageError);
        const bool valid = sandboxRejected && quotaRejected &&
            atomicFailurePreserved && capabilityRejected;
        return { valid, valid ? "sandbox, quota, atomic failure and unavailable capability"
                              : "platform failure contract rejected" };
    }

    if (*operation == "assert_platform_locale") {
        const kb::platform::PlatformLocale locale{
            .language = "pl", .region = "PL", .utcOffsetMinutes = 120 };
        const kb::platform::PlatformLocale invalidLocale{
            .language = "p1", .region = "P1", .utcOffsetMinutes = 900 };
        constexpr kb::platform::SafeDateTime date{ .unixSeconds = 1000 };
        constexpr kb::platform::SafeDateTime maximumDate{
            .unixSeconds = std::numeric_limits<std::int64_t>::max() };
        const bool valid = locale.IsValid() &&
            date.ToLocalSeconds(locale) == std::optional<std::int64_t>{ 8200 } &&
            !invalidLocale.IsValid() && !date.ToLocalSeconds(invalidLocale).has_value() &&
            !maximumDate.ToLocalSeconds(locale).has_value();
        return { valid, valid ? "bounded locale, UTC offset and safe date-time"
                              : "platform locale contract rejected" };
    }

    if (*operation == "assert_runtime_settings") {
        kb::platform::SettingsTransaction settings{
            kb::platform::RuntimeSettings{} };
        settings.Pending().vibration = true;
        const bool rejectedVibration = !settings.Apply({});
        settings.Revert();
        settings.Pending().masterVolume = 0.5F;
        settings.Pending().videoWidth = 1920U;
        settings.Pending().videoHeight = 1080U;
        settings.Pending().fullscreen = true;
        settings.Pending().mouseSensitivity = 2.0F;
        const bool applied = settings.Apply(
            { .flags = static_cast<std::uint32_t>(
                kb::platform::PlatformCapability::Vibration) },
            { .maximumVideoWidth = 1920U, .maximumVideoHeight = 1080U,
                .fullscreen = true });
        settings.Pending().videoWidth = 2560U;
        const bool rejectedVideo = !settings.Apply(
            {}, { .maximumVideoWidth = 1920U, .maximumVideoHeight = 1080U });
        settings.Revert();
        settings.Pending().mouseSensitivity = 0.0F;
        const bool rejectedInput = !settings.Apply({});
        settings.Revert();
        const bool valid = rejectedVibration && applied && rejectedVideo &&
            rejectedInput && settings.Current().masterVolume == 0.5F &&
            settings.Current().videoWidth == 1920U &&
            settings.Current().fullscreen &&
            settings.Pending().mouseSensitivity == 2.0F;
        return { valid, valid ? "audio, video and input settings transaction"
                              : "runtime settings contract rejected" };
    }

    if (*operation == "assert_optional_platform_adapter") {
        class Adapter final : public kb::platform::IPlatformAdapter {
        public:
            [[nodiscard]] bool IsAvailable(
                kb::platform::OptionalPlatformService service) const noexcept override {
                return service == kb::platform::OptionalPlatformService::Achievements;
            }
            [[nodiscard]] bool UnlockAchievement(std::string_view id) override {
                return IsAvailable(kb::platform::OptionalPlatformService::Achievements) && !id.empty();
            }
        } adapter;
        const bool valid =
            adapter.IsAvailable(kb::platform::OptionalPlatformService::Achievements) &&
            !adapter.IsAvailable(kb::platform::OptionalPlatformService::CloudSave) &&
            !adapter.IsAvailable(kb::platform::OptionalPlatformService::Dlc) &&
            !adapter.IsAvailable(kb::platform::OptionalPlatformService::User) &&
            adapter.UnlockAchievement("first") && !adapter.UnlockAchievement("");
        return { valid, valid ? "optional achievements, cloud, DLC and user adapter"
                              : "optional platform adapter contract rejected" };
    }

    if (*operation == "assert_game_flow") {
        kb::gameplay::GameInstance game{ state.context.Project() };
        const kb::gameplay::GameSceneId checkpointScene = game.CreateScene();
        const kb::gameplay::GameSceneId destinationScene = game.CreateScene();
        const bool succeeded =
            checkpointScene != 0U && destinationScene != 0U &&
            !game.SetCheckpoint(0U) && !game.TransitionToScene(999U) &&
            game.Flow().State() == kb::gameplay::GameFlowState::Playing &&
            !game.Flow().PendingTransition().has_value() &&
            game.SetCheckpoint(checkpointScene) && game.Pause() &&
            !game.TransitionToScene(destinationScene) && game.Resume() &&
            game.TransitionToScene(destinationScene) &&
            game.ActiveSceneId() == destinationScene && game.Win() &&
            !game.TransitionToScene(checkpointScene) &&
            game.RestartFromCheckpoint() &&
            game.ActiveSceneId() == checkpointScene && game.Lose() &&
            game.DestroyScene(checkpointScene) &&
            !game.Flow().Checkpoint().has_value() &&
            !game.RestartFromCheckpoint();
        return {
            succeeded,
            succeeded ? "checkpoint, pause, transition, outcome, restart" : "game flow lifecycle rejected" };
    }

    if (*operation == "write_file") {
        const auto path = StringMember(step, "path", error);
        const auto content = StringMember(step, "content", error);
        if (!path || !content) return { false, error };
        const auto expanded = ExpandContentTokens(state, *content, error);
        if (!expanded.has_value()) return { false, error };
        const auto resolved = ResolveProjectPath(*path, error);
        if (!resolved) return { false, error };
        std::error_code directoryError;
        std::filesystem::create_directories(
            resolved->parent_path(), directoryError);
        if (directoryError) {
            return { false, "could not create file directory" };
        }
        std::ofstream output(
            *resolved, std::ios::binary | std::ios::trunc);
        output.write(
            expanded->data(),
            static_cast<std::streamsize>(expanded->size()));
        if (!output.good()) {
            return { false, "file write failed" };
        }
        return { true, resolved->string() };
    }

    if (*operation == "write_pcm_wave") {
        const auto path = StringMember(step, "path", error);
        const double duration =
            NumberMember(step, "duration_ms", error, false)
                .value_or(250.0);
        const double sampleRate =
            NumberMember(step, "sample_rate", error, false)
                .value_or(16000.0);
        const double frequency =
            NumberMember(step, "frequency_hz", error, false)
                .value_or(440.0);
        const double amplitude =
            NumberMember(step, "amplitude", error, false)
                .value_or(0.15);
        if (!path || !error.empty()) return { false, error };
        if (duration < 1.0 || duration > 10000.0 ||
            std::floor(duration) != duration ||
            sampleRate < 8000.0 || sampleRate > 48000.0 ||
            std::floor(sampleRate) != sampleRate ||
            frequency < 20.0 || frequency >= sampleRate * 0.5 ||
            amplitude < 0.0 || amplitude > 1.0) {
            return { false, "invalid PCM wave parameters" };
        }
        const auto resolved = ResolveProjectPath(*path, error);
        if (!resolved) return { false, error };
        std::error_code directoryError;
        std::filesystem::create_directories(
            resolved->parent_path(), directoryError);
        if (directoryError) {
            return { false, "could not create wave directory" };
        }
        const bool written = WritePcmWave(
            *resolved, static_cast<std::uint32_t>(sampleRate),
            static_cast<std::uint32_t>(duration), frequency,
            amplitude);
        return {
            written,
            written ? resolved->string() : "wave write failed" };
    }

    if (*operation == "configure_physics_layers") {
        const auto path = StringMember(step, "path", error);
        const auto firstLayer = NumberMember(step, "first_layer", error);
        const auto firstName = StringMember(step, "first_name", error);
        const auto secondLayer = NumberMember(step, "second_layer", error);
        const auto secondName = StringMember(step, "second_name", error);
        const auto interact = BoolMember(step, "interact", error);
        if (!path || !firstLayer || !firstName || !secondLayer || !secondName || !interact) {
            return { false, error };
        }
        const auto validLayer = [](double value) {
            return value >= 0.0 && value < 32.0 && std::floor(value) == value;
        };
        if (!validLayer(*firstLayer) || !validLayer(*secondLayer) ||
            *firstLayer == *secondLayer || firstName->empty() || secondName->empty()) {
            return { false, "invalid physics layer configuration" };
        }
        const auto resolved = ResolveProjectPath(*path, error);
        if (!resolved) return { false, error };
        kb::scene::PhysicsLayersAsset asset;
        const std::uint32_t first = static_cast<std::uint32_t>(*firstLayer);
        const std::uint32_t second = static_cast<std::uint32_t>(*secondLayer);
        asset.layerNames[first] = *firstName;
        asset.layerNames[second] = *secondName;
        asset.SetLayersInteract(first, second, *interact);
        std::error_code directoryError;
        std::filesystem::create_directories(resolved->parent_path(), directoryError);
        const bool written = !directoryError && kb::scene::WritePhysicsLayersAsset(*resolved, asset);
        if (!written) return { false, "physics layers asset write failed" };
        const std::filesystem::path virtualPath =
            std::filesystem::path{ "/Game" } /
            resolved->lexically_relative(EditorProjectPaths::AssetsRoot());
        const bool configured = state.context.SetProjectPhysicsLayersAsset(
            kb::assets::NormalizeAssetPath(virtualPath));
        return { configured, configured ? virtualPath.generic_string() : "physics layers project setting failed" };
    }

    if (*operation == "select_project_settings_category") {
        const auto category = StringMember(step, "category", error);
        if (!category) return { false, error };
        int index = -1;
        if (*category == "inputs") {
            index = 0;
        } else if (*category == "graphics") {
            index = 1;
        } else if (*category == "physics") {
            index = 2;
        } else {
            return { false, "unknown project settings category" };
        }
        static_cast<void>(state.context.ProjectSettings().SelectCategory(index));
        return { true, *category };
    }

    if (*operation == "set_joint_connection" || *operation == "assert_joint_connection") {
        const auto owner = StringMember(step, "entity", error);
        const auto connected = StringMember(step, "connected_entity", error);
        if (!owner || !connected) return { false, error };
        const kb::scene::SceneEntity ownerEntity = ResolveEntity(state, *owner);
        const kb::scene::SceneEntity connectedEntity = ResolveEntity(state, *connected);
        if (!ownerEntity.IsValid() || !connectedEntity.IsValid()) {
            return { false, "joint entity alias was not found" };
        }
        kb::scene::JointComponent* joint = state.context.Scene().Components().Joints().TryGet(ownerEntity);
        if (joint == nullptr) {
            return { false, "owner does not have a Joint component" };
        }
        if (*operation == "assert_joint_connection") {
            const bool connectedToExpected = joint->connectedEntity == connectedEntity;
            return { connectedToExpected, connectedToExpected ? "connected" : "joint target differs" };
        }
        joint->connectedEntity = connectedEntity;
        state.context.Scene().Components().Joints().MarkModified(ownerEntity);
        return { true, *owner + " -> " + *connected };
    }

    if (*operation == "set_inspector_scroll") {
        const auto position = StringMember(step, "position", error);
        if (!position) return { false, error };
        if (*position == "top") {
            static_cast<void>(state.context.Inspector().SetScrollOffset(0, 0));
            return { true, *position };
        }
        if (*position == "bottom") {
            static_cast<void>(state.context.Inspector().SetScrollOffset(
                std::numeric_limits<int>::max(),
                std::numeric_limits<int>::max()));
            return { true, *position };
        }
        return { false, "unknown inspector scroll position" };
    }

    if (*operation == "set_physics_debug_draw") {
        const auto enabled = BoolMember(step, "enabled", error);
        if (!enabled) return { false, error };
        kb::scene::PhysicsDebugDraw::SetEnabled(state.context.Scene(), *enabled);
        const bool applied = kb::scene::PhysicsDebugDraw::IsEnabled(state.context.Scene()) == *enabled;
        return { applied, *enabled ? "enabled" : "disabled" };
    }

    if (*operation == "assert_physics_debug_line_count") {
        const auto expected = NumberMember(step, "count", error);
        if (!expected || !error.empty() || *expected < 0.0 ||
            std::floor(*expected) != *expected) {
            return { false, error.empty() ? "count must be a non-negative integer" : error };
        }
        const std::size_t actual =
            kb::scene::PhysicsDebugDraw::CollectLines(state.context.Scene()).size();
        const bool matched = actual == static_cast<std::size_t>(*expected);
        return { matched, "actual=" + std::to_string(actual) + " expected=" + std::to_string(static_cast<std::size_t>(*expected)) };
    }

    if (*operation == "copy_fixture") {
        const auto source = StringMember(step, "source", error);
        const auto destination =
            StringMember(step, "destination", error);
        if (!source || !destination) return { false, error };
        const std::filesystem::path relativeSource{ *source };
        if (relativeSource.empty() || relativeSource.is_absolute()) {
            return { false, "fixture source must be relative" };
        }
        const std::filesystem::path resolvedSource =
            std::filesystem::absolute(
                state.fixtureRoot / relativeSource).lexically_normal();
        if (!IsInside(state.fixtureRoot, resolvedSource) ||
            !std::filesystem::is_regular_file(resolvedSource)) {
            return { false, "fixture source escapes its root or is absent" };
        }
        const auto resolvedDestination =
            ResolveProjectPath(*destination, error);
        if (!resolvedDestination) return { false, error };
        std::error_code copyError;
        std::filesystem::create_directories(
            resolvedDestination->parent_path(), copyError);
        if (!copyError) {
            std::filesystem::copy_file(
                resolvedSource, *resolvedDestination,
                std::filesystem::copy_options::overwrite_existing,
                copyError);
        }
        return {
            !copyError,
            copyError ? copyError.message()
                      : resolvedDestination->string() };
    }

    if (*operation == "assert_file") {
        const auto path = StringMember(step, "path", error);
        if (!path) return { false, error };
        const auto resolved = ResolveProjectPath(*path, error);
        if (!resolved) return { false, error };
        const bool expected =
            BoolMember(step, "exists", error, false).value_or(true);
        const auto minimum =
            NumberMember(step, "min_size", error, false).value_or(0.0);
        const auto contains =
            StringMember(step, "contains", error, false);
        if (!error.empty() || minimum < 0.0 ||
            std::floor(minimum) != minimum) {
            return { false, error.empty()
                ? "min_size must be a non-negative integer" : error };
        }
        const bool exists =
            std::filesystem::is_regular_file(*resolved);
        if (exists != expected) {
            return {
                false, exists ? "unexpected file" : "file is absent" };
        }
        if (!exists) return { true, "absent" };
        std::error_code sizeError;
        const std::uintmax_t size =
            std::filesystem::file_size(*resolved, sizeError);
        if (sizeError ||
            size < static_cast<std::uintmax_t>(minimum)) {
            return { false, "file is smaller than expected" };
        }
        if (contains.has_value()) {
            std::string readError;
            const std::string text = ReadText(*resolved, readError);
            if (!readError.empty() ||
                text.find(*contains) == std::string::npos) {
                return { false, readError.empty()
                    ? "file text was not found" : readError };
            }
        }
        return { true, std::to_string(size) + " byte(s)" };
    }

    if (*operation == "discover_assets") {
        const std::size_t count =
            state.context.Scene().Assets().Discover();
        return { true, std::to_string(count) + " asset(s)" };
    }

    if (*operation == "unload_asset") {
        const auto asset = StringMember(step, "asset", error);
        if (!asset) return { false, error };
        const kb::assets::AssetId id = ResolveAsset(state, *asset);
        if (!id.IsValid()) return { false, "asset was not found" };
        kb::assets::AssetManager& manager =
            state.context.Scene().Assets().Manager();
        if (!manager.LoadOpaque(id)) {
            return { false, manager.LastError() };
        }
        const bool unloaded = manager.Unload(id);
        return { unloaded, *asset };
    }

    if (*operation == "import_asset") {
        const auto source = StringMember(step, "source", error);
        const auto destination =
            StringMember(step, "destination", error);
        if (!source || !destination) return { false, error };
        const auto sourcePath = ResolveProjectPath(*source, error);
        if (!sourcePath) return { false, error };
        const std::array<std::filesystem::path, 1U> sources{
            *sourcePath };
        return {
            state.context.ImportAssetFiles(
                sources, std::filesystem::path{ *destination }),
            sourcePath->string() };
    }

    if (*operation == "create_entity") {
        const auto alias = StringMember(step, "id", error);
        const auto name = StringMember(step, "name", error, false);
        if (!alias) return { false, error };
        if (state.entities.contains(*alias)) {
            return { false, "entity alias already exists" };
        }
        const kb::scene::SceneEntity entity =
            state.context.CreateHierarchyObject();
        if (!entity.IsValid()) {
            return { false, "entity creation failed" };
        }
        if (name.has_value()) {
            state.context.Scene().Entities().SetName(entity, *name);
        }
        state.entities.emplace(
            *alias,
            EntityAlias{
                .entity = entity,
                .name = state.context.Scene().Entities().Name(entity) });
        return { true, *alias + '=' + std::to_string(entity.Id()) };
    }

    if (*operation == "create_mesh_entity") {
        const auto alias = StringMember(step, "id", error);
        const auto asset = StringMember(step, "asset", error);
        if (!alias || !asset) return { false, error };
        if (state.entities.contains(*alias)) {
            return { false, "entity alias already exists" };
        }
        const kb::assets::AssetId assetId =
            ResolveAsset(state, *asset);
        if (!assetId.IsValid()) {
            return { false, "mesh asset was not found" };
        }
        const kb::scene::SceneEntity entity =
            state.context.CreateMeshAssetEntity(assetId);
        if (!entity.IsValid()) {
            return { false, "mesh entity creation failed" };
        }
        state.entities.emplace(
            *alias,
            EntityAlias{
                .entity = entity,
                .name = state.context.Scene().Entities().Name(entity) });
        return { true, *alias + '=' + std::to_string(entity.Id()) };
    }

    if (*operation == "duplicate_entity") {
        const auto sourceAlias =
            StringMember(step, "entity", error);
        const auto resultAlias = StringMember(step, "id", error);
        const auto name = StringMember(step, "name", error, false);
        if (!sourceAlias || !resultAlias) return { false, error };
        if (state.entities.contains(*resultAlias)) {
            return { false, "entity alias already exists" };
        }
        const kb::scene::SceneEntity source =
            ResolveEntity(state, *sourceAlias);
        if (!state.context.Scene().Entities().IsAlive(source)) {
            return { false, "source entity alias is not alive" };
        }
        state.context.SelectEntity(source);
        if (!state.context.DuplicateSelectedHierarchyEntities()) {
            return { false, "entity duplication failed" };
        }
        const kb::scene::SceneEntity entity =
            state.context.SelectedEntity();
        if (!state.context.Scene().Entities().IsAlive(entity) ||
            entity == source) {
            return { false, "duplicated entity was not selected" };
        }
        if (name.has_value()) {
            state.context.Scene().Entities().SetName(entity, *name);
        }
        state.entities.emplace(
            *resultAlias,
            EntityAlias{
                .entity = entity,
                .name = state.context.Scene().Entities().Name(entity) });
        return { true, *resultAlias };
    }

    if (*operation == "delete_entity") {
        const auto alias = StringMember(step, "entity", error);
        if (!alias) return { false, error };
        const kb::scene::SceneEntity entity =
            ResolveEntity(state, *alias);
        if (!state.context.Scene().Entities().IsAlive(entity)) {
            return { false, "entity alias is not alive" };
        }
        state.context.SelectEntity(entity);
        return {
            state.context.DeleteSelectedHierarchyEntity(),
            *alias };
    }

    if (*operation == "rename_entity") {
        const auto alias = StringMember(step, "entity", error);
        const auto name = StringMember(step, "name", error);
        if (!alias || !name || name->empty()) return { false, error };
        const kb::scene::SceneEntity entity =
            ResolveEntity(state, *alias);
        if (!state.context.Scene().Entities().IsAlive(entity)) {
            return { false, "entity alias is not alive" };
        }
        state.context.SelectEntity(entity);
        if (!state.context.BeginHierarchyRename()) {
            return { false, "hierarchy rename did not begin" };
        }
        state.context.SetHierarchyRenameText(*name);
        if (!state.context.CommitHierarchyRename()) {
            state.context.CancelHierarchyRename();
            return { false, "hierarchy rename failed" };
        }
        state.entities.at(*alias).entity = entity;
        state.entities.at(*alias).name = *name;
        return { true, *name };
    }

    if (*operation == "reparent_entity") {
        const auto childAlias =
            StringMember(step, "entity", error);
        const auto parentAlias =
            StringMember(step, "parent", error);
        if (!childAlias || !parentAlias) return { false, error };
        const kb::scene::SceneEntity child =
            ResolveEntity(state, *childAlias);
        const kb::scene::SceneEntity parent =
            ResolveEntity(state, *parentAlias);
        return {
            state.context.ReparentEntity(child, parent),
            *childAlias + " -> " + *parentAlias };
    }

    if (*operation == "create_prefab") {
        const auto alias = StringMember(step, "entity", error);
        const auto path = StringMember(step, "path", error);
        if (!alias || !path) return { false, error };
        const auto resolved = ResolveProjectPath(*path, error);
        if (!resolved) return { false, error };
        return {
            state.context.CreatePrefabAsset(
                ResolveEntity(state, *alias), *resolved),
            resolved->string() };
    }

    if (*operation == "instantiate_prefab") {
        const auto resultAlias = StringMember(step, "id", error);
        const auto path = StringMember(step, "path", error);
        const auto virtualPath =
            StringMember(step, "virtual_path", error);
        const auto name = StringMember(step, "name", error, false);
        if (!resultAlias || !path || !virtualPath) {
            return { false, error };
        }
        if (state.entities.contains(*resultAlias)) {
            return { false, "entity alias already exists" };
        }
        const auto resolved = ResolveProjectPath(*path, error);
        if (!resolved) return { false, error };
        const auto x = NumberMember(step, "x", error, false);
        const auto y = NumberMember(step, "y", error, false);
        const auto z = NumberMember(step, "z", error, false);
        if (!error.empty() ||
            (x.has_value() != y.has_value()) ||
            (x.has_value() != z.has_value())) {
            return { false, error.empty()
                ? "x, y and z must be supplied together" : error };
        }
        bool instantiated = false;
        if (x.has_value()) {
            instantiated = state.context.InstantiatePrefabAssetAt(
                *resolved, std::filesystem::path{ *virtualPath },
                kb::scene::Vec3{
                    static_cast<float>(*x),
                    static_cast<float>(*y),
                    static_cast<float>(*z) });
        } else {
            const auto parentAlias =
                StringMember(step, "parent", error, false);
            const kb::scene::SceneEntity parent =
                parentAlias.has_value()
                ? ResolveEntity(state, *parentAlias)
                : kb::scene::SceneEntity{};
            instantiated = state.context.InstantiatePrefabAsset(
                *resolved, std::filesystem::path{ *virtualPath },
                parent);
        }
        const kb::scene::SceneEntity entity =
            state.context.SelectedEntity();
        if (!instantiated ||
            !state.context.Scene().Entities().IsAlive(entity)) {
            return { false, "prefab instantiation failed" };
        }
        if (name.has_value()) {
            state.context.Scene().Entities().SetName(entity, *name);
        }
        state.entities.emplace(
            *resultAlias,
            EntityAlias{
                .entity = entity,
                .name = state.context.Scene().Entities().Name(entity) });
        return { true, *resultAlias };
    }

    if (*operation == "select_entity") {
        const auto alias = StringMember(step, "entity", error);
        if (!alias) return { false, error };
        const kb::scene::SceneEntity entity =
            ResolveEntity(state, *alias);
        if (!state.context.Scene().Entities().IsAlive(entity)) {
            return { false, "entity alias is not alive" };
        }
        state.context.SelectEntity(entity);
        return { true, *alias };
    }

    if (*operation == "assert_name") {
        const auto alias = StringMember(step, "entity", error);
        const auto expected = StringMember(step, "value", error);
        if (!alias || !expected) return { false, error };
        const kb::scene::SceneEntity entity = ResolveEntity(state, *alias);
        if (!state.context.Scene().Entities().IsAlive(entity)) {
            return { false, "entity alias is not alive" };
        }
        const std::string actual = state.context.Scene().Entities().Name(entity);
        return { actual == *expected, "actual=" + actual + " expected=" + *expected };
    }

    if (*operation == "add_component") {
        const auto alias = StringMember(step, "entity", error);
        const auto component =
            StringMember(step, "component", error);
        if (!alias || !component) return { false, error };
        const kb::scene::SceneEntity entity =
            ResolveEntity(state, *alias);
        if (!state.context.Scene().Entities().IsAlive(entity)) {
            return { false, "entity alias is not alive" };
        }
        state.context.SelectEntity(entity);
        return {
            state.automation.AddComponent(*component),
            *component + " on " + *alias };
    }

    if (*operation == "set_animator_root_motion_owner" ||
        *operation == "assert_animator_root_motion_owner") {
        const auto alias = StringMember(step, "entity", error);
        const auto owner = StringMember(step, "owner", error);
        if (!alias || !owner) return { false, error };
        const kb::scene::SceneEntity entity = ResolveEntity(state, *alias);
        const auto expected = [&]() -> std::optional<kb::scene::AnimatorRootMotionOwner> {
            if (*owner == "none") return kb::scene::AnimatorRootMotionOwner::None;
            if (*owner == "animator") return kb::scene::AnimatorRootMotionOwner::Animator;
            if (*owner == "character_controller") return kb::scene::AnimatorRootMotionOwner::CharacterController;
            if (*owner == "rigidbody") return kb::scene::AnimatorRootMotionOwner::Rigidbody;
            return std::nullopt;
        }();
        if (!expected) return { false, "unknown animator root-motion owner" };
        kb::scene::Animator* animator = state.context.Scene().Components().Animators().TryGet(entity);
        if (animator == nullptr) return { false, "entity has no Animator component" };
        if (*operation == "set_animator_root_motion_owner") {
            for (std::size_t attempts = 0U; animator->rootMotionOwner != *expected && attempts < 4U; ++attempts) {
                if (!state.context.CycleAnimatorRootMotionOwner(entity)) {
                    return { false, "editor rejected incompatible root-motion owner" };
                }
                animator = state.context.Scene().Components().Animators().TryGet(entity);
                if (animator == nullptr) return { false, "Animator component disappeared" };
            }
        }
        const bool matched = animator->rootMotionOwner == *expected;
        return { matched, matched ? *owner : "root-motion owner mismatch" };
    }

    if (*operation == "set_property" ||
        *operation == "assert_property") {
        const auto alias = StringMember(step, "entity", error);
        const auto component =
            StringMember(step, "component", error);
        const auto property =
            StringMember(step, "property", error);
        const JsonValue* jsonValue = step.Find("value");
        if (!alias || !component || !property ||
            jsonValue == nullptr) {
            return { false, error.empty() ? "missing 'value'" : error };
        }
        const kb::scene::SceneEntity entity =
            ResolveEntity(state, *alias);
        if (!state.context.Scene().Entities().IsAlive(entity)) {
            return { false, "entity alias is not alive" };
        }
        const auto* descriptor =
            FindProperty(*component, *property);
        if (descriptor == nullptr) {
            return { false, "component property is not registered" };
        }
        const auto value = ReadScriptValue(
            *jsonValue, descriptor->type, state, error);
        if (!value.has_value()) return { false, error };
        if (*operation == "set_property") {
            const auto mutation =
                kb::script::ScriptSceneComponentApi::SetProperty(
                    state.context.Scene(), entity, *component,
                    *property, *value);
            if (mutation.succeeded) {
                state.context.MarkSceneDocumentDirty();
                state.context.MarkSceneRenderDirty();
            }
            return {
                mutation.succeeded,
                mutation.succeeded ? ScriptValueText(*value)
                                   : mutation.error };
        }
        const auto actual =
            kb::script::ScriptSceneComponentApi::GetProperty(
                state.context.Scene(), entity, *component,
                *property);
        const double tolerance =
            NumberMember(step, "tolerance", error, false)
                .value_or(0.0001);
        const bool matched =
            actual.succeeded &&
            ValuesEqual(actual.value, *value, tolerance);
        return {
            matched,
            actual.succeeded
                ? "actual=" + ScriptValueText(actual.value) +
                    " expected=" + ScriptValueText(*value)
                : actual.error };
    }

    if (*operation == "assert_entity" ||
        *operation == "assert_component") {
        const auto alias = StringMember(step, "entity", error);
        if (!alias) return { false, error };
        const kb::scene::SceneEntity entity =
            ResolveEntity(state, *alias);
        bool found =
            state.context.Scene().Entities().IsAlive(entity);
        if (*operation == "assert_component") {
            const auto component =
                StringMember(step, "component", error);
            if (!component) return { false, error };
            found =
                found &&
                HasAuthoredComponent(
                    state.context.Scene(), entity, *component);
        }
        const bool expected =
            BoolMember(step, "exists", error, false).value_or(true);
        return {
            found == expected,
            std::string{ found ? "present" : "absent" } };
    }

    if (*operation == "assert_ui_element") {
        const auto alias = StringMember(step, "entity", error);
        const auto elementValue = NumberMember(step, "element", error);
        if (!alias || !elementValue) return { false, error };
        if (*elementValue < 1.0 || *elementValue > static_cast<double>(std::numeric_limits<kb::scene::UIElementId>::max()) ||
            std::floor(*elementValue) != *elementValue) {
            return { false, "'element' must be a positive integral UI element id" };
        }
        const kb::scene::SceneEntity entity = ResolveEntity(state, *alias);
        const kb::scene::UIElementId element = static_cast<kb::scene::UIElementId>(*elementValue);
        const bool present = state.context.Scene().UIDocuments().HasElement(entity, element);
        const bool expectedPresent = BoolMember(step, "exists", error, false).value_or(true);
        if (present != expectedPresent) {
            return { false, std::string{ present ? "present" : "absent" } };
        }
        const auto expectedVisible = BoolMember(step, "visible", error, false);
        const auto expectedKind = StringMember(step, "kind", error, false);
        const auto expectedText = StringMember(step, "text", error, false);
        if (!present && (expectedVisible.has_value() || expectedKind.has_value() || expectedText.has_value())) {
            return { false, "cannot assert a property of an absent UI element" };
        }
        if (expectedKind.has_value()) {
            const auto control = state.context.Scene().UIDocuments().Control(entity, element);
            const auto matchesKind = [kind = *expectedKind, &control]() {
                if (!control.has_value()) return false;
                if (kind == "Container") return control->kind == kb::scene::UIControlKind::Container;
                if (kind == "Text") return control->kind == kb::scene::UIControlKind::Text;
                if (kind == "Image") return control->kind == kb::scene::UIControlKind::Image;
                if (kind == "Button") return control->kind == kb::scene::UIControlKind::Button;
                if (kind == "Toggle") return control->kind == kb::scene::UIControlKind::Toggle;
                if (kind == "Slider") return control->kind == kb::scene::UIControlKind::Slider;
                if (kind == "List") return control->kind == kb::scene::UIControlKind::List;
                if (kind == "InputField") return control->kind == kb::scene::UIControlKind::InputField;
                if (kind == "ScrollView") return control->kind == kb::scene::UIControlKind::ScrollView;
                if (kind == "ModalDialog") return control->kind == kb::scene::UIControlKind::ModalDialog;
                return false;
            }();
            if (!matchesKind) return { false, "control kind mismatch" };
        }
        if (expectedText.has_value()) {
            const auto control = state.context.Scene().UIDocuments().Control(entity, element);
            if (!control.has_value() || control->text != *expectedText) {
                return { false, "control text mismatch" };
            }
        }
        if (!expectedVisible.has_value()) return { true, present ? "present" : "absent" };
        const bool visible = state.context.Scene().UIDocuments().Visible(entity, element);
        return { visible == *expectedVisible, visible ? "visible" : "hidden" };
    }

    if (*operation == "assert_parent") {
        const auto childAlias =
            StringMember(step, "entity", error);
        const auto parentAlias =
            StringMember(step, "parent", error);
        if (!childAlias || !parentAlias) return { false, error };
        const kb::scene::SceneEntity child =
            ResolveEntity(state, *childAlias);
        const kb::scene::SceneEntity parent =
            ResolveEntity(state, *parentAlias);
        const bool matched =
            state.context.Scene().Entities().IsAlive(child) &&
            state.context.Scene().Entities().IsAlive(parent) &&
            state.context.Scene().Hierarchy().Parent(child) == parent;
        return { matched, matched ? "matched" : "parent mismatch" };
    }

    if (*operation == "assert_asset") {
        const auto path = StringMember(step, "path", error);
        const auto type = StringMember(step, "type", error, false);
        if (!path) return { false, error };
        const kb::assets::AssetMetadata* metadata =
            state.context.Scene().Assets().Manager().Registry()
                .FindByPath(std::filesystem::path{ *path });
        const bool expected =
            BoolMember(step, "exists", error, false).value_or(true);
        if (!error.empty()) return { false, error };
        const bool matched =
            expected
            ? metadata != nullptr &&
                (!type.has_value() || metadata->type == *type)
            : metadata == nullptr;
        return {
            matched,
            metadata == nullptr ? "absent" : metadata->type };
    }

    if (*operation == "create_asset") {
        const auto alias = StringMember(step, "id", error);
        const auto type = StringMember(step, "type", error);
        const auto folder =
            StringMember(step, "folder", error, false);
        if (!alias || !type) return { false, error };
        if (state.assets.contains(*alias)) {
            return { false, "asset alias already exists" };
        }
        const std::filesystem::path virtualFolder =
            folder.value_or("/Game");
        const auto beforeSpan =
            state.context.Scene().Assets().Manager().Registry().All();
        const std::vector<kb::assets::AssetMetadata> before{
            beforeSpan.begin(), beforeSpan.end() };
        bool created = false;
        if (*type == "lua_script") {
            created =
                state.context.CreateLuaScriptAsset(virtualFolder);
        } else if (*type == "input_action") {
            created =
                state.context.CreateInputActionAsset(virtualFolder);
        } else if (*type == "input_axis") {
            created =
                state.context.CreateInputAxisAsset(virtualFolder);
        } else if (*type == "input_context") {
            created = state.context.CreateInputMappingContextAsset(
                virtualFolder);
        } else if (*type == "material") {
            created =
                state.context.CreateMaterialAsset(virtualFolder);
        } else if (*type == "material_function") {
            created = state.context.CreateMaterialFunctionAsset(
                virtualFolder);
        } else if (*type == "material_graph") {
            created = state.context.CreateMaterialGraphAsset(
                virtualFolder);
        } else if (*type == "material_type") {
            created =
                state.context.CreateMaterialTypeAsset(virtualFolder);
        } else {
            return { false, "unsupported asset type" };
        }
        if (!created) return { false, "asset creation failed" };
        const auto all =
            state.context.Scene().Assets().Manager().Registry().All();
        kb::assets::AssetId createdId{};
        for (const auto& metadata : all) {
            const bool existed = std::ranges::any_of(
                before, [&metadata](const auto& candidate) {
                    return candidate.id == metadata.id;
                });
            if (!existed) {
                createdId = metadata.id;
                break;
            }
        }
        if (!createdId.IsValid()) {
            return { false, "created asset was not registered" };
        }
        state.assets.emplace(*alias, createdId);
        return { true, *alias + '=' + std::to_string(createdId.value) };
    }

    if (*operation == "copy_asset" ||
        *operation == "move_asset") {
        const auto asset = StringMember(step, "asset", error);
        const auto destination =
            StringMember(step, "destination", error);
        if (!asset || !destination) return { false, error };
        const kb::assets::AssetId id = ResolveAsset(state, *asset);
        if (!id.IsValid()) return { false, "asset was not found" };
        const bool succeeded =
            *operation == "copy_asset"
            ? state.context.CopyAssetToFolder(
                id, std::filesystem::path{ *destination })
            : state.context.MoveAssetToFolder(
                id, std::filesystem::path{ *destination });
        return { succeeded, *destination };
    }

    if (*operation == "delete_asset") {
        const auto asset = StringMember(step, "asset", error);
        if (!asset) return { false, error };
        const kb::assets::AssetId id = ResolveAsset(state, *asset);
        return {
            id.IsValid() && state.context.DeleteAssetBrowserItem(id),
            *asset };
    }

    if (*operation == "assign_asset") {
        const auto entityAlias =
            StringMember(step, "entity", error);
        const auto asset = StringMember(step, "asset", error);
        const auto role = StringMember(step, "role", error);
        if (!entityAlias || !asset || !role) {
            return { false, error };
        }
        const kb::scene::SceneEntity entity =
            ResolveEntity(state, *entityAlias);
        const kb::assets::AssetId id = ResolveAsset(state, *asset);
        if (!state.context.Scene().Entities().IsAlive(entity) ||
            !id.IsValid()) {
            return { false, "entity or asset was not found" };
        }
        bool assigned = false;
        if (*role == "mesh") {
            assigned =
                state.context.SetMeshRendererMeshAsset(entity, id);
        } else if (*role == "material") {
            assigned =
                state.context.SetMeshRendererMaterialAsset(entity, id);
        } else if (*role == "audio_clip") {
            assigned =
                state.context.SetAudioSourceClipAsset(entity, id);
        } else if (*role == "animator_controller") {
            assigned =
                state.context.SetAnimatorControllerAsset(entity, id);
        } else if (*role == "ui_document") {
            assigned = state.context.SetUIDocumentAsset(entity, id);
        } else if (*role == "script") {
            assigned =
                state.context.AttachScriptToEntity(entity, id);
        } else {
            return { false, "unknown asset role" };
        }
        return { assigned, *role };
    }

    if (*operation == "assign_material_slot" ||
        *operation == "assert_material_slot") {
        const auto entityAlias = StringMember(step, "entity", error);
        const auto asset = StringMember(step, "asset", error);
        const auto slotValue = NumberMember(step, "slot", error);
        if (!entityAlias || !asset || !slotValue) {
            return { false, error };
        }
        if (*slotValue < 0.0 || *slotValue > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
            std::floor(*slotValue) != *slotValue) {
            return { false, "slot must be a uint32" };
        }
        const kb::scene::SceneEntity entity = ResolveEntity(state, *entityAlias);
        const kb::assets::AssetId id = ResolveAsset(state, *asset);
        const std::uint32_t slot = static_cast<std::uint32_t>(*slotValue);
        if (!state.context.Scene().Entities().IsAlive(entity) || !id.IsValid()) {
            return { false, "entity or material asset was not found" };
        }
        if (*operation == "assign_material_slot") {
            return {
                EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(
                    state.context.Scene(), entity, slot, id),
                std::to_string(slot) };
        }
        const kb::scene::MeshRendererComponent* renderer =
            state.context.Scene().Components().MeshRenderers().TryGet(entity);
        const bool assigned = renderer != nullptr &&
            slot < renderer->materialSlotOverrideCount &&
            renderer->materialSlotAssetIds[slot] == id.value;
        return { assigned, std::to_string(slot) };
    }

    if (*operation == "set_material" ||
        *operation == "assert_material") {
        const auto asset = StringMember(step, "asset", error);
        const auto property =
            StringMember(step, "property", error);
        const JsonValue* value = step.Find("value");
        if (!asset || !property || value == nullptr) {
            return { false, error.empty() ? "missing 'value'" : error };
        }
        const kb::assets::AssetId id = ResolveAsset(state, *asset);
        if (!id.IsValid()) return { false, "material was not found" };
        const auto current =
            state.context.ReadMaterialDocumentAsset(id);
        if (!current.has_value()) {
            return { false, "material document is unavailable" };
        }
        bool succeeded = false;
        std::string actual;
        if (*property == "double_sided") {
            if (value->GetKind() != JsonValue::Kind::Bool) {
                return { false, "double_sided requires a bool" };
            }
            const bool expected = value->AsBool();
            if (*operation == "set_material" &&
                current->desc.doubleSided != expected) {
                succeeded = state.context.ToggleMaterialDoubleSided(id);
            } else {
                succeeded = current->desc.doubleSided == expected;
            }
            const auto updated =
                state.context.ReadMaterialDocumentAsset(id);
            succeeded = succeeded && updated.has_value() &&
                updated->desc.doubleSided == expected;
            actual = expected ? "true" : "false";
        } else if (*property == "alpha_mode") {
            if (value->GetKind() != JsonValue::Kind::String) {
                return { false, "alpha_mode requires a string" };
            }
            kb::render::RenderMaterialAlphaMode expected{};
            if (value->AsString() == "opaque") {
                expected = kb::render::RenderMaterialAlphaMode::Opaque;
            } else if (value->AsString() == "mask") {
                expected = kb::render::RenderMaterialAlphaMode::Mask;
            } else if (value->AsString() == "blend") {
                expected = kb::render::RenderMaterialAlphaMode::Blend;
            } else {
                return { false, "invalid alpha mode" };
            }
            succeeded =
                *operation == "set_material"
                ? state.context.SetMaterialAlphaMode(id, expected)
                : current->desc.alphaMode == expected;
            const auto updated =
                state.context.ReadMaterialDocumentAsset(id);
            succeeded = succeeded && updated.has_value() &&
                updated->desc.alphaMode == expected;
            actual = value->AsString();
        } else {
            if (value->GetKind() != JsonValue::Kind::Number ||
                !std::isfinite(value->AsNumber())) {
                return { false, "material property requires a number" };
            }
            const float expected =
                static_cast<float>(value->AsNumber());
            const auto getValue = [&property](
                const kb::render::RenderMaterialAssetData& material) {
                if (*property == "metallic") {
                    return material.desc.metallicFactor;
                }
                if (*property == "roughness") {
                    return material.desc.roughnessFactor;
                }
                if (*property == "normal_scale") {
                    return material.desc.normalScale;
                }
                if (*property == "occlusion_strength") {
                    return material.desc.occlusionStrength;
                }
                if (*property == "emissive_strength") {
                    return material.desc.emissiveStrength;
                }
                if (*property == "alpha_cutoff") {
                    return material.desc.alphaCutoff;
                }
                return std::numeric_limits<float>::quiet_NaN();
            };
            if (!std::isfinite(getValue(*current))) {
                return { false, "unknown material property" };
            }
            if (*operation == "set_material") {
                if (*property == "metallic") {
                    succeeded =
                        state.context.SetMaterialMetallicFactor(id, expected);
                } else if (*property == "roughness") {
                    succeeded =
                        state.context.SetMaterialRoughnessFactor(id, expected);
                } else if (*property == "normal_scale") {
                    succeeded =
                        state.context.SetMaterialNormalScale(id, expected);
                } else if (*property == "occlusion_strength") {
                    succeeded =
                        state.context.SetMaterialOcclusionStrength(id, expected);
                } else if (*property == "emissive_strength") {
                    succeeded =
                        state.context.SetMaterialEmissiveStrength(id, expected);
                } else if (*property == "alpha_cutoff") {
                    succeeded =
                        state.context.SetMaterialAlphaCutoff(id, expected);
                }
            } else {
                succeeded =
                    std::abs(getValue(*current) - expected) <= 0.0001F;
            }
            const auto updated =
                state.context.ReadMaterialDocumentAsset(id);
            succeeded = succeeded && updated.has_value() &&
                std::abs(getValue(*updated) - expected) <= 0.0001F;
            actual = std::to_string(
                updated.has_value() ? getValue(*updated) : 0.0F);
        }
        return { succeeded, actual };
    }

    if (*operation == "save_material") {
        const auto asset = StringMember(step, "asset", error);
        if (!asset) return { false, error };
        const kb::assets::AssetId id = ResolveAsset(state, *asset);
        return {
            id.IsValid() &&
                state.context.SaveMaterialEditorAsset(id),
            *asset };
    }

    if (*operation == "find_material_node" ||
        *operation == "add_material_node") {
        const auto asset = StringMember(step, "asset", error);
        const auto alias = StringMember(step, "id", error);
        const auto kindText = StringMember(step, "kind", error);
        if (!asset || !alias || !kindText) return { false, error };
        if (state.materialNodes.contains(*alias)) {
            return { false, "material node alias already exists" };
        }
        const kb::assets::AssetId id = ResolveAsset(state, *asset);
        const auto kind =
            kb::render::ParseRenderMaterialGraphNodeKind(*kindText);
        if (!id.IsValid() || !kind.has_value()) {
            return { false, "asset or node kind is invalid" };
        }
        auto document = state.context.ReadMaterialDocumentAsset(id);
        if (!document.has_value()) {
            return { false, "material graph document is unavailable" };
        }
        std::uint32_t nodeId = 0U;
        if (*operation == "find_material_node") {
            for (const auto& node : document->graph.nodes) {
                if (node.kind != *kind) continue;
                if (nodeId != 0U) {
                    return { false, "material node kind is ambiguous" };
                }
                nodeId = node.id;
            }
        } else {
            const auto x = NumberMember(step, "x", error);
            const auto y = NumberMember(step, "y", error);
            if (!x || !y ||
                *x < std::numeric_limits<int>::min() ||
                *x > std::numeric_limits<int>::max() ||
                *y < std::numeric_limits<int>::min() ||
                *y > std::numeric_limits<int>::max() ||
                std::floor(*x) != *x || std::floor(*y) != *y) {
                return { false, error.empty()
                    ? "node coordinates must be integers" : error };
            }
            std::vector<std::uint32_t> before;
            before.reserve(document->graph.nodes.size());
            for (const auto& node : document->graph.nodes) {
                before.push_back(node.id);
            }
            if (!state.context.AddMaterialGraphNode(
                    id, *kind, static_cast<int>(*x),
                    static_cast<int>(*y))) {
                return { false, "material node creation failed" };
            }
            document = state.context.ReadMaterialDocumentAsset(id);
            if (document.has_value()) {
                for (const auto& node : document->graph.nodes) {
                    if (node.kind == *kind &&
                        std::ranges::find(before, node.id) ==
                            before.end()) {
                        nodeId = node.id;
                        break;
                    }
                }
            }
        }
        if (nodeId == 0U) {
            return { false, "material node was not resolved" };
        }
        state.materialNodes.emplace(*alias, nodeId);
        return { true, *alias + '=' + std::to_string(nodeId) };
    }

    if (*operation == "connect_material_nodes") {
        const auto asset = StringMember(step, "asset", error);
        const auto from = StringMember(step, "from", error);
        const auto fromPin = StringMember(step, "from_pin", error);
        const auto to = StringMember(step, "to", error);
        const auto toPin = StringMember(step, "to_pin", error);
        if (!asset || !from || !fromPin || !to || !toPin) {
            return { false, error };
        }
        const kb::assets::AssetId id = ResolveAsset(state, *asset);
        const auto fromNode = state.materialNodes.find(*from);
        const auto toNode = state.materialNodes.find(*to);
        if (!id.IsValid() ||
            fromNode == state.materialNodes.end() ||
            toNode == state.materialNodes.end()) {
            return { false, "material node alias was not found" };
        }
        const bool connected =
            state.context.BeginMaterialGraphPinConnection(
                id, fromNode->second, *fromPin) &&
            state.context.CompleteMaterialGraphPinConnection(
                id, toNode->second, *toPin);
        const auto document =
            state.context.ReadMaterialDocumentAsset(id);
        const bool persisted =
            connected && document.has_value() &&
            std::ranges::any_of(
                document->graph.links,
                [&fromNode, &toNode, &fromPin, &toPin](const auto& link) {
                    return link.fromNodeId == fromNode->second &&
                        link.fromPin == *fromPin &&
                        link.toNodeId == toNode->second &&
                        link.toPin == *toPin;
                });
        return { persisted, persisted ? "connected" : "connection failed" };
    }

    if (*operation == "wait_material_cook") {
        const auto timeout =
            NumberMember(step, "timeout_ms", error, false)
                .value_or(30000.0);
        if (!error.empty() || timeout < 1.0 ||
            timeout > 120000.0 || std::floor(timeout) != timeout) {
            return { false, error.empty()
                ? "timeout_ms must be an integer from 1 to 120000"
                : error };
        }
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds{
                static_cast<std::int64_t>(timeout) };
        EditorMaterialGraphCookResult result =
            state.context.OpenMaterialGraphCookResult();
        while (std::chrono::steady_clock::now() < deadline) {
            static_cast<void>(
                state.context.PumpMaterialGraphCookResults());
            result = state.context.OpenMaterialGraphCookResult();
            if (result.status ==
                    EditorMaterialGraphCookStatus::Ready ||
                result.status ==
                    EditorMaterialGraphCookStatus::UpToDate) {
                return {
                    true,
                    std::string{
                        EditorMaterialGraphCookStatusName(result.status) } +
                        " passes=" +
                        std::to_string(result.passes.size()) };
            }
            if (result.status ==
                    EditorMaterialGraphCookStatus::Failed ||
                result.status ==
                    EditorMaterialGraphCookStatus::CookUnavailable ||
                result.status ==
                    EditorMaterialGraphCookStatus::Stale) {
                return {
                    false,
                    result.diagnostics.empty()
                    ? std::string{
                        EditorMaterialGraphCookStatusName(result.status) }
                    : result.diagnostics.front() };
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds{ 10 });
        }
        return {
            false,
            "material cook timed out in state " +
                std::string{
                    EditorMaterialGraphCookStatusName(result.status) } };
    }

    if (*operation == "configure_input_action") {
        const auto alias = StringMember(step, "asset", error);
        const auto name = StringMember(step, "name", error);
        const auto type = StringMember(step, "value_type", error);
        if (!alias || !name || !type) return { false, error };
        const kb::assets::AssetId id =
            ResolveAsset(state, *alias);
        bool validType = false;
        const auto valueType = ParseActionType(*type, validType);
        const bool configured =
            id.IsValid() && validType &&
            state.context.SetInputActionName(id, *name) &&
            state.context.SetInputActionValueType(id, valueType);
        return { configured, configured ? *name : "configuration failed" };
    }

    if (*operation == "configure_input_mapping") {
        const auto contextAlias =
            StringMember(step, "context", error);
        const auto keyText = StringMember(step, "key", error);
        if (!contextAlias || !keyText) return { false, error };
        const auto indexNumber =
            NumberMember(step, "index", error, false).value_or(0.0);
        const auto scale =
            NumberMember(step, "scale", error, false).value_or(1.0);
        if (indexNumber < 0.0 ||
            std::floor(indexNumber) != indexNumber) {
            return { false, "mapping index must be non-negative" };
        }
        const kb::input::InputKey key =
            kb::input::ParseInputKey(*keyText);
        const kb::assets::AssetId contextId =
            ResolveAsset(state, *contextAlias);
        const std::size_t index =
            static_cast<std::size_t>(indexNumber);
        bool configured =
            contextId.IsValid() &&
            key != kb::input::InputKey::None;
        const auto current =
            configured
            ? state.context.ReadInputMappingContextAsset(contextId)
            : std::nullopt;
        while (configured && current.has_value() &&
               index >= current->mappings.size()) {
            configured =
                state.context.AddInputMapping(contextId);
            if (!configured) break;
            const auto expanded =
                state.context.ReadInputMappingContextAsset(contextId);
            if (!expanded.has_value()) {
                configured = false;
                break;
            }
            if (expanded->mappings.size() > index) {
                break;
            }
        }
        configured =
            configured &&
            state.context.SetInputMappingKey(
                contextId, index, key) &&
            state.context.SetInputMappingScale(
                contextId, index, static_cast<float>(scale)) &&
            state.context.CycleInputMappingAction(contextId, index);
        return { configured, configured ? *keyText : "mapping failed" };
    }

    if (*operation == "activate_input_context") {
        const auto alias = StringMember(step, "asset", error);
        if (!alias) return { false, error };
        const kb::assets::AssetId id =
            ResolveAsset(state, *alias);
        const kb::assets::AssetMetadata* metadata =
            state.context.Scene().Assets().Manager().Registry().Find(id);
        const bool activated =
            metadata != nullptr &&
            state.context.SetProjectInputMappingContext(
                metadata->virtualPath.generic_string());
        return { activated, activated ? *alias : "activation failed" };
    }

    if (*operation == "set_project_input_enabled") {
        const auto enabled = BoolMember(step, "enabled", error);
        if (!enabled) return { false, error };
        if (state.context.Project().inputEnabled != *enabled &&
            !state.context.ToggleProjectInputEnabled()) {
            return { false, "project input toggle failed" };
        }
        return {
            state.context.Project().inputEnabled == *enabled,
            *enabled ? "enabled" : "disabled" };
    }

    if (*operation == "set_plugin" ||
        *operation == "assert_plugin") {
        const auto id = StringMember(step, "id", error);
        const auto enabled = BoolMember(step, "enabled", error);
        if (!id || !enabled) return { false, error };
        std::optional<std::size_t> index;
        for (std::size_t candidate = 0U;
             candidate < EditorPluginCatalog::Count(); ++candidate) {
            const EditorPluginDescriptor* descriptor =
                EditorPluginCatalog::At(candidate);
            if (descriptor != nullptr && descriptor->id == *id) {
                index = candidate;
                break;
            }
        }
        if (!index.has_value()) {
            return { false, "plugin is not in the editor catalog" };
        }
        if (*operation == "set_plugin" &&
            state.context.IsProjectPluginEnabled(*id) != *enabled &&
            !state.context.ToggleProjectPlugin(*index)) {
            return { false, "plugin toggle failed" };
        }
        const bool matched =
            state.context.IsProjectPluginEnabled(*id) == *enabled;
        return {
            matched,
            matched ? (*enabled ? "enabled" : "disabled")
                    : "plugin state mismatch" };
    }

    if (*operation == "assert_backend") {
        const auto backend = StringMember(step, "backend", error);
        const auto expected = BoolMember(step, "available", error);
        if (!backend || !expected) return { false, error };
        bool available = false;
        if (*backend == "physics") {
            available = kb::scene::PhysicsBackend::HasBackend(
                state.context.Scene());
        } else if (*backend == "audio") {
            available = kb::audio::AudioPlayback::HasBackend(
                state.context.Scene());
        } else if (*backend == "haptics") {
            available = kb::input::InputHaptics::HasBackend(
                state.context.Scene());
        } else if (*backend == "basic_lighting") {
            available =
                kb::scene::SceneLightingAccess::BasicLightingEnabled(
                    state.context.Scene());
        } else {
            return { false, "unknown backend" };
        }
        return {
            available == *expected,
            available ? "available" : "unavailable" };
    }

    if (*operation == "attach_script") {
        const auto entityAlias =
            StringMember(step, "entity", error);
        const auto asset = StringMember(step, "asset", error);
        if (!entityAlias || !asset) return { false, error };
        const kb::scene::SceneEntity entity =
            ResolveEntity(state, *entityAlias);
        const kb::assets::AssetId assetId =
            ResolveAsset(state, *asset);
        const bool attached =
            entity.IsValid() && assetId.IsValid() &&
            state.context.AddBehaviourAssetToEntity(
                assetId, entity);
        return { attached, attached ? *asset : "attach failed" };
    }

    if (*operation == "open_asset") {
        const auto asset = StringMember(step, "asset", error);
        if (!asset) return { false, error };
        const kb::assets::AssetId id = ResolveAsset(state, *asset);
        const kb::assets::AssetMetadata* metadata =
            state.context.Scene().Assets().Manager().Registry().Find(id);
        if (metadata == nullptr) {
            return { false, "asset was not found" };
        }
        bool opened = false;
        if (metadata->type == "LuaScript") {
            opened = state.context.OpenLuaScript(id);
        } else if (
            metadata->type == "AnimationClip" ||
            metadata->type == "AnimatorController" ||
            metadata->type == "Timeline") {
            opened = state.context.OpenAnimationAsset(id);
        } else if (
            metadata->type == "RenderMaterial" ||
            metadata->type == "RenderMaterialInstance" ||
            metadata->type == "RenderMaterialGraph") {
            opened = state.context.OpenMaterialEditorAsset(id);
        }
        return { opened, opened ? metadata->type : "unsupported asset" };
    }

    if (*operation == "new_scene") {
        return {
            state.context.NewScene(EditorDirtySceneResolution::Discard),
            "new scene" };
    }

    if (*operation == "save_scene") {
        const auto path = StringMember(step, "path", error, false);
        if (!path.has_value()) {
            return {
                state.context.SaveCurrentScene(),
                state.context.CurrentScenePath().string() };
        }
        const auto resolved = ResolveProjectPath(*path, error);
        if (!resolved) return { false, error };
        return {
            state.context.SaveCurrentSceneAs(*resolved),
            resolved->string() };
    }

    if (*operation == "open_scene") {
        const auto path = StringMember(step, "path", error);
        if (!path) return { false, error };
        const auto resolved = ResolveProjectPath(*path, error);
        if (!resolved) return { false, error };
        return {
            state.context.OpenScene(
                *resolved, EditorDirtySceneResolution::Discard),
            resolved->string() };
    }

    if (*operation == "reload_scene") {
        return {
            state.context.ReloadSceneFromProject(),
            "project scene reloaded" };
    }

    if (*operation == "undo") {
        return { state.context.UndoSceneCommand(), "undo" };
    }
    if (*operation == "redo") {
        return { state.context.RedoSceneCommand(), "redo" };
    }

    if (*operation == "play") {
        return {
            state.context.BeginPlayModeSceneSession(),
            "play mode started" };
    }
    if (*operation == "stop") {
        return {
            state.context.RestorePlayModeSceneSession(),
            "play mode stopped" };
    }

    if (*operation == "key") {
        const auto keyText = StringMember(step, "key", error);
        const auto down = BoolMember(step, "down", error);
        if (!keyText || !down) return { false, error };
        const auto gamepad =
            NumberMember(step, "gamepad", error, false).value_or(0.0);
        if (gamepad < 0.0 ||
            gamepad >=
                kb::input::InputDeviceState::kMaxGamepads ||
            std::floor(gamepad) != gamepad) {
            return { false, "invalid gamepad index" };
        }
        const kb::input::InputKey key =
            kb::input::ParseInputKey(*keyText);
        return {
            state.automation.SetGameplayKey(
                key, *down,
                static_cast<std::uint8_t>(gamepad)),
            *keyText };
    }

    if (*operation == "analog") {
        const auto keyText = StringMember(step, "key", error);
        const auto value = NumberMember(step, "value", error);
        if (!keyText || !value) return { false, error };
        const auto gamepad =
            NumberMember(step, "gamepad", error, false).value_or(0.0);
        if (gamepad < 0.0 ||
            gamepad >=
                kb::input::InputDeviceState::kMaxGamepads ||
            std::floor(gamepad) != gamepad) {
            return { false, "invalid gamepad index" };
        }
        return {
            state.automation.SetGameplayAnalog(
                kb::input::ParseInputKey(*keyText),
                static_cast<float>(*value),
                static_cast<std::uint8_t>(gamepad)),
            *keyText };
    }

    if (*operation == "pointer") {
        const auto x = NumberMember(step, "x", error);
        const auto y = NumberMember(step, "y", error);
        if (!x || !y) return { false, error };
        return {
            state.automation.SetGameplayPointer(
                static_cast<float>(*x), static_cast<float>(*y)),
            std::to_string(*x) + ',' + std::to_string(*y) };
    }

    if (*operation == "touch") {
        const JsonValue* points = Member(
            step, "points", JsonValue::Kind::Array, error);
        if (points == nullptr) return { false, error };
        if (points->Size() >
            kb::input::InputDeviceState::kMaxTouchPoints) {
            return { false, "too many touch points" };
        }
        std::vector<kb::input::InputTouchPoint> parsed;
        parsed.reserve(points->Size());
        for (std::size_t index = 0; index < points->Size(); ++index) {
            const JsonValue* point = points->At(index);
            if (point == nullptr ||
                point->GetKind() != JsonValue::Kind::Object) {
                return { false, "touch point must be an object" };
            }
            const auto id = NumberMember(*point, "id", error);
            const auto x = NumberMember(*point, "x", error);
            const auto y = NumberMember(*point, "y", error);
            const auto phase = StringMember(*point, "phase", error);
            if (!id || !x || !y || !phase || *id < 0.0 ||
                *id > std::numeric_limits<std::uint32_t>::max() ||
                std::floor(*id) != *id) {
                return { false, error.empty()
                    ? "invalid touch point" : error };
            }
            kb::input::InputTouchPhase parsedPhase{};
            if (*phase == "began") {
                parsedPhase = kb::input::InputTouchPhase::Began;
            } else if (*phase == "moved") {
                parsedPhase = kb::input::InputTouchPhase::Moved;
            } else if (*phase == "ended") {
                parsedPhase = kb::input::InputTouchPhase::Ended;
            } else {
                return { false, "invalid touch phase" };
            }
            parsed.push_back(kb::input::InputTouchPoint{
                .id = static_cast<std::uint32_t>(*id),
                .x = static_cast<float>(*x),
                .y = static_cast<float>(*y),
                .phase = parsedPhase });
        }
        return {
            state.automation.SetGameplayTouches(parsed),
            std::to_string(parsed.size()) + " point(s)" };
    }

    if (*operation == "focus") {
        const auto focused = BoolMember(step, "focused", error);
        if (!focused) return { false, error };
        return {
            state.automation.SetGameplayFocus(*focused),
            *focused ? "focused" : "unfocused" };
    }

    if (*operation == "gamepad_connected") {
        const auto index = NumberMember(step, "index", error);
        const auto connected =
            BoolMember(step, "connected", error);
        if (!index || !connected || *index < 0.0 ||
            *index >= kb::input::InputDeviceState::kMaxGamepads ||
            std::floor(*index) != *index) {
            return { false, error.empty() ? "invalid gamepad index" : error };
        }
        return {
            state.automation.SetGamepadConnected(
                static_cast<std::uint8_t>(*index), *connected),
            std::to_string(*index) };
    }

    if (*operation == "step") {
        const auto frames = NumberMember(step, "frames", error);
        const auto delta =
            NumberMember(step, "dt", error, false)
                .value_or(1.0 / 60.0);
        if (!frames || *frames < 1.0 ||
            std::floor(*frames) != *frames) {
            return { false, "frames must be a positive integer" };
        }
        return {
            state.automation.StepRuntime(
                static_cast<std::size_t>(*frames),
                static_cast<float>(delta)),
            std::to_string(*frames) + " frame(s)" };
    }

    if (*operation == "inspector_pointer") {
        const auto action = StringMember(step, "action", error);
        if (!action) return { false, error };
        if (*action == "up") {
            return {
                state.automation.InspectorPointerUp(), "up" };
        }
        const auto x = NumberMember(step, "x", error);
        const auto y = NumberMember(step, "y", error);
        if (!x || !y) return { false, error };
        if (*action == "down") {
            return {
                state.automation.InspectorPointerDown(
                    static_cast<int>(*x), static_cast<int>(*y)),
                "down" };
        }
        if (*action == "drag") {
            return {
                state.automation.InspectorPointerDrag(
                    static_cast<int>(*x), static_cast<int>(*y)),
                "drag" };
        }
        return { false, "unknown inspector pointer action" };
    }

    if (*operation == "inspector_text") {
        const auto text = StringMember(step, "text", error);
        if (!text) return { false, error };
        bool routed = true;
        for (const unsigned char character : *text) {
            if (character > 0x7FU ||
                !state.automation.InspectorChar(
                    static_cast<wchar_t>(character))) {
                routed = false;
                break;
            }
        }
        return { routed, *text };
    }

    if (*operation == "inspector_key") {
        const auto key = StringMember(step, "key", error);
        if (!key) return { false, error };
        const auto virtualKey = ParseVirtualKey(*key);
        return {
            virtualKey.has_value() &&
                state.automation.InspectorKey(*virtualKey),
            *key };
    }

    if (*operation == "capture") {
        const auto panel = StringMember(step, "panel", error);
        const auto checkpoint =
            StringMember(step, "checkpoint", error);
        if (!panel || !checkpoint) return { false, error };
        return {
            state.automation.CapturePanel(*panel, *checkpoint),
            *panel + ':' + *checkpoint };
    }

    if (*operation == "capture_runtime") {
        const auto checkpoint =
            StringMember(step, "checkpoint", error);
        if (!checkpoint) return { false, error };
        const bool requireNonUniform =
            BoolMember(
                step, "require_non_uniform", error, false)
                .value_or(false);
        if (!error.empty()) return { false, error };
        return {
            state.automation.CaptureRuntime(
                *checkpoint, requireNonUniform),
            *checkpoint };
    }

    if (*operation == "snapshot") {
        const auto kind = StringMember(step, "kind", error);
        const auto checkpoint =
            StringMember(step, "checkpoint", error);
        if (!kind || !checkpoint) return { false, error };
        if (*kind == "console") {
            state.automation.SnapshotConsole(*checkpoint);
            return { true, *checkpoint };
        }
        if (*kind == "inspector_tree") {
            return {
                state.automation.SnapshotInspectorTree(*checkpoint),
                *checkpoint };
        }
        return { false, "unknown snapshot kind" };
    }

    if (*operation == "assert_console") {
        const auto contains =
            StringMember(step, "contains", error);
        if (!contains) return { false, error };
        const auto category =
            StringMember(step, "category", error, false);
        const auto level =
            StringMember(step, "level", error, false);
        const auto minimum =
            NumberMember(step, "count_at_least", error, false)
                .value_or(1.0);
        if (!error.empty() || minimum < 1.0 ||
            std::floor(minimum) != minimum) {
            return { false, error.empty()
                ? "count_at_least must be a positive integer" : error };
        }
        std::optional<EditorConsoleLevel> expectedLevel;
        if (level.has_value()) {
            if (*level == "info") {
                expectedLevel = EditorConsoleLevel::Info;
            } else if (*level == "warning") {
                expectedLevel = EditorConsoleLevel::Warning;
            } else if (*level == "error") {
                expectedLevel = EditorConsoleLevel::Error;
            } else {
                return { false, "invalid console level" };
            }
        }
        const std::size_t count = std::ranges::count_if(
            state.context.Console().Entries(),
            [&contains, &category, &expectedLevel](
                const EditorConsoleEntry& entry) {
                return entry.message.find(*contains) !=
                        std::string::npos &&
                    (!category.has_value() ||
                        entry.category == *category) &&
                    (!expectedLevel.has_value() ||
                        entry.level == *expectedLevel);
            });
        const bool found =
            count >= static_cast<std::size_t>(minimum);
        return {
            found,
            found ? std::to_string(count) + " match(es)"
                  : "matching console entry not found" };
    }

    if (*operation == "clear_console") {
        state.context.Console().Clear();
        return { true, "console cleared" };
    }

    if (*operation == "assert_no_errors") {
        const auto errors =
            state.context.Scene().Runtime().DrainSceneSystemErrors();
        const auto consoleError = std::ranges::find_if(
            state.context.Console().Entries(),
            [](const EditorConsoleEntry& entry) {
                return entry.level == EditorConsoleLevel::Error;
            });
        if (consoleError != state.context.Console().Entries().end()) {
            return {
                false,
                consoleError->category + ": " + consoleError->message };
        }
        return {
            errors.empty(),
            errors.empty() ? "no runtime or console errors"
                           : errors.front() };
    }

    return { false, "unknown operation '" + *operation + "'" };
}

void WriteFailureArtifact(
    const std::filesystem::path& artifactRoot,
    std::string_view message) {
    std::error_code error;
    std::filesystem::create_directories(artifactRoot, error);
    std::ofstream report(
        artifactRoot / "report.txt",
        std::ios::binary | std::ios::trunc);
    report << "21kb external editor automation\n"
           << "[FAIL] " << message << "\nRESULT: FAIL\n";
}

} // namespace

int EditorAutomationScenarioRunner::Run(
    const std::filesystem::path& scenarioPath,
    const std::filesystem::path& artifactRoot) {
    std::string error;
    const std::string source = ReadText(scenarioPath, error);
    if (!error.empty()) {
        WriteFailureArtifact(artifactRoot, error);
        return 1;
    }
    JsonValue root;
    if (!JsonValue::Parse(source, root, error) ||
        root.GetKind() != JsonValue::Kind::Object) {
        WriteFailureArtifact(
            artifactRoot,
            error.empty() ? "scenario root must be an object" : error);
        return 1;
    }
    const auto schema = StringMember(root, "schema", error);
    const JsonValue* steps = Member(
        root, "steps", JsonValue::Kind::Array, error);
    if (!schema || *schema != kSchema || steps == nullptr) {
        WriteFailureArtifact(
            artifactRoot,
            !schema || *schema != kSchema
                ? "unsupported scenario schema"
                : error);
        return 1;
    }
    const bool continueOnFailure =
        BoolMember(root, "continueOnFailure", error, false)
            .value_or(false);
    if (!error.empty()) {
        WriteFailureArtifact(artifactRoot, error);
        return 1;
    }

    const std::filesystem::path absoluteArtifacts =
        std::filesystem::absolute(artifactRoot);
    std::error_code artifactError;
    std::filesystem::remove_all(
        absoluteArtifacts, artifactError);
    if (artifactError) return 1;
    std::filesystem::create_directories(
        absoluteArtifacts / "workspace", artifactError);
    if (artifactError) return 1;
    {
        std::ofstream copy(
            absoluteArtifacts / "scenario.json",
            std::ios::binary | std::ios::trunc);
        copy.write(
            source.data(),
            static_cast<std::streamsize>(source.size()));
        if (!copy.good()) return 1;
    }

    const ScopedProjectFile projectScope{
        absoluteArtifacts / "workspace" / "Project.21kbproject" };

    std::vector<std::string> reportLines;
    bool passed = true;
    {
        EditorSceneContext context;
        ScenarioState state{
            context, absoluteArtifacts,
            std::filesystem::absolute(scenarioPath)
                .parent_path().lexically_normal() };
        for (std::size_t index = 0U; index < steps->Size(); ++index) {
            const JsonValue* step = steps->At(index);
            const StepOutcome outcome =
                step == nullptr
                ? StepOutcome{ false, "missing step" }
                : ExecuteStep(state, *step);
            const JsonValue* operation =
                step == nullptr ? nullptr : step->Find("op");
            const std::string operationName =
                operation != nullptr &&
                    operation->GetKind() == JsonValue::Kind::String
                ? operation->AsString()
                : "<invalid>";
            std::ostringstream line;
            line << (outcome.succeeded ? "[PASS] " : "[FAIL] ")
                 << "step " << (index + 1U) << ' ' << operationName;
            if (!outcome.detail.empty()) {
                line << " - " << outcome.detail;
            }
            reportLines.push_back(line.str());
            state.automation.Trace(
                "scenario_step", outcome.succeeded,
                std::to_string(index + 1U) + ':' + operationName);
            passed = passed && outcome.succeeded;
            if (!outcome.succeeded && !continueOnFailure) break;
        }
        state.automation.SnapshotConsole("final");
        if (context.HasPlayModeSceneSession()) {
            static_cast<void>(
                context.RestorePlayModeSceneSession());
        }
    }
    std::ofstream report(
        absoluteArtifacts / "report.txt",
        std::ios::binary | std::ios::trunc);
    report << "21kb external editor automation\n"
           << "Schema: " << kSchema << '\n'
           << "Scenario: " << scenarioPath.string() << '\n'
           << "========================================\n";
    for (const std::string& line : reportLines) {
        report << line << '\n';
    }
    report << "========================================\n"
           << "RESULT: " << (passed ? "PASS" : "FAIL") << '\n';
    if (!report.good()) return 1;

    std::ofstream manifest(
        absoluteArtifacts / "manifest.txt",
        std::ios::binary | std::ios::trunc);
    manifest << "task=" << absoluteArtifacts.filename().string()
             << '\n'
             << "schema=" << kSchema << '\n'
             << "stepsExecuted=" << reportLines.size() << '\n'
             << "result=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed && manifest.good() ? 0 : 1;
}

} // namespace kb::editor
#endif
