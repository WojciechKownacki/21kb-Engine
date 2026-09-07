#include "scene/user_widget/UserWidgetEditorProperty.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <utility>

namespace kb::editor {
namespace {

template <typename... Values> [[nodiscard]] std::string Fields(const Values&... values) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::boolalpha << std::setprecision(7);
    std::size_t index = 0U;
    ((output << (index++ == 0U ? "" : " ") << values), ...);
    return output.str();
}

template <typename... Values> [[nodiscard]] bool ParseFields(std::string_view text, Values&... values) {
    std::istringstream input{std::string{text}};
    input.imbue(std::locale::classic());
    if (!((input >> values) && ...))
        return false;
    input >> std::ws;
    return input.eof();
}

[[nodiscard]] bool ParseBool(std::string_view text, bool& value) {
    if (text == "true") {
        value = true;
        return true;
    }
    if (text == "false") {
        value = false;
        return true;
    }
    return false;
}

template <typename Enum>
[[nodiscard]] bool EnumValue(std::uint32_t value, std::uint32_t maximum, Enum& output) noexcept {
    if (value > maximum)
        return false;
    output = static_cast<Enum>(value);
    return true;
}

template <typename... Values> [[nodiscard]] bool Finite(const Values&... values) noexcept {
    return (... && std::isfinite(static_cast<double>(values)));
}

[[nodiscard]] UserWidgetEditorPropertyRow Row(UserWidgetEditorProperty property, std::string label, std::string value,
                                              std::string hint) {
    return UserWidgetEditorPropertyRow{
        .property = property,
        .label = std::move(label),
        .value = std::move(value),
        .hint = std::move(hint),
    };
}

} // namespace

std::vector<UserWidgetEditorPropertyRow>
UserWidgetEditorPropertyAdapter::Rows(const kb::scene::UIDocumentElement& element) {
    std::vector<UserWidgetEditorPropertyRow> rows;
    rows.reserve(25U);
    rows.push_back(Row(UserWidgetEditorProperty::Name, "Name", element.name, "Non-empty element name"));
    rows.push_back(
        Row(UserWidgetEditorProperty::Visible, "Visible", element.visible ? "true" : "false", "true or false"));
    rows.push_back(Row(
        UserWidgetEditorProperty::RectAnchors, "Anchors",
        Fields(element.rect.anchorMin.x, element.rect.anchorMin.y, element.rect.anchorMax.x, element.rect.anchorMax.y),
        "minX minY maxX maxY"));
    rows.push_back(Row(
        UserWidgetEditorProperty::RectOffsets, "Offsets",
        Fields(element.rect.offsetMin.x, element.rect.offsetMin.y, element.rect.offsetMax.x, element.rect.offsetMax.y),
        "minX minY maxX maxY"));
    rows.push_back(
        Row(UserWidgetEditorProperty::RectPivot, "Pivot", Fields(element.rect.pivot.x, element.rect.pivot.y), "x y"));
    rows.push_back(
        Row(UserWidgetEditorProperty::RectScale, "Scale", Fields(element.rect.scale.x, element.rect.scale.y), "x y"));
    rows.push_back(
        Row(UserWidgetEditorProperty::RectRotation, "Rotation", Fields(element.rect.rotationDegrees), "degrees"));
    rows.push_back(Row(UserWidgetEditorProperty::RectZOrder, "Z order", Fields(element.rect.zOrder), "integer"));

    if (element.canvas) {
        rows.push_back(Row(UserWidgetEditorProperty::Canvas, "Canvas",
                           Fields(static_cast<std::uint32_t>(element.canvas->scaleMode),
                                  element.canvas->referenceResolution.x, element.canvas->referenceResolution.y,
                                  element.canvas->scaleFactor, element.canvas->matchWidthOrHeight),
                           "scaleMode(0..1) width height scale match"));
    }
    if (element.layout) {
        rows.push_back(Row(UserWidgetEditorProperty::Layout, "Layout",
                           Fields(static_cast<std::uint32_t>(element.layout->mode), element.layout->padding.left,
                                  element.layout->padding.top, element.layout->padding.right,
                                  element.layout->padding.bottom, element.layout->spacing.x, element.layout->spacing.y,
                                  static_cast<std::uint32_t>(element.layout->horizontalAlignment),
                                  static_cast<std::uint32_t>(element.layout->verticalAlignment),
                                  element.layout->cellSize.x, element.layout->cellSize.y, element.layout->columns),
                           "mode paddingLTRB spacingXY alignH alignV cellXY columns"));
    } else {
        rows.push_back(Row(UserWidgetEditorProperty::Layout, "Layout", "none",
                           "mode paddingLTRB spacingXY alignH alignV cellXY columns"));
    }

    if (element.paint) {
        rows.push_back(Row(UserWidgetEditorProperty::Paint, "Paint",
                           Fields(element.paint->borderWidth.left, element.paint->borderWidth.top,
                                  element.paint->borderWidth.right, element.paint->borderWidth.bottom,
                                  element.paint->cornerRadius.x, element.paint->cornerRadius.y,
                                  element.paint->cornerRadius.z, element.paint->cornerRadius.w, element.paint->opacity),
                           "borderLTRB radiusTLTRBRBL opacity; none removes"));
        rows.push_back(Row(UserWidgetEditorProperty::PaintBackgroundColor, "Background color",
                           Fields(element.paint->backgroundColor.r, element.paint->backgroundColor.g,
                                  element.paint->backgroundColor.b, element.paint->backgroundColor.a),
                           "Click to open the color picker"));
        rows.push_back(Row(UserWidgetEditorProperty::PaintBorderColor, "Border color",
                           Fields(element.paint->borderColor.r, element.paint->borderColor.g,
                                  element.paint->borderColor.b, element.paint->borderColor.a),
                           "Click to open the color picker"));
    } else {
        rows.push_back(Row(UserWidgetEditorProperty::Paint, "Paint", "none", "borderLTRB radiusTLTRBRBL opacity"));
    }
    if (element.image) {
        rows.push_back(Row(UserWidgetEditorProperty::Image, "Image asset / UV",
                           Fields(element.image->imageAssetId, element.image->uvRect.x, element.image->uvRect.y,
                                  element.image->uvRect.width, element.image->uvRect.height,
                                  static_cast<std::uint32_t>(element.image->scaleMode), element.image->preserveAspect,
                                  element.image->nineSlice.left, element.image->nineSlice.top,
                                  element.image->nineSlice.right, element.image->nineSlice.bottom),
                           "assetId uvXYWH mode(0..2) preserve nineSliceLTRB"));
    } else {
        rows.push_back(Row(UserWidgetEditorProperty::Image, "Image asset / UV", "none",
                           "assetId uvXYWH mode(0..2) preserve nineSliceLTRB"));
    }
    rows.push_back(Row(UserWidgetEditorProperty::Text, "Text", element.control.text, "Displayed text"));
    if (element.textStyle) {
        rows.push_back(Row(UserWidgetEditorProperty::TextStyle, "Font asset / style",
                           Fields(element.textStyle->fontAssetId, element.textStyle->fontSize,
                                  static_cast<std::uint32_t>(element.textStyle->horizontalAlignment),
                                  static_cast<std::uint32_t>(element.textStyle->verticalAlignment),
                                  static_cast<std::uint32_t>(element.textStyle->wrapMode)),
                           "fontId size horizontal(0..2) vertical(0..2) wrap(0..2)"));
        rows.push_back(Row(UserWidgetEditorProperty::TextColor, "Text color",
                           Fields(element.textStyle->color.r, element.textStyle->color.g, element.textStyle->color.b,
                                  element.textStyle->color.a),
                           "Click to open the color picker"));
    } else {
        rows.push_back(Row(UserWidgetEditorProperty::TextStyle, "Font asset / style", "none",
                           "fontId size horizontal(0..2) vertical(0..2) wrap(0..2)"));
    }
    if (element.interaction) {
        rows.push_back(Row(UserWidgetEditorProperty::Interaction, "Interaction",
                           Fields(element.interaction->raycastTarget, element.interaction->interactable,
                                  static_cast<std::uint32_t>(element.interaction->navigationMode),
                                  element.interaction->navigationUp, element.interaction->navigationDown,
                                  element.interaction->navigationLeft, element.interaction->navigationRight),
                           "raycast interactable navigation(0..2) up down left right"));
        rows.push_back(Row(UserWidgetEditorProperty::InteractionAction, "Lua action", element.interaction->eventName,
                           "Lua event/action name"));
    } else {
        rows.push_back(Row(UserWidgetEditorProperty::Interaction, "Interaction", "none",
                           "raycast interactable navigation(0..2) up down left right"));
    }
    if (element.effects) {
        rows.push_back(
            Row(UserWidgetEditorProperty::Effects, "Effects",
                Fields(element.effects->clipChildren, element.effects->mask, element.effects->shadowEnabled,
                       element.effects->shadowOffset.x, element.effects->shadowOffset.y, element.effects->shadowBlur,
                       element.effects->outlineEnabled, element.effects->outlineWidth, element.effects->backgroundBlur),
                "clip mask shadow offsetXY blur outline width backgroundBlur; none removes"));
        rows.push_back(Row(UserWidgetEditorProperty::EffectsShadowColor, "Shadow color",
                           Fields(element.effects->shadowColor.r, element.effects->shadowColor.g,
                                  element.effects->shadowColor.b, element.effects->shadowColor.a),
                           "Click to open the color picker"));
        rows.push_back(Row(UserWidgetEditorProperty::EffectsOutlineColor, "Outline color",
                           Fields(element.effects->outlineColor.r, element.effects->outlineColor.g,
                                  element.effects->outlineColor.b, element.effects->outlineColor.a),
                           "Click to open the color picker"));
    } else {
        rows.push_back(Row(UserWidgetEditorProperty::Effects, "Effects", "none",
                           "clip mask shadow offsetXY blur outline width backgroundBlur"));
    }
    rows.push_back(Row(UserWidgetEditorProperty::ControlState, "Control state",
                       Fields(element.control.toggleValue, element.control.sliderValue, element.control.sliderMinimum,
                              element.control.sliderMaximum, element.control.selectedIndex,
                              element.control.scrollOffset, element.control.modalOpen),
                       "toggle value minimum maximum selectedIndex scrollOffset modalOpen"));
    if (element.control.kind == kb::scene::UIControlKind::List ||
        element.control.kind == kb::scene::UIControlKind::Dropdown) {
        std::string items;
        for (const std::string& item : element.control.listItems) {
            if (!items.empty())
                items.push_back('|');
            items += item;
        }
        rows.push_back(
            Row(UserWidgetEditorProperty::ListItems, "List items", std::move(items), "Items separated by |"));
    }
    return rows;
}

bool UserWidgetEditorPropertyAdapter::Apply(UserWidgetEditorProperty property, std::string_view value,
                                            kb::scene::UIDocumentElement& element) {
    switch (property) {
    case UserWidgetEditorProperty::Name:
        if (value.empty())
            return false;
        element.name.assign(value);
        return true;
    case UserWidgetEditorProperty::Visible:
        return ParseBool(value, element.visible);
    case UserWidgetEditorProperty::RectAnchors:
        return ParseFields(value, element.rect.anchorMin.x, element.rect.anchorMin.y, element.rect.anchorMax.x,
                           element.rect.anchorMax.y) &&
               Finite(element.rect.anchorMin.x, element.rect.anchorMin.y, element.rect.anchorMax.x,
                      element.rect.anchorMax.y);
    case UserWidgetEditorProperty::RectOffsets:
        return ParseFields(value, element.rect.offsetMin.x, element.rect.offsetMin.y, element.rect.offsetMax.x,
                           element.rect.offsetMax.y) &&
               Finite(element.rect.offsetMin.x, element.rect.offsetMin.y, element.rect.offsetMax.x,
                      element.rect.offsetMax.y);
    case UserWidgetEditorProperty::RectPivot:
        return ParseFields(value, element.rect.pivot.x, element.rect.pivot.y) &&
               Finite(element.rect.pivot.x, element.rect.pivot.y);
    case UserWidgetEditorProperty::RectScale:
        return ParseFields(value, element.rect.scale.x, element.rect.scale.y) &&
               Finite(element.rect.scale.x, element.rect.scale.y);
    case UserWidgetEditorProperty::RectRotation:
        return ParseFields(value, element.rect.rotationDegrees) && Finite(element.rect.rotationDegrees);
    case UserWidgetEditorProperty::RectZOrder:
        return ParseFields(value, element.rect.zOrder);
    case UserWidgetEditorProperty::Canvas: {
        if (!element.canvas)
            return false;
        std::uint32_t scaleMode = 0U;
        kb::scene::UICanvas canvas = *element.canvas;
        if (!ParseFields(value, scaleMode, canvas.referenceResolution.x, canvas.referenceResolution.y,
                         canvas.scaleFactor, canvas.matchWidthOrHeight) ||
            !EnumValue(scaleMode, 1U, canvas.scaleMode) ||
            !Finite(canvas.referenceResolution.x, canvas.referenceResolution.y, canvas.scaleFactor,
                    canvas.matchWidthOrHeight)) {
            return false;
        }
        element.canvas = canvas;
        return true;
    }
    case UserWidgetEditorProperty::Layout: {
        if (value == "none") {
            element.layout.reset();
            return true;
        }
        std::uint32_t mode = 0U;
        std::uint32_t horizontal = 0U;
        std::uint32_t vertical = 0U;
        kb::scene::UIContainerLayout layout = element.layout.value_or(kb::scene::UIContainerLayout{});
        if (!ParseFields(value, mode, layout.padding.left, layout.padding.top, layout.padding.right,
                         layout.padding.bottom, layout.spacing.x, layout.spacing.y, horizontal, vertical,
                         layout.cellSize.x, layout.cellSize.y, layout.columns) ||
            !EnumValue(mode, 5U, layout.mode) || !EnumValue(horizontal, 3U, layout.horizontalAlignment) ||
            !EnumValue(vertical, 3U, layout.verticalAlignment) ||
            !Finite(layout.padding.left, layout.padding.top, layout.padding.right, layout.padding.bottom,
                    layout.spacing.x, layout.spacing.y, layout.cellSize.x, layout.cellSize.y)) {
            return false;
        }
        element.layout = layout;
        return true;
    }
    case UserWidgetEditorProperty::Paint: {
        if (value == "none") {
            element.paint.reset();
            return true;
        }
        kb::scene::UIPaint paint = element.paint.value_or(kb::scene::UIPaint{});
        if (!ParseFields(value, paint.borderWidth.left, paint.borderWidth.top, paint.borderWidth.right,
                         paint.borderWidth.bottom, paint.cornerRadius.x, paint.cornerRadius.y, paint.cornerRadius.z,
                         paint.cornerRadius.w, paint.opacity) ||
            !Finite(paint.borderWidth.left, paint.borderWidth.top, paint.borderWidth.right, paint.borderWidth.bottom,
                    paint.cornerRadius.x, paint.cornerRadius.y, paint.cornerRadius.z, paint.cornerRadius.w,
                    paint.opacity)) {
            return false;
        }
        element.paint = paint;
        return true;
    }
    case UserWidgetEditorProperty::PaintBackgroundColor:
    case UserWidgetEditorProperty::PaintBorderColor:
    case UserWidgetEditorProperty::TextColor:
    case UserWidgetEditorProperty::EffectsShadowColor:
    case UserWidgetEditorProperty::EffectsOutlineColor: {
        std::array<float, 4U> color{};
        return ParseFields(value, color[0U], color[1U], color[2U], color[3U]) && ApplyColor(property, color, element);
    }
    case UserWidgetEditorProperty::Image: {
        if (value == "none") {
            element.image.reset();
            return true;
        }
        std::uint32_t mode = 0U;
        std::string preserve;
        kb::scene::UIImage image = element.image.value_or(kb::scene::UIImage{});
        if (!ParseFields(value, image.imageAssetId, image.uvRect.x, image.uvRect.y, image.uvRect.width,
                         image.uvRect.height, mode, preserve, image.nineSlice.left, image.nineSlice.top,
                         image.nineSlice.right, image.nineSlice.bottom) ||
            !ParseBool(preserve, image.preserveAspect) || !EnumValue(mode, 2U, image.scaleMode) ||
            !Finite(image.uvRect.x, image.uvRect.y, image.uvRect.width, image.uvRect.height, image.nineSlice.left,
                    image.nineSlice.top, image.nineSlice.right, image.nineSlice.bottom)) {
            return false;
        }
        element.image = image;
        return true;
    }
    case UserWidgetEditorProperty::Text:
        element.control.text.assign(value);
        return true;
    case UserWidgetEditorProperty::TextStyle: {
        if (value == "none") {
            element.textStyle.reset();
            return true;
        }
        std::uint32_t horizontal = 0U;
        std::uint32_t vertical = 0U;
        std::uint32_t wrap = 0U;
        kb::scene::UIText style = element.textStyle.value_or(kb::scene::UIText{});
        if (!ParseFields(value, style.fontAssetId, style.fontSize, horizontal, vertical, wrap) ||
            !EnumValue(horizontal, 2U, style.horizontalAlignment) ||
            !EnumValue(vertical, 2U, style.verticalAlignment) || !EnumValue(wrap, 2U, style.wrapMode) ||
            !Finite(style.fontSize)) {
            return false;
        }
        element.textStyle = style;
        return true;
    }
    case UserWidgetEditorProperty::Interaction: {
        if (value == "none") {
            element.interaction.reset();
            return true;
        }
        std::string raycast;
        std::string interactable;
        std::uint32_t navigation = 0U;
        kb::scene::UIInteraction interaction = element.interaction.value_or(kb::scene::UIInteraction{});
        if (!ParseFields(value, raycast, interactable, navigation, interaction.navigationUp, interaction.navigationDown,
                         interaction.navigationLeft, interaction.navigationRight) ||
            !ParseBool(raycast, interaction.raycastTarget) || !ParseBool(interactable, interaction.interactable) ||
            !EnumValue(navigation, 2U, interaction.navigationMode)) {
            return false;
        }
        element.interaction = std::move(interaction);
        return true;
    }
    case UserWidgetEditorProperty::InteractionAction:
        if (!element.interaction)
            return false;
        element.interaction->eventName.assign(value);
        return true;
    case UserWidgetEditorProperty::Effects: {
        if (value == "none") {
            element.effects.reset();
            return true;
        }
        std::string clip;
        std::string mask;
        std::string shadow;
        std::string outline;
        kb::scene::UIEffects effects = element.effects.value_or(kb::scene::UIEffects{});
        if (!ParseFields(value, clip, mask, shadow, effects.shadowOffset.x, effects.shadowOffset.y, effects.shadowBlur,
                         outline, effects.outlineWidth, effects.backgroundBlur) ||
            !ParseBool(clip, effects.clipChildren) || !ParseBool(mask, effects.mask) ||
            !ParseBool(shadow, effects.shadowEnabled) || !ParseBool(outline, effects.outlineEnabled) ||
            !Finite(effects.shadowOffset.x, effects.shadowOffset.y, effects.shadowBlur, effects.outlineWidth,
                    effects.backgroundBlur)) {
            return false;
        }
        element.effects = effects;
        return true;
    }
    case UserWidgetEditorProperty::ControlState: {
        std::string toggle;
        std::string modal;
        kb::scene::UIControlState control = element.control;
        if (!ParseFields(value, toggle, control.sliderValue, control.sliderMinimum, control.sliderMaximum,
                         control.selectedIndex, control.scrollOffset, modal) ||
            !ParseBool(toggle, control.toggleValue) || !ParseBool(modal, control.modalOpen) ||
            !Finite(control.sliderValue, control.sliderMinimum, control.sliderMaximum, control.scrollOffset)) {
            return false;
        }
        element.control = std::move(control);
        return true;
    }
    case UserWidgetEditorProperty::ListItems: {
        if (element.control.kind != kb::scene::UIControlKind::List &&
            element.control.kind != kb::scene::UIControlKind::Dropdown) {
            return false;
        }
        element.control.listItems.clear();
        while (!value.empty()) {
            const std::size_t separator = value.find('|');
            element.control.listItems.emplace_back(value.substr(0U, separator));
            if (separator == std::string_view::npos)
                break;
            value.remove_prefix(separator + 1U);
        }
        return true;
    }
    }
    return false;
}

std::optional<std::array<float, 4U>>
UserWidgetEditorPropertyAdapter::Color(UserWidgetEditorProperty property,
                                       const kb::scene::UIDocumentElement& element) noexcept {
    const kb::math::Color* color = nullptr;
    switch (property) {
    case UserWidgetEditorProperty::PaintBackgroundColor:
        if (element.paint)
            color = &element.paint->backgroundColor;
        break;
    case UserWidgetEditorProperty::PaintBorderColor:
        if (element.paint)
            color = &element.paint->borderColor;
        break;
    case UserWidgetEditorProperty::TextColor:
        if (element.textStyle)
            color = &element.textStyle->color;
        break;
    case UserWidgetEditorProperty::EffectsShadowColor:
        if (element.effects)
            color = &element.effects->shadowColor;
        break;
    case UserWidgetEditorProperty::EffectsOutlineColor:
        if (element.effects)
            color = &element.effects->outlineColor;
        break;
    default:
        break;
    }
    if (color == nullptr)
        return std::nullopt;
    return std::array<float, 4U>{color->r, color->g, color->b, color->a};
}

bool UserWidgetEditorPropertyAdapter::ApplyColor(UserWidgetEditorProperty property, const std::array<float, 4U>& color,
                                                 kb::scene::UIDocumentElement& element) noexcept {
    for (const float channel : color) {
        if (!std::isfinite(channel) || channel < 0.0F || channel > 1.0F) {
            return false;
        }
    }
    kb::math::Color* target = nullptr;
    switch (property) {
    case UserWidgetEditorProperty::PaintBackgroundColor:
        if (element.paint)
            target = &element.paint->backgroundColor;
        break;
    case UserWidgetEditorProperty::PaintBorderColor:
        if (element.paint)
            target = &element.paint->borderColor;
        break;
    case UserWidgetEditorProperty::TextColor:
        if (element.textStyle)
            target = &element.textStyle->color;
        break;
    case UserWidgetEditorProperty::EffectsShadowColor:
        if (element.effects)
            target = &element.effects->shadowColor;
        break;
    case UserWidgetEditorProperty::EffectsOutlineColor:
        if (element.effects)
            target = &element.effects->outlineColor;
        break;
    default:
        break;
    }
    if (target == nullptr)
        return false;
    *target = {color[0U], color[1U], color[2U], color[3U]};
    return true;
}

} // namespace kb::editor
