#include "engine/script/ScriptInputApi.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/input/InputContextPriority.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputLocalUser.hpp"
#include "engine/input/InputHaptics.hpp"
#include "engine/input/InputRebinding.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <charconv>
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

std::string ActionName(std::span<const ScriptFunctionArgument> arguments) {
    const ScriptValue* value = FindArg(arguments, "action");
    return value != nullptr ? value->AsString() : std::string{};
}

// Optional; absent (or <= 0) means the primary local user, so every existing
// call site that predates LIB-115 keeps querying exactly what it always has.
kb::input::LocalUserId PlayerFromArgs(std::span<const ScriptFunctionArgument> arguments) {
    const ScriptValue* value = FindArg(arguments, "player");
    if (value == nullptr) {
        return kb::input::kPrimaryLocalUser;
    }
    const int player = value->AsInt();
    return player > 0 ? kb::input::LocalUserId{static_cast<std::uint32_t>(player)} : kb::input::kPrimaryLocalUser;
}

ScriptFunctionPin PlayerPin() {
    return ScriptFunctionPin{"player", ScriptValueType::Int, false};
}

ScriptFunctionCallResult NoScene() {
    return ScriptFunctionCallResult{.executed = false, .outputs = {}, .errors = {"input api requires an active scene"}};
}

// Helper to build a one-output boolean result.
ScriptFunctionCallResult BoolResult(std::string_view pin, bool value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {ScriptFunctionArgument{std::string{pin}, ScriptValue{value}}},
        .errors = {}};
}

ScriptFunctionCallResult RebindOperationResult(
    std::string_view statusPin, bool succeeded, std::string error = {}) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{
                std::string{statusPin}, ScriptValue{succeeded}},
            ScriptFunctionArgument{"error", ScriptValue{std::move(error)}},
        },
        .errors = {}};
}

bool RegisterActionQuery(ScriptRuntimeHost& host, std::string name, std::string outputPin,
                         bool (kb::input::InputSubsystem::*query)(std::string_view) const) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"action", ScriptValueType::String, true}, PlayerPin()};
    desc.signature.outputs = {ScriptFunctionPin{outputPin, ScriptValueType::Bool, true}};
    desc.callback = [outputPin, query](const ScriptFunctionCallContext& context,
                                       std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const bool value = (context.scene->Input(PlayerFromArgs(arguments)).*query)(ActionName(arguments));
        return BoolResult(outputPin, value);
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterValueQuery(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"action", ScriptValueType::String, true}, PlayerPin()};
    desc.signature.outputs = {ScriptFunctionPin{"value", ScriptValueType::Float, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const kb::input::InputValue value = context.scene->Input(PlayerFromArgs(arguments)).GetActionValue(ActionName(arguments));
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {ScriptFunctionArgument{"value", ScriptValue{value.AsAxis1D()}}},
            .errors = {}};
    };
    return host.RegisterFunction(std::move(desc));
}

// Reads the action's raw/modified value as a bool (value.AsBool(), i.e. x != 0),
// distinct from Held/Pressed/Released below: those reflect whether a *trigger*
// fired (respecting deadzones, Hold thresholds, etc. - see InputMappingEvaluator),
// while ActionBool reflects the value itself, mirroring Unreal's direct
// FInputActionValue::Get<bool>() read versus binding to a trigger event.
bool RegisterActionBoolQuery(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"action", ScriptValueType::String, true}, PlayerPin()};
    desc.signature.outputs = {ScriptFunctionPin{"value", ScriptValueType::Bool, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const kb::input::InputValue value = context.scene->Input(PlayerFromArgs(arguments)).GetActionValue(ActionName(arguments));
        return BoolResult("value", value.AsBool());
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterValueQueryXY(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"action", ScriptValueType::String, true}, PlayerPin()};
    desc.signature.outputs = {ScriptFunctionPin{"x", ScriptValueType::Float, true},
                              ScriptFunctionPin{"y", ScriptValueType::Float, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const kb::input::InputValue value = context.scene->Input(PlayerFromArgs(arguments)).GetActionValue(ActionName(arguments));
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {ScriptFunctionArgument{"x", ScriptValue{value.x}},
                        ScriptFunctionArgument{"y", ScriptValue{value.y}}},
            .errors = {}};
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterValueQueryXYZ(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"action", ScriptValueType::String, true}, PlayerPin()};
    desc.signature.outputs = {
        ScriptFunctionPin{"x", ScriptValueType::Float, true},
        ScriptFunctionPin{"y", ScriptValueType::Float, true},
        ScriptFunctionPin{"z", ScriptValueType::Float, true},
    };
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const kb::input::InputValue value = context.scene->Input(PlayerFromArgs(arguments)).GetActionValue(ActionName(arguments));
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {ScriptFunctionArgument{"x", ScriptValue{value.x}},
                        ScriptFunctionArgument{"y", ScriptValue{value.y}},
                        ScriptFunctionArgument{"z", ScriptValue{value.z}}},
            .errors = {}};
    };
    return host.RegisterFunction(std::move(desc));
}

[[nodiscard]] bool ParseUnsignedId(
    std::string_view text, std::uint64_t& id) noexcept {
    id = 0U;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), id);
    return parsed.ec == std::errc{} &&
        parsed.ptr == text.data() + text.size();
}

[[nodiscard]] std::uint64_t ResolveMappingContextId(
    kb::scene::Scene& scene, std::string_view reference) {
    const auto isMappingContext = [](const kb::assets::AssetMetadata* metadata) {
        return metadata != nullptr && metadata->type == "InputMappingContext";
    };

    // Preserve the legacy decimal-id form used by the direct Lua wrapper.
    // AssetRef strings use hexadecimal ids, while normal authoring uses a
    // virtual /Game path; all three forms must resolve to the same asset.
    std::uint64_t decimalId = 0U;
    if (ParseUnsignedId(reference, decimalId) && decimalId != 0U) {
        const kb::assets::AssetMetadata* metadata =
            scene.Assets().Manager().Registry().Find(kb::assets::AssetId{decimalId});
        if (isMappingContext(metadata)) {
            return decimalId;
        }
        // Resolver-only scenes predate the asset registry integration and
        // legitimately expose mapping contexts by numeric id alone.
        if (scene.Assets().Manager().Registry().Count() == 0U) {
            return decimalId;
        }
    }

    kb::assets::AssetId assetId{};
    if (kb::assets::TryParseAssetId(reference, assetId) && assetId.IsValid()) {
        const kb::assets::AssetMetadata* metadata =
            scene.Assets().Manager().Registry().Find(assetId);
        if (isMappingContext(metadata)) {
            return assetId.value;
        }
    }

    const kb::assets::AssetMetadata* metadata =
        scene.Assets().Manager().Registry().FindByPath(
            std::filesystem::path{reference});
    return isMappingContext(metadata) ? metadata->id.value : 0U;
}

bool RegisterAddMappingContext(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"context", ScriptValueType::String, true},
                             ScriptFunctionPin{"priority", ScriptValueType::Int, false},
                             PlayerPin()};
    desc.signature.outputs = {ScriptFunctionPin{"added", ScriptValueType::Bool, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const ScriptValue* contextArg = FindArg(arguments, "context");
        const ScriptValue* priorityArg = FindArg(arguments, "priority");
        const std::uint64_t id = contextArg != nullptr
            ? ResolveMappingContextId(*context.scene, contextArg->AsString())
            : 0U;
        const auto priority = static_cast<std::int32_t>(priorityArg != nullptr ? priorityArg->AsInt() : 0);
        const bool added = context.scene->Input(PlayerFromArgs(arguments)).AddMappingContext(id, priority);
        return BoolResult("added", added);
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterRemoveMappingContext(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"context", ScriptValueType::String, true}, PlayerPin()};
    desc.signature.outputs = {ScriptFunctionPin{"removed", ScriptValueType::Bool, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const ScriptValue* contextArg = FindArg(arguments, "context");
        const std::uint64_t id = contextArg != nullptr
            ? ResolveMappingContextId(*context.scene, contextArg->AsString())
            : 0U;
        kb::input::InputSubsystem& input = context.scene->Input(PlayerFromArgs(arguments));
        const bool had = input.HasMappingContext(id);
        input.RemoveMappingContext(id);
        return BoolResult("removed", had);
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterRebind(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Input.Rebind";
    desc.signature.inputs = {
        ScriptFunctionPin{"context", ScriptValueType::String, true},
        ScriptFunctionPin{"binding", ScriptValueType::String, true},
        ScriptFunctionPin{"key", ScriptValueType::String, true},
        ScriptFunctionPin{"gamepadIndex", ScriptValueType::Int, false},
        ScriptFunctionPin{"allowConflict", ScriptValueType::Bool, false},
        PlayerPin(),
    };
    desc.signature.outputs = {
        ScriptFunctionPin{"applied", ScriptValueType::Bool, true},
        ScriptFunctionPin{"conflict", ScriptValueType::String, true},
    };
    desc.callback = [](const ScriptFunctionCallContext& context,
                       std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const ScriptValue* contextArg = FindArg(arguments, "context");
        const ScriptValue* bindingArg = FindArg(arguments, "binding");
        const ScriptValue* keyArg = FindArg(arguments, "key");
        const ScriptValue* gamepadArg = FindArg(arguments, "gamepadIndex");
        const ScriptValue* allowConflictArg =
            FindArg(arguments, "allowConflict");

        std::uint64_t contextId = 0U;
        std::uint64_t bindingId = 0U;
        const std::string contextText =
            contextArg != nullptr ? contextArg->AsString() : std::string{};
        const std::string bindingText =
            bindingArg != nullptr ? bindingArg->AsString() : std::string{};
        const std::string keyText =
            keyArg != nullptr ? keyArg->AsString() : std::string{};
        const kb::input::InputKey key = kb::input::ParseInputKey(keyText);
        const int gamepadIndex =
            gamepadArg != nullptr ? gamepadArg->AsInt() : 0;
        const bool validKey =
            key != kb::input::InputKey::None ||
            keyText == kb::input::ToString(kb::input::InputKey::None);
        if (!ParseUnsignedId(contextText, contextId) ||
            !ParseUnsignedId(bindingText, bindingId) || bindingId == 0U ||
            !validKey || gamepadIndex < 0 ||
            gamepadIndex >= kb::input::InputDeviceState::kMaxGamepads) {
            return ScriptFunctionCallResult{
                .executed = true,
                .outputs = {
                    ScriptFunctionArgument{"applied", ScriptValue{false}},
                    ScriptFunctionArgument{
                        "conflict", ScriptValue{std::string{}}},
                },
                .errors = {}};
        }

        const kb::input::InputRuntimeRebindResult result =
            context.scene->Input(PlayerFromArgs(arguments))
                .Rebind(
                    contextId, bindingId, key,
                    static_cast<std::uint8_t>(gamepadIndex),
                    allowConflictArg != nullptr &&
                        allowConflictArg->AsBool());
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {
                ScriptFunctionArgument{"applied", ScriptValue{result.applied}},
                ScriptFunctionArgument{
                    "conflict",
                    ScriptValue{
                        result.conflict.has_value()
                            ? std::to_string(
                                  result.conflict->conflictingBindingId)
                            : std::string{}}},
            },
            .errors = {}};
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterSaveRebindProfile(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Input.SaveRebindProfile";
    desc.signature.inputs = {
        ScriptFunctionPin{"context", ScriptValueType::String, true},
        ScriptFunctionPin{"path", ScriptValueType::String, true},
        PlayerPin(),
    };
    desc.signature.outputs = {
        ScriptFunctionPin{"saved", ScriptValueType::Bool, true},
        ScriptFunctionPin{"error", ScriptValueType::String, true},
    };
    desc.callback = [](const ScriptFunctionCallContext& context,
                       std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const ScriptValue* contextArg = FindArg(arguments, "context");
        const ScriptValue* pathArg = FindArg(arguments, "path");
        std::uint64_t contextId = 0U;
        const std::string contextText =
            contextArg != nullptr ? contextArg->AsString() : std::string{};
        const std::string path =
            pathArg != nullptr ? pathArg->AsString() : std::string{};
        if (!ParseUnsignedId(contextText, contextId) || path.empty()) {
            return RebindOperationResult(
                "saved", false, "invalid context id or profile path");
        }
        kb::input::InputSubsystem& input =
            context.scene->Input(PlayerFromArgs(arguments));
        if (!input.HasMappingContext(contextId)) {
            return RebindOperationResult(
                "saved", false, "mapping context is not active");
        }
        const std::span<const kb::input::InputRebindOverride> profile =
            input.RebindProfile(contextId);
        const bool saved = kb::input::WriteRebindProfile(
            std::filesystem::path{path}, profile);
        return RebindOperationResult(
            "saved", saved,
            saved ? std::string{} : "could not write rebind profile");
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterLoadRebindProfile(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Input.LoadRebindProfile";
    desc.signature.inputs = {
        ScriptFunctionPin{"context", ScriptValueType::String, true},
        ScriptFunctionPin{"path", ScriptValueType::String, true},
        PlayerPin(),
    };
    desc.signature.outputs = {
        ScriptFunctionPin{"loaded", ScriptValueType::Bool, true},
        ScriptFunctionPin{"error", ScriptValueType::String, true},
    };
    desc.callback = [](const ScriptFunctionCallContext& context,
                       std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const ScriptValue* contextArg = FindArg(arguments, "context");
        const ScriptValue* pathArg = FindArg(arguments, "path");
        std::uint64_t contextId = 0U;
        const std::string contextText =
            contextArg != nullptr ? contextArg->AsString() : std::string{};
        const std::string path =
            pathArg != nullptr ? pathArg->AsString() : std::string{};
        if (!ParseUnsignedId(contextText, contextId) || path.empty()) {
            return RebindOperationResult(
                "loaded", false, "invalid context id or profile path");
        }
        kb::input::InputAssetLoadResult<
            std::vector<kb::input::InputRebindOverride>>
            loaded =
                kb::input::ReadRebindProfile(std::filesystem::path{path});
        if (!loaded.succeeded) {
            return RebindOperationResult(
                "loaded", false, std::move(loaded.error));
        }
        const bool applied =
            context.scene->Input(PlayerFromArgs(arguments))
                .SetRebindProfile(contextId, loaded.asset);
        return RebindOperationResult(
            "loaded", applied,
            applied ? std::string{}
                    : "mapping context could not be resolved");
    };
    return host.RegisterFunction(std::move(desc));
}

// The mouse is a singular physical device (unlike gamepads), shared by every
// local user - so unlike Input.* above, Pointer.* takes no player pin and
// always reads the primary local user's device state (LIB-115's
// kPrimaryLocalUser), which is where the platform collector writes it.
bool RegisterPointerPosition(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Pointer.Position";
    desc.signature.outputs = {ScriptFunctionPin{"x", ScriptValueType::Float, true},
                              ScriptFunctionPin{"y", ScriptValueType::Float, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const kb::input::InputDeviceState& device = context.scene->Input().DeviceState();
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {ScriptFunctionArgument{"x", ScriptValue{device.PointerX()}},
                        ScriptFunctionArgument{"y", ScriptValue{device.PointerY()}}},
            .errors = {}};
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterPointerDelta(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Pointer.Delta";
    desc.signature.outputs = {ScriptFunctionPin{"x", ScriptValueType::Float, true},
                              ScriptFunctionPin{"y", ScriptValueType::Float, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const kb::input::InputDeviceState& device = context.scene->Input().DeviceState();
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {ScriptFunctionArgument{"x", ScriptValue{device.GetValue(kb::input::InputKey::MouseX)}},
                        ScriptFunctionArgument{"y", ScriptValue{device.GetValue(kb::input::InputKey::MouseY)}}},
            .errors = {}};
    };
    return host.RegisterFunction(std::move(desc));
}

// 0=left, 1=right, 2=middle - the same convention as Unity's Input.GetMouseButton.
[[nodiscard]] kb::input::InputKey MouseButtonKey(int button) noexcept {
    switch (button) {
        case 1:
            return kb::input::InputKey::MouseRight;
        case 2:
            return kb::input::InputKey::MouseMiddle;
        default:
            return kb::input::InputKey::MouseLeft;
    }
}

bool RegisterPointerButton(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Pointer.Button";
    desc.signature.inputs = {ScriptFunctionPin{"button", ScriptValueType::Int, true}};
    desc.signature.outputs = {ScriptFunctionPin{"pressed", ScriptValueType::Bool, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const ScriptValue* buttonArg = FindArg(arguments, "button");
        const kb::input::InputKey key = MouseButtonKey(buttonArg != nullptr ? buttonArg->AsInt() : 0);
        const bool pressed = context.scene->Input().DeviceState().IsKeyDown(key);
        return BoolResult("pressed", pressed);
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterPointerScroll(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Pointer.Scroll";
    desc.signature.outputs = {ScriptFunctionPin{"delta", ScriptValueType::Float, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const float delta = context.scene->Input().DeviceState().GetValue(
            kb::input::InputKey::MouseWheel);
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {ScriptFunctionArgument{"delta", ScriptValue{delta}}},
            .errors = {},
        };
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterPointerRay(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Pointer.Ray";
    desc.signature.outputs = {
        ScriptFunctionPin{"valid", ScriptValueType::Bool, true},
        ScriptFunctionPin{"originX", ScriptValueType::Float, true},
        ScriptFunctionPin{"originY", ScriptValueType::Float, true},
        ScriptFunctionPin{"originZ", ScriptValueType::Float, true},
        ScriptFunctionPin{"directionX", ScriptValueType::Float, true},
        ScriptFunctionPin{"directionY", ScriptValueType::Float, true},
        ScriptFunctionPin{"directionZ", ScriptValueType::Float, true},
    };
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const kb::input::InputDeviceState& device = context.scene->Input().DeviceState();
        const kb::scene::SceneRenderCameraRay cameraRay =
            kb::scene::SceneRenderFeedback::ScreenPointToRay(
                *context.scene, device.PointerX(), device.PointerY());
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {
                ScriptFunctionArgument{"valid", ScriptValue{cameraRay.valid}},
                ScriptFunctionArgument{"originX", ScriptValue{cameraRay.ray.origin.x}},
                ScriptFunctionArgument{"originY", ScriptValue{cameraRay.ray.origin.y}},
                ScriptFunctionArgument{"originZ", ScriptValue{cameraRay.ray.origin.z}},
                ScriptFunctionArgument{"directionX", ScriptValue{cameraRay.ray.direction.x}},
                ScriptFunctionArgument{"directionY", ScriptValue{cameraRay.ray.direction.y}},
                ScriptFunctionArgument{"directionZ", ScriptValue{cameraRay.ray.direction.z}},
            },
            .errors = {},
        };
    };
    return host.RegisterFunction(std::move(desc));
}

// Constant-returning functions so scripts reference the named priority bands
// (LIB-118) by name instead of hardcoding magic numbers into
// Input.AddMappingContext's priority argument. No existing mechanism in this
// registry exposes plain constants (only callable functions), so these are
// zero-input functions returning the int - the same shape every other
// registration here already uses, not a new kind of registration.
bool RegisterPriorityConstant(ScriptRuntimeHost& host, std::string name, std::int32_t value) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.outputs = {ScriptFunctionPin{"priority", ScriptValueType::Int, true}};
    desc.callback = [value](const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument>) {
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {ScriptFunctionArgument{"priority", ScriptValue{static_cast<int>(value)}}},
            .errors = {}};
    };
    return host.RegisterFunction(std::move(desc));
}

// LIB-120: whether the host window currently has focus - lets gameplay code
// distinguish "genuinely nothing pressed" from "input is suppressed because
// the window lost focus / is in the background" and react (e.g. auto-pause).
bool RegisterHasFocus(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Input.HasFocus";
    desc.signature.outputs = {ScriptFunctionPin{"focus", ScriptValueType::Bool, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        return BoolResult("focus", context.scene->Input().DeviceState().HasFocus());
    };
    return host.RegisterFunction(std::move(desc));
}

// LIB-120: hardware presence for a specific gamepad slot, independent of
// whether it is pressing anything - see InputDeviceState::IsGamepadConnected.
bool RegisterIsGamepadConnected(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Input.IsGamepadConnected";
    desc.signature.inputs = {ScriptFunctionPin{"gamepadIndex", ScriptValueType::Int, true}};
    desc.signature.outputs = {ScriptFunctionPin{"connected", ScriptValueType::Bool, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const ScriptValue* indexArg = FindArg(arguments, "gamepadIndex");
        const auto gamepadIndex = static_cast<std::uint8_t>(indexArg != nullptr ? indexArg->AsInt() : 0);
        return BoolResult("connected", context.scene->Input().DeviceState().IsGamepadConnected(gamepadIndex));
    };
    return host.RegisterFunction(std::move(desc));
}

// LIB-153: haptics capability for a gamepad slot - honest supported/connected/limits
// through the host-registered backend (supported=false with a reason when no backend or
// platform support exists, never a fake no-op actuator).
bool RegisterHasHaptics(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Input.HasHaptics";
    desc.signature.inputs = {ScriptFunctionPin{"gamepadIndex", ScriptValueType::Int, true}};
    desc.signature.outputs = {
        ScriptFunctionPin{"supported", ScriptValueType::Bool, true},
        ScriptFunctionPin{"connected", ScriptValueType::Bool, true},
        ScriptFunctionPin{"dualMotor", ScriptValueType::Bool, true},
        ScriptFunctionPin{"maxGamepads", ScriptValueType::Int, true},
        ScriptFunctionPin{"reason", ScriptValueType::String, true},
    };
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const ScriptValue* indexArg = FindArg(arguments, "gamepadIndex");
        const kb::input::InputHapticsCapability capability = kb::input::InputHaptics::Capability(
            *context.scene,
            static_cast<std::uint32_t>(indexArg != nullptr ? indexArg->AsInt() : 0));
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {
                ScriptFunctionArgument{"supported", ScriptValue{capability.supported}},
                ScriptFunctionArgument{"connected", ScriptValue{capability.connected}},
                ScriptFunctionArgument{"dualMotor", ScriptValue{capability.dualMotor}},
                ScriptFunctionArgument{"maxGamepads", ScriptValue{static_cast<int>(capability.maxGamepads)}},
                ScriptFunctionArgument{"reason", ScriptValue{capability.disabledReason}},
            },
            .errors = {},
        };
    };
    return host.RegisterFunction(std::move(desc));
}

// LIB-153: the actuator - dual motor magnitudes in [0,1] (the platform limit XInput-class
// devices expose). Honest false for an out-of-range slot, a disconnected pad, or no
// backend.
bool RegisterSetVibration(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Input.SetVibration";
    desc.signature.inputs = {
        ScriptFunctionPin{"gamepadIndex", ScriptValueType::Int, true},
        ScriptFunctionPin{"lowFrequency", ScriptValueType::Float, true},
        ScriptFunctionPin{"highFrequency", ScriptValueType::Float, true},
    };
    desc.signature.outputs = {ScriptFunctionPin{"applied", ScriptValueType::Bool, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const ScriptValue* indexArg = FindArg(arguments, "gamepadIndex");
        const ScriptValue* lowArg = FindArg(arguments, "lowFrequency");
        const ScriptValue* highArg = FindArg(arguments, "highFrequency");
        const bool applied = kb::input::InputHaptics::SetVibration(
            *context.scene,
            static_cast<std::uint32_t>(indexArg != nullptr ? indexArg->AsInt() : 0),
            lowArg != nullptr ? lowArg->AsFloat() : 0.0F,
            highArg != nullptr ? highArg->AsFloat() : 0.0F);
        return BoolResult("applied", applied);
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterStopVibration(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Input.StopVibration";
    desc.signature.outputs = {ScriptFunctionPin{"stopped", ScriptValueType::Bool, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const bool hasBackend = kb::input::InputHaptics::HasBackend(*context.scene);
        kb::input::InputHaptics::StopAll(*context.scene);
        return BoolResult("stopped", hasBackend);
    };
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptInputApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterActionQuery(host, "Input.IsPressed", "pressed", &kb::input::InputSubsystem::IsActionPressed) && ok;
    ok = RegisterActionQuery(host, "Input.WasPressed", "pressed", &kb::input::InputSubsystem::WasActionStarted) && ok;
    ok = RegisterActionQuery(host, "Input.WasReleased", "released", &kb::input::InputSubsystem::WasActionReleased) && ok;
    ok = RegisterValueQuery(host, "Input.Value") && ok;
    ok = RegisterValueQueryXY(host, "Input.Vector2") && ok;
    ok = RegisterValueQueryXYZ(host, "Input.Vector3") && ok;
    ok = RegisterAddMappingContext(host, "Input.AddMappingContext") && ok;
    ok = RegisterRemoveMappingContext(host, "Input.RemoveMappingContext") && ok;
    ok = RegisterRebind(host) && ok;
    ok = RegisterSaveRebindProfile(host) && ok;
    ok = RegisterLoadRebindProfile(host) && ok;

    ok = RegisterActionBoolQuery(host, "Input.ActionBool") && ok;
    ok = RegisterValueQuery(host, "Input.ActionFloat") && ok;
    ok = RegisterValueQueryXY(host, "Input.Action2D") && ok;
    ok = RegisterActionQuery(host, "Input.Pressed", "pressed", &kb::input::InputSubsystem::WasActionStarted) && ok;
    ok = RegisterActionQuery(host, "Input.Released", "released", &kb::input::InputSubsystem::WasActionReleased) && ok;
    ok = RegisterActionQuery(host, "Input.Held", "held", &kb::input::InputSubsystem::IsActionPressed) && ok;

    ok = RegisterPointerPosition(host) && ok;
    ok = RegisterPointerDelta(host) && ok;
    ok = RegisterPointerButton(host) && ok;
    ok = RegisterPointerScroll(host) && ok;
    ok = RegisterPointerRay(host) && ok;

    ok = RegisterPriorityConstant(host, "Input.PriorityGameplay", kb::input::InputContextPriority::Gameplay) && ok;
    ok = RegisterPriorityConstant(host, "Input.PriorityUI", kb::input::InputContextPriority::UI) && ok;
    ok = RegisterPriorityConstant(host, "Input.PriorityConsole", kb::input::InputContextPriority::Console) && ok;
    ok = RegisterPriorityConstant(host, "Input.PriorityDebugOverlay", kb::input::InputContextPriority::DebugOverlay) && ok;

    ok = RegisterHasFocus(host) && ok;
    ok = RegisterIsGamepadConnected(host) && ok;
    ok = RegisterHasHaptics(host) && ok;
    ok = RegisterSetVibration(host) && ok;
    ok = RegisterStopVibration(host) && ok;

    ok = RegisterActionQuery(host, "IsActionPressed", "pressed", &kb::input::InputSubsystem::IsActionPressed) && ok;
    ok = RegisterActionQuery(host, "WasActionStarted", "started", &kb::input::InputSubsystem::WasActionStarted) && ok;
    ok = RegisterActionQuery(host, "WasActionTriggered", "triggered", &kb::input::InputSubsystem::WasActionTriggered) && ok;
    ok = RegisterActionQuery(host, "WasActionCompleted", "completed", &kb::input::InputSubsystem::WasActionCompleted) && ok;
    ok = RegisterActionQuery(host, "WasActionReleased", "released", &kb::input::InputSubsystem::WasActionReleased) && ok;
    ok = RegisterValueQuery(host, "GetActionValue") && ok;
    ok = RegisterValueQueryXY(host, "GetActionValueXY") && ok;
    ok = RegisterValueQueryXYZ(host, "GetActionValueXYZ") && ok;
    ok = RegisterAddMappingContext(host, "AddMappingContext") && ok;
    ok = RegisterRemoveMappingContext(host, "RemoveMappingContext") && ok;
    return ok;
}

} // namespace kb::script
