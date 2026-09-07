#include "engine/ui/UIAssetValidation.hpp"

#include <cmath>
#include <cstdint>
#include <optional>

namespace kb::scene {
namespace {

bool Finite(float value) noexcept {
    return std::isfinite(value);
}
bool Unit(float value) noexcept {
    return Finite(value) && value >= 0.0F && value <= 1.0F;
}
bool NonNegative(float value) noexcept {
    return Finite(value) && value >= 0.0F;
}
bool Positive(float value) noexcept {
    return Finite(value) && value > 0.0F;
}

bool ValidVec2(kb::math::Vec2 value) noexcept {
    return Finite(value.x) && Finite(value.y);
}

bool ValidColor(kb::math::Color value) noexcept {
    return Unit(value.r) && Unit(value.g) && Unit(value.b) && Unit(value.a);
}

bool ValidEdges(const UIEdges& value) noexcept {
    return NonNegative(value.left) && NonNegative(value.top) && NonNegative(value.right) && NonNegative(value.bottom);
}

bool IsTextKind(UIControlKind kind) noexcept {
    return kind == UIControlKind::Text || kind == UIControlKind::Button || kind == UIControlKind::InputField ||
           kind == UIControlKind::Dropdown;
}

std::optional<UIContainerLayoutMode> RequiredLayoutMode(UIControlKind kind) noexcept {
    switch (kind) {
    case UIControlKind::Overlay:
        return UIContainerLayoutMode::Overlay;
    case UIControlKind::HorizontalBox:
        return UIContainerLayoutMode::Horizontal;
    case UIControlKind::VerticalBox:
        return UIContainerLayoutMode::Vertical;
    case UIControlKind::Grid:
        return UIContainerLayoutMode::Grid;
    case UIControlKind::Wrap:
        return UIContainerLayoutMode::Wrap;
    default:
        return std::nullopt;
    }
}

} // namespace

bool UIAssetValidation::RectTransform(const UIRectTransform& value) noexcept {
    return Unit(value.anchorMin.x) && Unit(value.anchorMin.y) && Unit(value.anchorMax.x) && Unit(value.anchorMax.y) &&
           value.anchorMin.x <= value.anchorMax.x && value.anchorMin.y <= value.anchorMax.y &&
           ValidVec2(value.offsetMin) && ValidVec2(value.offsetMax) && Unit(value.pivot.x) && Unit(value.pivot.y) &&
           Positive(value.scale.x) && Positive(value.scale.y) && Finite(value.rotationDegrees);
}

bool UIAssetValidation::Canvas(const UICanvas& value) noexcept {
    return static_cast<std::uint8_t>(value.scaleMode) <=
               static_cast<std::uint8_t>(UICanvasScaleMode::ScaleWithScreenSize) &&
           Positive(value.referenceResolution.x) && Positive(value.referenceResolution.y) &&
           Positive(value.scaleFactor) && Unit(value.matchWidthOrHeight);
}

bool UIAssetValidation::ContainerLayout(const UIContainerLayout& value) noexcept {
    return static_cast<std::uint8_t>(value.mode) <= static_cast<std::uint8_t>(UIContainerLayoutMode::Wrap) &&
           static_cast<std::uint8_t>(value.horizontalAlignment) <= static_cast<std::uint8_t>(UIAlignment::Stretch) &&
           static_cast<std::uint8_t>(value.verticalAlignment) <= static_cast<std::uint8_t>(UIAlignment::Stretch) &&
           ValidEdges(value.padding) && NonNegative(value.spacing.x) && NonNegative(value.spacing.y) &&
           Positive(value.cellSize.x) && Positive(value.cellSize.y) && value.columns > 0U;
}

bool UIAssetValidation::Paint(const UIPaint& value) noexcept {
    return ValidColor(value.backgroundColor) && ValidColor(value.borderColor) && ValidEdges(value.borderWidth) &&
           NonNegative(value.cornerRadius.x) && NonNegative(value.cornerRadius.y) &&
           NonNegative(value.cornerRadius.z) && NonNegative(value.cornerRadius.w) && Unit(value.opacity);
}

bool UIAssetValidation::Image(const UIImage& value) noexcept {
    return Finite(value.uvRect.x) && Finite(value.uvRect.y) && Positive(value.uvRect.width) &&
           Positive(value.uvRect.height) && value.uvRect.x >= 0.0F && value.uvRect.y >= 0.0F &&
           value.uvRect.x + value.uvRect.width <= 1.0F && value.uvRect.y + value.uvRect.height <= 1.0F &&
           static_cast<std::uint8_t>(value.scaleMode) <= static_cast<std::uint8_t>(UIImageScaleMode::Cover) &&
           ValidEdges(value.nineSlice);
}

bool UIAssetValidation::Text(const UIText& value) noexcept {
    return Positive(value.fontSize) && ValidColor(value.color) &&
           static_cast<std::uint8_t>(value.horizontalAlignment) <=
               static_cast<std::uint8_t>(UITextHorizontalAlignment::Right) &&
           static_cast<std::uint8_t>(value.verticalAlignment) <=
               static_cast<std::uint8_t>(UITextVerticalAlignment::Bottom) &&
           static_cast<std::uint8_t>(value.wrapMode) <= static_cast<std::uint8_t>(UITextWrapMode::Character);
}

bool UIAssetValidation::Interaction(const UIInteraction& value) noexcept {
    if (static_cast<std::uint8_t>(value.navigationMode) > static_cast<std::uint8_t>(UINavigationMode::Explicit))
        return false;
    const bool hasExplicitTarget = value.navigationUp != 0U || value.navigationDown != 0U ||
                                   value.navigationLeft != 0U || value.navigationRight != 0U;
    return value.eventName.size() <= kMaxUIActionNameBytes &&
           (value.navigationMode == UINavigationMode::Explicit || !hasExplicitTarget);
}

bool UIAssetValidation::Effects(const UIEffects& value) noexcept {
    return ValidVec2(value.shadowOffset) && ValidColor(value.shadowColor) && NonNegative(value.shadowBlur) &&
           ValidColor(value.outlineColor) && NonNegative(value.outlineWidth) && NonNegative(value.backgroundBlur);
}

bool UIAssetValidation::Control(const UIControlState& value) noexcept {
    if (!Finite(value.sliderValue) || !Finite(value.sliderMinimum) || !Finite(value.sliderMaximum) ||
        !Finite(value.scrollOffset) || value.sliderMinimum > value.sliderMaximum ||
        value.sliderValue < value.sliderMinimum || value.sliderValue > value.sliderMaximum ||
        value.scrollOffset < 0.0F || UIControlKindName(value.kind).empty() || value.text.size() > kMaxUITextBytes ||
        value.listItems.size() > kMaxUIListItems)
        return false;
    for (const std::string& item : value.listItems) {
        if (item.empty() || item.size() > kMaxUITextBytes)
            return false;
    }
    if (value.kind == UIControlKind::Dropdown) {
        return value.listItems.empty() ? value.selectedIndex == 0U : value.selectedIndex < value.listItems.size();
    }
    return value.kind == UIControlKind::WidgetSwitcher || value.selectedIndex == 0U;
}

bool UIAssetValidation::ElementComposition(const UIDocumentElement& value, bool isRoot,
                                           std::size_t directChildCount) noexcept {
    if (!RectTransform(value.rect) || !Control(value.control))
        return false;
    if (value.canvas.has_value() != isRoot || (isRoot && value.control.kind != UIControlKind::Canvas) ||
        (!isRoot && value.control.kind == UIControlKind::Canvas))
        return false;
    if (value.canvas && !Canvas(*value.canvas))
        return false;
    if (value.layout && !ContainerLayout(*value.layout))
        return false;
    if (value.paint && !Paint(*value.paint))
        return false;
    if (value.image && !Image(*value.image))
        return false;
    if (value.textStyle && !Text(*value.textStyle))
        return false;
    if (value.interaction && !Interaction(*value.interaction))
        return false;
    if (value.effects && !Effects(*value.effects))
        return false;
    if (value.control.kind == UIControlKind::Image && !value.image)
        return false;
    if (IsTextKind(value.control.kind) && !value.textStyle)
        return false;
    const std::optional<UIContainerLayoutMode> requiredLayout = RequiredLayoutMode(value.control.kind);
    if (requiredLayout && (!value.layout || value.layout->mode != *requiredLayout))
        return false;
    if (value.control.kind == UIControlKind::WidgetSwitcher) {
        return directChildCount == 0U ? value.control.selectedIndex == 0U
                                      : value.control.selectedIndex < directChildCount;
    }
    return true;
}

} // namespace kb::scene
