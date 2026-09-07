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
    kb::scene::UIControlKind kind{};
    return kb::scene::TryParseUIControlKind(value, kind) ?
        std::optional<kb::scene::UIControlKind>{ kind } : std::nullopt;
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

std::optional<kb::scene::UIElementComponents> ExistingComponents(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments,
    std::string& error) {
    if (context.scene == nullptr) {
        error = "UI component API requires an active scene";
        return std::nullopt;
    }
    const auto components = context.scene->UIDocuments().ElementComponents(
        Target(context, arguments), Arg(arguments, "element")->AsUInt64());
    if (!components.has_value()) error = "UI component API requires a live UI element";
    return components;
}

ScriptFunctionCallResult QueueComponents(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments,
    kb::scene::UIElementComponents components) {
    return Applied(context.scene->UIDocuments().QueueSetComponents(
        Target(context, arguments), Arg(arguments, "element")->AsUInt64(), components),
        "UI component update was rejected by the runtime queue");
}

void SetFloat(std::span<const ScriptFunctionArgument> arguments, std::string_view name, float& value) {
    if (const ScriptValue* argument = Arg(arguments, name); argument != nullptr) value = argument->AsFloat();
}

void SetBool(std::span<const ScriptFunctionArgument> arguments, std::string_view name, bool& value) {
    if (const ScriptValue* argument = Arg(arguments, name); argument != nullptr) value = argument->AsBool();
}

void SetHash(std::span<const ScriptFunctionArgument> arguments, std::string_view name, std::uint64_t& value) {
    if (const ScriptValue* argument = Arg(arguments, name); argument != nullptr) value = argument->AsUInt64();
}

std::optional<kb::scene::UIAlignment> Alignment(std::string_view value) {
    if (value == "Start") return kb::scene::UIAlignment::Start;
    if (value == "Center") return kb::scene::UIAlignment::Center;
    if (value == "End") return kb::scene::UIAlignment::End;
    if (value == "Stretch") return kb::scene::UIAlignment::Stretch;
    return std::nullopt;
}

std::optional<kb::scene::UIContainerLayoutMode> LayoutMode(std::string_view value) {
    if (value == "None") return kb::scene::UIContainerLayoutMode::None;
    if (value == "Overlay") return kb::scene::UIContainerLayoutMode::Overlay;
    if (value == "Horizontal") return kb::scene::UIContainerLayoutMode::Horizontal;
    if (value == "Vertical") return kb::scene::UIContainerLayoutMode::Vertical;
    if (value == "Grid") return kb::scene::UIContainerLayoutMode::Grid;
    if (value == "Wrap") return kb::scene::UIContainerLayoutMode::Wrap;
    return std::nullopt;
}

ScriptFunctionCallResult SetRect(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto components = ExistingComponents(context, arguments, error);
    if (!components) return Error(std::move(error));
    auto& rect = components->rect;
    SetFloat(arguments, "anchorMinX", rect.anchorMin.x);
    SetFloat(arguments, "anchorMinY", rect.anchorMin.y);
    SetFloat(arguments, "anchorMaxX", rect.anchorMax.x);
    SetFloat(arguments, "anchorMaxY", rect.anchorMax.y);
    SetFloat(arguments, "offsetMinX", rect.offsetMin.x);
    SetFloat(arguments, "offsetMinY", rect.offsetMin.y);
    SetFloat(arguments, "offsetMaxX", rect.offsetMax.x);
    SetFloat(arguments, "offsetMaxY", rect.offsetMax.y);
    SetFloat(arguments, "pivotX", rect.pivot.x);
    SetFloat(arguments, "pivotY", rect.pivot.y);
    SetFloat(arguments, "scaleX", rect.scale.x);
    SetFloat(arguments, "scaleY", rect.scale.y);
    SetFloat(arguments, "rotation", rect.rotationDegrees);
    if (const ScriptValue* zOrder = Arg(arguments, "zOrder"); zOrder != nullptr) rect.zOrder = zOrder->AsInt();
    return QueueComponents(context, arguments, std::move(*components));
}

ScriptFunctionCallResult SetCanvas(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto components = ExistingComponents(context, arguments, error);
    if (!components) return Error(std::move(error));
    kb::scene::UICanvas canvas = components->canvas.value_or(kb::scene::UICanvas{});
    if (const ScriptValue* mode = Arg(arguments, "scaleMode"); mode != nullptr) {
        if (mode->AsString() == "ConstantPixelSize") canvas.scaleMode = kb::scene::UICanvasScaleMode::ConstantPixelSize;
        else if (mode->AsString() == "ScaleWithScreenSize") canvas.scaleMode = kb::scene::UICanvasScaleMode::ScaleWithScreenSize;
        else return Error("UI.SetCanvas scaleMode must be ConstantPixelSize or ScaleWithScreenSize");
    }
    SetFloat(arguments, "referenceWidth", canvas.referenceResolution.x);
    SetFloat(arguments, "referenceHeight", canvas.referenceResolution.y);
    SetFloat(arguments, "scaleFactor", canvas.scaleFactor);
    SetFloat(arguments, "match", canvas.matchWidthOrHeight);
    components->canvas = canvas;
    return QueueComponents(context, arguments, std::move(*components));
}

ScriptFunctionCallResult SetLayout(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto components = ExistingComponents(context, arguments, error);
    if (!components) return Error(std::move(error));
    kb::scene::UIContainerLayout layout = components->layout.value_or(kb::scene::UIContainerLayout{});
    if (const ScriptValue* mode = Arg(arguments, "mode"); mode != nullptr) {
        const auto parsed = LayoutMode(mode->AsString());
        if (!parsed) return Error("UI.SetLayout mode must be None, Overlay, Horizontal, Vertical, Grid, or Wrap");
        layout.mode = *parsed;
    }
    SetFloat(arguments, "paddingLeft", layout.padding.left);
    SetFloat(arguments, "paddingTop", layout.padding.top);
    SetFloat(arguments, "paddingRight", layout.padding.right);
    SetFloat(arguments, "paddingBottom", layout.padding.bottom);
    SetFloat(arguments, "spacingX", layout.spacing.x);
    SetFloat(arguments, "spacingY", layout.spacing.y);
    SetFloat(arguments, "cellWidth", layout.cellSize.x);
    SetFloat(arguments, "cellHeight", layout.cellSize.y);
    if (const ScriptValue* columns = Arg(arguments, "columns"); columns != nullptr) layout.columns = columns->AsUInt32();
    if (const ScriptValue* alignment = Arg(arguments, "horizontalAlignment"); alignment != nullptr) {
        const auto parsed = Alignment(alignment->AsString());
        if (!parsed) return Error("UI.SetLayout horizontalAlignment is invalid");
        layout.horizontalAlignment = *parsed;
    }
    if (const ScriptValue* alignment = Arg(arguments, "verticalAlignment"); alignment != nullptr) {
        const auto parsed = Alignment(alignment->AsString());
        if (!parsed) return Error("UI.SetLayout verticalAlignment is invalid");
        layout.verticalAlignment = *parsed;
    }
    components->layout = layout;
    return QueueComponents(context, arguments, std::move(*components));
}

ScriptFunctionCallResult SetPaint(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto components = ExistingComponents(context, arguments, error);
    if (!components) return Error(std::move(error));
    kb::scene::UIPaint paint = components->paint.value_or(kb::scene::UIPaint{});
    SetFloat(arguments, "red", paint.backgroundColor.r);
    SetFloat(arguments, "green", paint.backgroundColor.g);
    SetFloat(arguments, "blue", paint.backgroundColor.b);
    SetFloat(arguments, "alpha", paint.backgroundColor.a);
    SetFloat(arguments, "borderRed", paint.borderColor.r);
    SetFloat(arguments, "borderGreen", paint.borderColor.g);
    SetFloat(arguments, "borderBlue", paint.borderColor.b);
    SetFloat(arguments, "borderAlpha", paint.borderColor.a);
    SetFloat(arguments, "borderLeft", paint.borderWidth.left);
    SetFloat(arguments, "borderTop", paint.borderWidth.top);
    SetFloat(arguments, "borderRight", paint.borderWidth.right);
    SetFloat(arguments, "borderBottom", paint.borderWidth.bottom);
    SetFloat(arguments, "radiusTopLeft", paint.cornerRadius.x);
    SetFloat(arguments, "radiusTopRight", paint.cornerRadius.y);
    SetFloat(arguments, "radiusBottomRight", paint.cornerRadius.z);
    SetFloat(arguments, "radiusBottomLeft", paint.cornerRadius.w);
    SetFloat(arguments, "opacity", paint.opacity);
    components->paint = paint;
    return QueueComponents(context, arguments, std::move(*components));
}

ScriptFunctionCallResult SetImageStyle(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto components = ExistingComponents(context, arguments, error);
    if (!components) return Error(std::move(error));
    kb::scene::UIImage image = components->image.value_or(kb::scene::UIImage{});
    SetHash(arguments, "image", image.imageAssetId);
    SetFloat(arguments, "uvX", image.uvRect.x);
    SetFloat(arguments, "uvY", image.uvRect.y);
    SetFloat(arguments, "uvWidth", image.uvRect.width);
    SetFloat(arguments, "uvHeight", image.uvRect.height);
    SetBool(arguments, "preserveAspect", image.preserveAspect);
    SetFloat(arguments, "sliceLeft", image.nineSlice.left);
    SetFloat(arguments, "sliceTop", image.nineSlice.top);
    SetFloat(arguments, "sliceRight", image.nineSlice.right);
    SetFloat(arguments, "sliceBottom", image.nineSlice.bottom);
    if (const ScriptValue* mode = Arg(arguments, "scaleMode"); mode != nullptr) {
        if (mode->AsString() == "Stretch") image.scaleMode = kb::scene::UIImageScaleMode::Stretch;
        else if (mode->AsString() == "Contain") image.scaleMode = kb::scene::UIImageScaleMode::Contain;
        else if (mode->AsString() == "Cover") image.scaleMode = kb::scene::UIImageScaleMode::Cover;
        else return Error("UI.SetImageStyle scaleMode must be Stretch, Contain, or Cover");
    }
    components->image = image;
    return QueueComponents(context, arguments, std::move(*components));
}

ScriptFunctionCallResult SetTextStyle(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto components = ExistingComponents(context, arguments, error);
    if (!components) return Error(std::move(error));
    kb::scene::UIText style = components->textStyle.value_or(kb::scene::UIText{});
    SetHash(arguments, "font", style.fontAssetId);
    SetFloat(arguments, "fontSize", style.fontSize);
    SetFloat(arguments, "red", style.color.r);
    SetFloat(arguments, "green", style.color.g);
    SetFloat(arguments, "blue", style.color.b);
    SetFloat(arguments, "alpha", style.color.a);
    if (const ScriptValue* alignment = Arg(arguments, "horizontalAlignment"); alignment != nullptr) {
        if (alignment->AsString() == "Left") style.horizontalAlignment = kb::scene::UITextHorizontalAlignment::Left;
        else if (alignment->AsString() == "Center") style.horizontalAlignment = kb::scene::UITextHorizontalAlignment::Center;
        else if (alignment->AsString() == "Right") style.horizontalAlignment = kb::scene::UITextHorizontalAlignment::Right;
        else return Error("UI.SetTextStyle horizontalAlignment is invalid");
    }
    if (const ScriptValue* alignment = Arg(arguments, "verticalAlignment"); alignment != nullptr) {
        if (alignment->AsString() == "Top") style.verticalAlignment = kb::scene::UITextVerticalAlignment::Top;
        else if (alignment->AsString() == "Center") style.verticalAlignment = kb::scene::UITextVerticalAlignment::Center;
        else if (alignment->AsString() == "Bottom") style.verticalAlignment = kb::scene::UITextVerticalAlignment::Bottom;
        else return Error("UI.SetTextStyle verticalAlignment is invalid");
    }
    if (const ScriptValue* wrap = Arg(arguments, "wrap"); wrap != nullptr) {
        if (wrap->AsString() == "NoWrap") style.wrapMode = kb::scene::UITextWrapMode::NoWrap;
        else if (wrap->AsString() == "Word") style.wrapMode = kb::scene::UITextWrapMode::Word;
        else if (wrap->AsString() == "Character") style.wrapMode = kb::scene::UITextWrapMode::Character;
        else return Error("UI.SetTextStyle wrap must be NoWrap, Word, or Character");
    }
    components->textStyle = style;
    return QueueComponents(context, arguments, std::move(*components));
}

ScriptFunctionCallResult SetInteraction(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto components = ExistingComponents(context, arguments, error);
    if (!components) return Error(std::move(error));
    kb::scene::UIInteraction interaction = components->interaction.value_or(kb::scene::UIInteraction{});
    SetBool(arguments, "raycastTarget", interaction.raycastTarget);
    SetBool(arguments, "interactable", interaction.interactable);
    SetHash(arguments, "navigationUp", interaction.navigationUp);
    SetHash(arguments, "navigationDown", interaction.navigationDown);
    SetHash(arguments, "navigationLeft", interaction.navigationLeft);
    SetHash(arguments, "navigationRight", interaction.navigationRight);
    if (const ScriptValue* mode = Arg(arguments, "navigationMode"); mode != nullptr) {
        if (mode->AsString() == "None") interaction.navigationMode = kb::scene::UINavigationMode::None;
        else if (mode->AsString() == "Automatic") interaction.navigationMode = kb::scene::UINavigationMode::Automatic;
        else if (mode->AsString() == "Explicit") interaction.navigationMode = kb::scene::UINavigationMode::Explicit;
        else return Error("UI.SetInteraction navigationMode must be None, Automatic, or Explicit");
    }
    if (const ScriptValue* eventName = Arg(arguments, "eventName"); eventName != nullptr) interaction.eventName = eventName->AsString();
    components->interaction = std::move(interaction);
    return QueueComponents(context, arguments, std::move(*components));
}

ScriptFunctionCallResult SetEffects(
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto components = ExistingComponents(context, arguments, error);
    if (!components) return Error(std::move(error));
    kb::scene::UIEffects effects = components->effects.value_or(kb::scene::UIEffects{});
    SetBool(arguments, "clipChildren", effects.clipChildren);
    SetBool(arguments, "mask", effects.mask);
    SetBool(arguments, "shadowEnabled", effects.shadowEnabled);
    SetFloat(arguments, "shadowX", effects.shadowOffset.x);
    SetFloat(arguments, "shadowY", effects.shadowOffset.y);
    SetFloat(arguments, "shadowRed", effects.shadowColor.r);
    SetFloat(arguments, "shadowGreen", effects.shadowColor.g);
    SetFloat(arguments, "shadowBlue", effects.shadowColor.b);
    SetFloat(arguments, "shadowAlpha", effects.shadowColor.a);
    SetFloat(arguments, "shadowBlur", effects.shadowBlur);
    SetBool(arguments, "outlineEnabled", effects.outlineEnabled);
    SetFloat(arguments, "outlineRed", effects.outlineColor.r);
    SetFloat(arguments, "outlineGreen", effects.outlineColor.g);
    SetFloat(arguments, "outlineBlue", effects.outlineColor.b);
    SetFloat(arguments, "outlineAlpha", effects.outlineColor.a);
    SetFloat(arguments, "outlineWidth", effects.outlineWidth);
    SetFloat(arguments, "backgroundBlur", effects.backgroundBlur);
    components->effects = effects;
    return QueueComponents(context, arguments, std::move(*components));
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
    kb::scene::UIElementComponents components{};
    if (const ScriptValue* image = Arg(arguments, "image"); image != nullptr) {
        components.image = kb::scene::UIImage{ .imageAssetId = image->AsUInt64() };
    }
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
        .components = std::move(components),
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

ScriptFunctionCallResult Focus(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("UI.Focus requires an active scene");
    return Applied(context.scene->UIDocuments().QueueFocus(Target(context, arguments), Arg(arguments, "element")->AsUInt64()),
        "UI.Focus requires a visible, focusable UI element and event queue capacity");
}

// Deliberately setup-only O(n) name scan. The result is a typed
// UIElementId handle (ScriptValueType::Hash) that callers retain and pass to
// the mutation/event APIs; no per-frame lookup cache is hidden in the API.
ScriptFunctionCallResult Find(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("UI.Find requires an active scene");
    const auto element = context.scene->UIDocuments().Find(Target(context, arguments), Arg(arguments, "name")->AsString());
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            { "element", ScriptValue{ element.value_or(0U), ScriptValueType::Hash } },
            { "found", ScriptValue{ element.has_value() } },
        },
    };
}

ScriptFunctionCallResult SetText(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->text = Arg(arguments, "text")->AsString();
    return QueueControl(context, arguments, std::move(*control), { kb::scene::UIControlKind::Text, kb::scene::UIControlKind::Button, kb::scene::UIControlKind::InputField, kb::scene::UIControlKind::Dropdown },
        "UI.SetText requires a Text, Button, InputField, or Dropdown element");
}

ScriptFunctionCallResult SetImage(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    if (control->kind != kb::scene::UIControlKind::Image) {
        return Error("UI.SetImage requires an Image element");
    }
    auto components = ExistingComponents(context, arguments, error);
    if (!components) return Error(std::move(error));
    kb::scene::UIImage image = components->image.value_or(kb::scene::UIImage{});
    image.imageAssetId = Arg(arguments, "image")->AsUInt64();
    components->image = image;
    return QueueComponents(context, arguments, std::move(*components));
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
    return QueueControl(context, arguments, std::move(*control), { kb::scene::UIControlKind::Slider, kb::scene::UIControlKind::ProgressBar, kb::scene::UIControlKind::Scrollbar },
        "UI.SetSlider requires a Slider, ProgressBar, or Scrollbar element with a valid range");
}

ScriptFunctionCallResult SetSelected(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->selectedIndex = Arg(arguments, "index")->AsUInt32();
    return QueueControl(context, arguments, std::move(*control),
        { kb::scene::UIControlKind::Dropdown, kb::scene::UIControlKind::WidgetSwitcher },
        "UI.SetSelected requires a Dropdown or WidgetSwitcher and an in-range index");
}

ScriptFunctionCallResult ListAppend(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->listItems.push_back(Arg(arguments, "item")->AsString());
    return QueueControl(context, arguments, std::move(*control),
        { kb::scene::UIControlKind::List, kb::scene::UIControlKind::Dropdown },
        "UI.ListAppend requires a List or Dropdown element and at most 4096 non-empty items");
}

ScriptFunctionCallResult ListClear(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    std::string error;
    auto control = ExistingControl(context, arguments, error);
    if (!control) return Error(std::move(error));
    control->listItems.clear();
    control->selectedIndex = 0U;
    return QueueControl(context, arguments, std::move(*control),
        { kb::scene::UIControlKind::List, kb::scene::UIControlKind::Dropdown },
        "UI.ListClear requires a List or Dropdown element");
}

ScriptFunctionCallResult ConfigureList(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("UI.ConfigureList requires an active scene");
    return Applied(context.scene->UIDocuments().QueueConfigureVirtualList(Target(context, arguments), Arg(arguments, "element")->AsUInt64(),
        Arg(arguments, "viewportItems")->AsUInt32(), Arg(arguments, "overscan")->AsUInt32()),
        "UI.ConfigureList requires a live List, viewportItems in 1..512, and overscan in 0..128");
}

ScriptFunctionCallResult ScrollListTo(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("UI.ListScrollTo requires an active scene");
    return Applied(context.scene->UIDocuments().QueueScrollVirtualListTo(Target(context, arguments), Arg(arguments, "element")->AsUInt64(),
        Arg(arguments, "firstVisibleIndex")->AsUInt32()),
        "UI.ListScrollTo requires a configured live virtual List");
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
        RegisterFunction(host, "UI.Focus", Targeted({ { "element", ScriptValueType::Hash, true } }), applied, &Focus) &&
        RegisterFunction(host, "UI.Find", Targeted({ { "name", ScriptValueType::String, true } }),
            { { "element", ScriptValueType::Hash, true }, { "found", ScriptValueType::Bool, true } }, &Find) &&
        RegisterFunction(host, "UI.SetText", Targeted({ { "element", ScriptValueType::Hash, true }, { "text", ScriptValueType::String, true } }), applied, &SetText) &&
        RegisterFunction(host, "UI.SetImage", Targeted({ { "element", ScriptValueType::Hash, true }, { "image", ScriptValueType::Hash, true } }), applied, &SetImage) &&
        RegisterFunction(host, "UI.SetToggle", Targeted({ { "element", ScriptValueType::Hash, true }, { "value", ScriptValueType::Bool, true } }), applied, &SetToggle) &&
        RegisterFunction(host, "UI.SetSlider", Targeted({ { "element", ScriptValueType::Hash, true }, { "value", ScriptValueType::Float, true }, { "minimum", ScriptValueType::Float, false }, { "maximum", ScriptValueType::Float, false } }), applied, &SetSlider) &&
        RegisterFunction(host, "UI.SetSelected", Targeted({ { "element", ScriptValueType::Hash, true }, { "index", ScriptValueType::UInt32, true } }), applied, &SetSelected) &&
        RegisterFunction(host, "UI.ListAppend", Targeted({ { "element", ScriptValueType::Hash, true }, { "item", ScriptValueType::String, true } }), applied, &ListAppend) &&
        RegisterFunction(host, "UI.ListClear", Targeted({ { "element", ScriptValueType::Hash, true } }), applied, &ListClear) &&
        RegisterFunction(host, "UI.ConfigureList", Targeted({ { "element", ScriptValueType::Hash, true }, { "viewportItems", ScriptValueType::UInt32, true }, { "overscan", ScriptValueType::UInt32, true } }), applied, &ConfigureList) &&
        RegisterFunction(host, "UI.ListScrollTo", Targeted({ { "element", ScriptValueType::Hash, true }, { "firstVisibleIndex", ScriptValueType::UInt32, true } }), applied, &ScrollListTo) &&
        RegisterFunction(host, "UI.SetScrollOffset", Targeted({ { "element", ScriptValueType::Hash, true }, { "offset", ScriptValueType::Float, true } }), applied, &SetScrollOffset) &&
        RegisterFunction(host, "UI.SetModalOpen", Targeted({ { "element", ScriptValueType::Hash, true }, { "open", ScriptValueType::Bool, true } }), applied, &SetModalOpen) &&
        RegisterFunction(host, "UI.SetRect", Targeted({
            { "element", ScriptValueType::Hash, true },
            { "anchorMinX", ScriptValueType::Float, false }, { "anchorMinY", ScriptValueType::Float, false },
            { "anchorMaxX", ScriptValueType::Float, false }, { "anchorMaxY", ScriptValueType::Float, false },
            { "offsetMinX", ScriptValueType::Float, false }, { "offsetMinY", ScriptValueType::Float, false },
            { "offsetMaxX", ScriptValueType::Float, false }, { "offsetMaxY", ScriptValueType::Float, false },
            { "pivotX", ScriptValueType::Float, false }, { "pivotY", ScriptValueType::Float, false },
            { "scaleX", ScriptValueType::Float, false }, { "scaleY", ScriptValueType::Float, false },
            { "rotation", ScriptValueType::Float, false }, { "zOrder", ScriptValueType::Int, false },
        }), applied, &SetRect) &&
        RegisterFunction(host, "UI.SetCanvas", Targeted({
            { "element", ScriptValueType::Hash, true }, { "scaleMode", ScriptValueType::String, false },
            { "referenceWidth", ScriptValueType::Float, false }, { "referenceHeight", ScriptValueType::Float, false },
            { "scaleFactor", ScriptValueType::Float, false }, { "match", ScriptValueType::Float, false },
        }), applied, &SetCanvas) &&
        RegisterFunction(host, "UI.SetLayout", Targeted({
            { "element", ScriptValueType::Hash, true }, { "mode", ScriptValueType::String, false },
            { "paddingLeft", ScriptValueType::Float, false }, { "paddingTop", ScriptValueType::Float, false },
            { "paddingRight", ScriptValueType::Float, false }, { "paddingBottom", ScriptValueType::Float, false },
            { "spacingX", ScriptValueType::Float, false }, { "spacingY", ScriptValueType::Float, false },
            { "horizontalAlignment", ScriptValueType::String, false }, { "verticalAlignment", ScriptValueType::String, false },
            { "cellWidth", ScriptValueType::Float, false }, { "cellHeight", ScriptValueType::Float, false },
            { "columns", ScriptValueType::UInt32, false },
        }), applied, &SetLayout) &&
        RegisterFunction(host, "UI.SetPaint", Targeted({
            { "element", ScriptValueType::Hash, true },
            { "red", ScriptValueType::Float, false }, { "green", ScriptValueType::Float, false },
            { "blue", ScriptValueType::Float, false }, { "alpha", ScriptValueType::Float, false },
            { "borderRed", ScriptValueType::Float, false }, { "borderGreen", ScriptValueType::Float, false },
            { "borderBlue", ScriptValueType::Float, false }, { "borderAlpha", ScriptValueType::Float, false },
            { "borderLeft", ScriptValueType::Float, false }, { "borderTop", ScriptValueType::Float, false },
            { "borderRight", ScriptValueType::Float, false }, { "borderBottom", ScriptValueType::Float, false },
            { "radiusTopLeft", ScriptValueType::Float, false }, { "radiusTopRight", ScriptValueType::Float, false },
            { "radiusBottomRight", ScriptValueType::Float, false }, { "radiusBottomLeft", ScriptValueType::Float, false },
            { "opacity", ScriptValueType::Float, false },
        }), applied, &SetPaint) &&
        RegisterFunction(host, "UI.SetImageStyle", Targeted({
            { "element", ScriptValueType::Hash, true }, { "image", ScriptValueType::Hash, false },
            { "uvX", ScriptValueType::Float, false }, { "uvY", ScriptValueType::Float, false },
            { "uvWidth", ScriptValueType::Float, false }, { "uvHeight", ScriptValueType::Float, false },
            { "scaleMode", ScriptValueType::String, false }, { "preserveAspect", ScriptValueType::Bool, false },
            { "sliceLeft", ScriptValueType::Float, false }, { "sliceTop", ScriptValueType::Float, false },
            { "sliceRight", ScriptValueType::Float, false }, { "sliceBottom", ScriptValueType::Float, false },
        }), applied, &SetImageStyle) &&
        RegisterFunction(host, "UI.SetTextStyle", Targeted({
            { "element", ScriptValueType::Hash, true }, { "font", ScriptValueType::Hash, false },
            { "fontSize", ScriptValueType::Float, false }, { "red", ScriptValueType::Float, false },
            { "green", ScriptValueType::Float, false }, { "blue", ScriptValueType::Float, false },
            { "alpha", ScriptValueType::Float, false }, { "horizontalAlignment", ScriptValueType::String, false },
            { "verticalAlignment", ScriptValueType::String, false }, { "wrap", ScriptValueType::String, false },
        }), applied, &SetTextStyle) &&
        RegisterFunction(host, "UI.SetInteraction", Targeted({
            { "element", ScriptValueType::Hash, true }, { "raycastTarget", ScriptValueType::Bool, false },
            { "interactable", ScriptValueType::Bool, false }, { "navigationMode", ScriptValueType::String, false },
            { "navigationUp", ScriptValueType::Hash, false }, { "navigationDown", ScriptValueType::Hash, false },
            { "navigationLeft", ScriptValueType::Hash, false }, { "navigationRight", ScriptValueType::Hash, false },
            { "eventName", ScriptValueType::String, false },
        }), applied, &SetInteraction) &&
        RegisterFunction(host, "UI.SetEffects", Targeted({
            { "element", ScriptValueType::Hash, true }, { "clipChildren", ScriptValueType::Bool, false },
            { "mask", ScriptValueType::Bool, false }, { "shadowEnabled", ScriptValueType::Bool, false },
            { "shadowX", ScriptValueType::Float, false }, { "shadowY", ScriptValueType::Float, false },
            { "shadowRed", ScriptValueType::Float, false }, { "shadowGreen", ScriptValueType::Float, false },
            { "shadowBlue", ScriptValueType::Float, false }, { "shadowAlpha", ScriptValueType::Float, false },
            { "shadowBlur", ScriptValueType::Float, false }, { "outlineEnabled", ScriptValueType::Bool, false },
            { "outlineRed", ScriptValueType::Float, false }, { "outlineGreen", ScriptValueType::Float, false },
            { "outlineBlue", ScriptValueType::Float, false }, { "outlineAlpha", ScriptValueType::Float, false },
            { "outlineWidth", ScriptValueType::Float, false }, { "backgroundBlur", ScriptValueType::Float, false },
        }), applied, &SetEffects) &&
        RegisterFunction(host, "UI.EmitClick", Targeted({ { "element", ScriptValueType::Hash, true }, { "x", ScriptValueType::Float, true }, { "y", ScriptValueType::Float, true } }), applied, &EmitClick) &&
        RegisterFunction(host, "UI.EmitPointer", Targeted({ { "element", ScriptValueType::Hash, true }, { "x", ScriptValueType::Float, true }, { "y", ScriptValueType::Float, true } }), applied, &EmitPointer) &&
        RegisterFunction(host, "UI.EmitSubmit", Targeted({ { "element", ScriptValueType::Hash, true }, { "text", ScriptValueType::String, true } }), applied, &EmitSubmit) &&
        RegisterFunction(host, "UI.EmitChanged", Targeted({ { "element", ScriptValueType::Hash, true }, { "value", ScriptValueType::Float, true } }), applied, &EmitChanged) &&
        RegisterFunction(host, "UI.EmitFocus", Targeted({ { "element", ScriptValueType::Hash, true }, { "focused", ScriptValueType::Bool, true } }), applied, &EmitFocus) &&
        RegisterFunction(host, "UI.EmitNavigation", Targeted({ { "element", ScriptValueType::Hash, true }, { "direction", ScriptValueType::String, true } }), applied, &EmitNavigation);
}

} // namespace kb::script
