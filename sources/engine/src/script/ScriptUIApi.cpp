#include "engine/script/ScriptUIApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneUIDocuments.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <span>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

const ScriptValue* Arg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) if (argument.name == name) return &argument.value;
    return nullptr;
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

kb::scene::SceneEntity Target(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const ScriptValue* explicitEntity = Arg(arguments, "entity");
    return explicitEntity == nullptr ? context.caller : kb::scene::SceneEntity{ explicitEntity->AsUInt64() };
}

ScriptFunctionCallResult Applied(bool applied, std::string failure) {
    return applied ? ScriptFunctionCallResult{ .executed = true, .outputs = { { "applied", ScriptValue{ true } } } }
                   : Error(std::move(failure));
}

std::optional<kb::scene::UIControlKind> ControlKind(std::string_view value) {
    if (value == "Container") return kb::scene::UIControlKind::Container;
    if (value == "Text") return kb::scene::UIControlKind::Text;
    if (value == "Image") return kb::scene::UIControlKind::Image;
    if (value == "Button") return kb::scene::UIControlKind::Button;
    if (value == "Toggle") return kb::scene::UIControlKind::Toggle;
    if (value == "Slider") return kb::scene::UIControlKind::Slider;
    if (value == "List") return kb::scene::UIControlKind::List;
    if (value == "InputField") return kb::scene::UIControlKind::InputField;
    if (value == "ScrollView") return kb::scene::UIControlKind::ScrollView;
    if (value == "ModalDialog") return kb::scene::UIControlKind::ModalDialog;
    return std::nullopt;
}

std::optional<kb::scene::UINavigationDirection> NavigationDirection(std::string_view value) {
    if (value == "Next") return kb::scene::UINavigationDirection::Next;
    if (value == "Previous") return kb::scene::UINavigationDirection::Previous;
    if (value == "Up") return kb::scene::UINavigationDirection::Up;
    if (value == "Down") return kb::scene::UINavigationDirection::Down;
    if (value == "Left") return kb::scene::UINavigationDirection::Left;
    if (value == "Right") return kb::scene::UINavigationDirection::Right;
    return std::nullopt;
}

std::optional<kb::scene::UIControlState> ExistingControl(const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments, std::string& error) {
    if (context.scene == nullptr) {
        error = "UI control API requires an active scene";
        return std::nullopt;
    }
    const auto control = context.scene->UIDocuments().Control(Target(context, arguments), Arg(arguments, "element")->AsUInt64());
    if (!control.has_value()) error = "UI control API requires a live UI element";
    return control;
}

bool IsOneOf(kb::scene::UIControlKind kind, std::initializer_list<kb::scene::UIControlKind> allowed) {
    for (const auto candidate : allowed) if (kind == candidate) return true;
    return false;
}

ScriptFunctionCallResult QueueControl(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments,
    kb::scene::UIControlState control, std::initializer_list<kb::scene::UIControlKind> allowed, std::string failure) {
    if (!IsOneOf(control.kind, allowed)) return Error(std::move(failure));
    return Applied(context.scene->UIDocuments().QueueSetControl(Target(context, arguments), Arg(arguments, "element")->AsUInt64(), control),
        "UI control update was rejected by the runtime queue");
}

ScriptFunctionCallResult Create(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("UI.Create requires an active scene");
    const ScriptValue* styleClass = Arg(arguments, "styleClass");
    const ScriptValue* visible = Arg(arguments, "visible");
    kb::scene::UIControlState control{};
    if (const ScriptValue* kind = Arg(arguments, "kind"); kind != nullptr) {
        const auto parsed = ControlKind(kind->AsString());
        if (!parsed.has_value()) return Error("UI.Create kind must name a supported UI control");
        control.kind = *parsed;
    }
    if (const ScriptValue* text = Arg(arguments, "text"); text != nullptr) control.text = text->AsString();
    if (const ScriptValue* image = Arg(arguments, "image"); image != nullptr) control.imageAssetId = image->AsUInt64();
    if (const ScriptValue* toggle = Arg(arguments, "toggle"); toggle != nullptr) control.toggleValue = toggle->AsBool();
    if (const ScriptValue* value = Arg(arguments, "value"); value != nullptr) control.sliderValue = value->AsFloat();
    if (const ScriptValue* minimum = Arg(arguments, "minimum"); minimum != nullptr) control.sliderMinimum = minimum->AsFloat();
    if (const ScriptValue* maximum = Arg(arguments, "maximum"); maximum != nullptr) control.sliderMaximum = maximum->AsFloat();
    if (const ScriptValue* scroll = Arg(arguments, "scroll"); scroll != nullptr) control.scrollOffset = scroll->AsFloat();
    if (const ScriptValue* modal = Arg(arguments, "modal"); modal != nullptr) control.modalOpen = modal->AsBool();
    const auto element = context.scene->UIDocuments().QueueCreate(Target(context, arguments), kb::scene::UIRuntimeElementDesc{
        .parentId = Arg(arguments, "parent")->AsUInt64(),
        .name = Arg(arguments, "name")->AsString(),
        .styleClass = styleClass == nullptr ? std::string{} : styleClass->AsString(),
        .visible = visible == nullptr || visible->AsBool(),
        .control = std::move(control),
    });
    if (!element.has_value()) return Error("UI.Create requires a live UI document, a live parent, a non-empty name, and queue capacity");
    return ScriptFunctionCallResult{ .executed = true, .outputs = { { "element", ScriptValue{ *element, ScriptValueType::Hash } } } };
}

ScriptFunctionCallResult Destroy(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("UI.Destroy requires an active scene");
    return Applied(context.scene->UIDocuments().QueueDestroy(Target(context, arguments), Arg(arguments, "element")->AsUInt64()),
        "UI.Destroy requires a live non-root runtime element that is not already queued for destruction");
}

ScriptFunctionCallResult SetVisible(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments, bool visible) {
    if (context.scene == nullptr) return Error("UI visibility API requires an active scene");
    const bool queued = visible
        ? context.scene->UIDocuments().QueueShow(Target(context, arguments), Arg(arguments, "element")->AsUInt64())
        : context.scene->UIDocuments().QueueHide(Target(context, arguments), Arg(arguments, "element")->AsUInt64());
    return Applied(queued, "UI visibility command requires a live element that is not queued for destruction");
}

ScriptFunctionCallResult Show(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) { return SetVisible(context, arguments, true); }
ScriptFunctionCallResult Hide(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) { return SetVisible(context, arguments, false); }

ScriptFunctionCallResult SetText(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->text = Arg(arguments, "text")->AsString();
    return QueueControl(context, arguments, std::move(*control), { kb::scene::UIControlKind::Text, kb::scene::UIControlKind::Button, kb::scene::UIControlKind::InputField },
        "UI.SetText requires a Text, Button, or InputField element");
}

ScriptFunctionCallResult SetImage(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->imageAssetId = Arg(arguments, "image")->AsUInt64();
    return QueueControl(context, arguments, std::move(*control), { kb::scene::UIControlKind::Image }, "UI.SetImage requires an Image element");
}

ScriptFunctionCallResult SetToggle(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->toggleValue = Arg(arguments, "value")->AsBool();
    return QueueControl(context, arguments, std::move(*control), { kb::scene::UIControlKind::Toggle }, "UI.SetToggle requires a Toggle element");
}

ScriptFunctionCallResult SetSlider(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->sliderValue = Arg(arguments, "value")->AsFloat();
    if (const ScriptValue* minimum = Arg(arguments, "minimum"); minimum != nullptr) control->sliderMinimum = minimum->AsFloat();
    if (const ScriptValue* maximum = Arg(arguments, "maximum"); maximum != nullptr) control->sliderMaximum = maximum->AsFloat();
    return QueueControl(context, arguments, std::move(*control), { kb::scene::UIControlKind::Slider }, "UI.SetSlider requires a Slider element with a valid range");
}

ScriptFunctionCallResult ListAppend(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->listItems.push_back(Arg(arguments, "item")->AsString());
    return QueueControl(context, arguments, std::move(*control), { kb::scene::UIControlKind::List }, "UI.ListAppend requires a List element and at most 4096 non-empty items");
}

ScriptFunctionCallResult ListClear(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->listItems.clear();
    return QueueControl(context, arguments, std::move(*control), { kb::scene::UIControlKind::List }, "UI.ListClear requires a List element");
}

ScriptFunctionCallResult SetScrollOffset(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->scrollOffset = Arg(arguments, "offset")->AsFloat();
    return QueueControl(context, arguments, std::move(*control), { kb::scene::UIControlKind::ScrollView }, "UI.SetScrollOffset requires a ScrollView element and a non-negative offset");
}

ScriptFunctionCallResult SetModalOpen(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->modalOpen = Arg(arguments, "open")->AsBool();
    return QueueControl(context, arguments, std::move(*control), { kb::scene::UIControlKind::ModalDialog }, "UI.SetModalOpen requires a ModalDialog element");
}

ScriptFunctionCallResult QueueEvent(const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments, kb::scene::UIRuntimeEvent event) {
    if (context.scene == nullptr) return Error("UI event API requires an active scene");
    event.elementId = Arg(arguments, "element")->AsUInt64();
    return Applied(context.scene->UIDocuments().QueueEvent(Target(context, arguments), event),
        "UI event requires a visible live UI element and valid event data");
}

ScriptFunctionCallResult EmitClick(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return QueueEvent(context, arguments, kb::scene::UIRuntimeEvent{
        .kind = kb::scene::UIRuntimeEventKind::Click,
        .pointerX = Arg(arguments, "x")->AsFloat(),
        .pointerY = Arg(arguments, "y")->AsFloat(),
    });
}

ScriptFunctionCallResult EmitPointer(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return QueueEvent(context, arguments, kb::scene::UIRuntimeEvent{
        .kind = kb::scene::UIRuntimeEventKind::Pointer,
        .pointerX = Arg(arguments, "x")->AsFloat(),
        .pointerY = Arg(arguments, "y")->AsFloat(),
    });
}

ScriptFunctionCallResult EmitSubmit(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return QueueEvent(context, arguments, kb::scene::UIRuntimeEvent{
        .kind = kb::scene::UIRuntimeEventKind::Submit,
        .text = Arg(arguments, "text")->AsString(),
    });
}

ScriptFunctionCallResult EmitChanged(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return QueueEvent(context, arguments, kb::scene::UIRuntimeEvent{
        .kind = kb::scene::UIRuntimeEventKind::Changed,
        .value = Arg(arguments, "value")->AsFloat(),
    });
}

ScriptFunctionCallResult EmitFocus(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    return QueueEvent(context, arguments, kb::scene::UIRuntimeEvent{
        .kind = kb::scene::UIRuntimeEventKind::Focus,
        .focused = Arg(arguments, "focused")->AsBool(),
    });
}

ScriptFunctionCallResult EmitNavigation(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const auto direction = NavigationDirection(Arg(arguments, "direction")->AsString());
    if (!direction.has_value()) return Error("UI.EmitNavigation direction must be Next, Previous, Up, Down, Left, or Right");
    return QueueEvent(context, arguments, kb::scene::UIRuntimeEvent{
        .kind = kb::scene::UIRuntimeEventKind::Navigation,
        .navigation = *direction,
    });
}

bool RegisterFunction(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    ScriptFunctionDesc function{};
    function.signature.name = std::move(name);
    function.signature.inputs = std::move(inputs);
    function.signature.outputs = std::move(outputs);
    function.callback = std::move(callback);
    return host.RegisterFunction(std::move(function));
}

std::vector<ScriptFunctionPin> Targeted(std::vector<ScriptFunctionPin> inputs) {
    inputs.push_back({ "entity", ScriptValueType::Entity, false });
    return inputs;
}

} // namespace

bool ScriptUIApi::Register(ScriptRuntimeHost& host) {
    const auto applied = std::vector<ScriptFunctionPin>{ { "applied", ScriptValueType::Bool, true } };
    return RegisterFunction(host, "UI.Create", Targeted({
            { "parent", ScriptValueType::Hash, true },
            { "name", ScriptValueType::String, true },
            { "styleClass", ScriptValueType::String, false },
            { "visible", ScriptValueType::Bool, false },
            { "kind", ScriptValueType::String, false },
            { "text", ScriptValueType::String, false },
            { "image", ScriptValueType::Hash, false },
            { "toggle", ScriptValueType::Bool, false },
            { "value", ScriptValueType::Float, false },
            { "minimum", ScriptValueType::Float, false },
            { "maximum", ScriptValueType::Float, false },
            { "scroll", ScriptValueType::Float, false },
            { "modal", ScriptValueType::Bool, false },
        }), { { "element", ScriptValueType::Hash, true } }, &Create) &&
        RegisterFunction(host, "UI.Destroy", Targeted({ { "element", ScriptValueType::Hash, true } }), applied, &Destroy) &&
        RegisterFunction(host, "UI.Show", Targeted({ { "element", ScriptValueType::Hash, true } }), applied, &Show) &&
        RegisterFunction(host, "UI.Hide", Targeted({ { "element", ScriptValueType::Hash, true } }), applied, &Hide) &&
        RegisterFunction(host, "UI.SetText", Targeted({ { "element", ScriptValueType::Hash, true }, { "text", ScriptValueType::String, true } }), applied, &SetText) &&
        RegisterFunction(host, "UI.SetImage", Targeted({ { "element", ScriptValueType::Hash, true }, { "image", ScriptValueType::Hash, true } }), applied, &SetImage) &&
        RegisterFunction(host, "UI.SetToggle", Targeted({ { "element", ScriptValueType::Hash, true }, { "value", ScriptValueType::Bool, true } }), applied, &SetToggle) &&
        RegisterFunction(host, "UI.SetSlider", Targeted({ { "element", ScriptValueType::Hash, true }, { "value", ScriptValueType::Float, true }, { "minimum", ScriptValueType::Float, false }, { "maximum", ScriptValueType::Float, false } }), applied, &SetSlider) &&
        RegisterFunction(host, "UI.ListAppend", Targeted({ { "element", ScriptValueType::Hash, true }, { "item", ScriptValueType::String, true } }), applied, &ListAppend) &&
        RegisterFunction(host, "UI.ListClear", Targeted({ { "element", ScriptValueType::Hash, true } }), applied, &ListClear) &&
        RegisterFunction(host, "UI.SetScrollOffset", Targeted({ { "element", ScriptValueType::Hash, true }, { "offset", ScriptValueType::Float, true } }), applied, &SetScrollOffset) &&
        RegisterFunction(host, "UI.SetModalOpen", Targeted({ { "element", ScriptValueType::Hash, true }, { "open", ScriptValueType::Bool, true } }), applied, &SetModalOpen) &&
        RegisterFunction(host, "UI.EmitClick", Targeted({ { "element", ScriptValueType::Hash, true }, { "x", ScriptValueType::Float, true }, { "y", ScriptValueType::Float, true } }), applied, &EmitClick) &&
        RegisterFunction(host, "UI.EmitPointer", Targeted({ { "element", ScriptValueType::Hash, true }, { "x", ScriptValueType::Float, true }, { "y", ScriptValueType::Float, true } }), applied, &EmitPointer) &&
        RegisterFunction(host, "UI.EmitSubmit", Targeted({ { "element", ScriptValueType::Hash, true }, { "text", ScriptValueType::String, true } }), applied, &EmitSubmit) &&
        RegisterFunction(host, "UI.EmitChanged", Targeted({ { "element", ScriptValueType::Hash, true }, { "value", ScriptValueType::Float, true } }), applied, &EmitChanged) &&
        RegisterFunction(host, "UI.EmitFocus", Targeted({ { "element", ScriptValueType::Hash, true }, { "focused", ScriptValueType::Bool, true } }), applied, &EmitFocus) &&
        RegisterFunction(host, "UI.EmitNavigation", Targeted({ { "element", ScriptValueType::Hash, true }, { "direction", ScriptValueType::String, true } }), applied, &EmitNavigation);
}

} // namespace kb::script
