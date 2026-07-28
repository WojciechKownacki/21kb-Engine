#include "app/EditorAutomationScenarioRunner.hpp"

#if defined(_WIN32)
#include "app/EditorHeadlessAutomation.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptValue.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/EditorSceneContext.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
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
#include <unordered_map>
#include <vector>

namespace kb::editor {
namespace {

using kb::core::JsonValue;

constexpr std::string_view kSchema =
    "21kb.editor-automation/v1";
constexpr std::uintmax_t kMaxScenarioBytes = 64U * 1024U * 1024U;

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
        const std::filesystem::path& artifacts)
        : context(sceneContext)
        , automation(sceneContext, artifacts / "automation") {}

    EditorSceneContext& context;
    EditorHeadlessAutomation automation;
    std::unordered_map<std::string, EntityAlias> entities;
    std::unordered_map<std::string, kb::assets::AssetId> assets;
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

    if (*operation == "write_file") {
        const auto path = StringMember(step, "path", error);
        const auto content = StringMember(step, "content", error);
        if (!path || !content) return { false, error };
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
            content->data(),
            static_cast<std::streamsize>(content->size()));
        if (!output.good()) {
            return { false, "file write failed" };
        }
        return { true, resolved->string() };
    }

    if (*operation == "discover_assets") {
        const std::size_t count =
            state.context.Scene().Assets().Discover();
        return { true, std::to_string(count) + " asset(s)" };
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
                kb::script::ScriptSceneComponentApi::HasComponent(
                    state.context.Scene(), entity, *component);
        }
        const bool expected =
            BoolMember(step, "exists", error, false).value_or(true);
        return {
            found == expected,
            std::string{ found ? "present" : "absent" } };
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
            if (state.context.ReadInputMappingContextAsset(contextId)
                    ->mappings.size() > index) {
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
        return {
            state.automation.CaptureRuntime(*checkpoint),
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
        const bool found = std::ranges::any_of(
            state.context.Console().Entries(),
            [&contains](const EditorConsoleEntry& entry) {
                return entry.message.find(*contains) !=
                    std::string::npos;
            });
        return { found, found ? *contains : "text not found" };
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
        ScenarioState state{ context, absoluteArtifacts };
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
