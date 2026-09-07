#include "engine/ui/UIComponentValidation.hpp"

#include "engine/input/InputText.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::scene {
namespace {

using kb::math::Color;
using kb::math::Rect;
using kb::math::Vec2;
using kb::math::Vec4;

[[nodiscard]] bool Finite(float value) noexcept { return std::isfinite(value); }
[[nodiscard]] bool Finite(Vec2 value) noexcept { return Finite(value.x) && Finite(value.y); }
[[nodiscard]] bool Finite(Vec4 value) noexcept { return Finite(value.x) && Finite(value.y) && Finite(value.z) && Finite(value.w); }
[[nodiscard]] bool Finite(Color value) noexcept { return Finite(value.r) && Finite(value.g) && Finite(value.b) && Finite(value.a); }
[[nodiscard]] bool Finite(Rect value) noexcept { return Finite(value.x) && Finite(value.y) && Finite(value.width) && Finite(value.height); }
[[nodiscard]] bool Finite(UIEdges value) noexcept { return Finite(value.left) && Finite(value.top) && Finite(value.right) && Finite(value.bottom); }
[[nodiscard]] bool Normalized(Color value) noexcept {
    return Finite(value) && value.r >= 0.0F && value.r <= 1.0F && value.g >= 0.0F && value.g <= 1.0F &&
        value.b >= 0.0F && value.b <= 1.0F && value.a >= 0.0F && value.a <= 1.0F;
}
[[nodiscard]] bool NonNegative(UIEdges value) noexcept {
    return Finite(value) && value.left >= 0.0F && value.top >= 0.0F && value.right >= 0.0F && value.bottom >= 0.0F;
}
template <typename Enum>
[[nodiscard]] bool EnumAtMost(Enum value, Enum maximum) noexcept {
    return static_cast<unsigned int>(value) <= static_cast<unsigned int>(maximum);
}
template <typename T>
[[nodiscard]] bool OptionalValid(const std::optional<T>& component) noexcept {
    return !component || IsUIComponentValid(*component);
}
[[nodiscard]] bool LayoutBase(UIEdges padding, UIAlignment horizontal, UIAlignment vertical) noexcept {
    return NonNegative(padding) && EnumAtMost(horizontal, UIAlignment::Stretch) && EnumAtMost(vertical, UIAlignment::Stretch);
}
[[nodiscard]] bool Range(float minimum, float maximum, float value) noexcept {
    return Finite(minimum) && Finite(maximum) && Finite(value) && minimum <= maximum && value >= minimum && value <= maximum;
}

} // namespace

bool IsUIComponentValid(const UIRectTransform& value) noexcept {
    return Finite(value.anchorMin) && Finite(value.anchorMax) && Finite(value.offsetMin) && Finite(value.offsetMax) &&
        Finite(value.pivot) && Finite(value.scale) && Finite(value.rotationDegrees) && value.anchorMin.x >= 0.0F &&
        value.anchorMin.y >= 0.0F && value.anchorMax.x <= 1.0F && value.anchorMax.y <= 1.0F &&
        value.anchorMin.x <= value.anchorMax.x && value.anchorMin.y <= value.anchorMax.y &&
        value.pivot.x >= 0.0F && value.pivot.x <= 1.0F && value.pivot.y >= 0.0F && value.pivot.y <= 1.0F;
}
bool IsUIComponentValid(const UICanvas&) noexcept { return true; }
bool IsUIComponentValid(const UICanvasScaler& value) noexcept {
    return EnumAtMost(value.scaleMode, UICanvasScaleMode::ScaleWithScreenSize) && Finite(value.referenceResolution) &&
        value.referenceResolution.x > 0.0F && value.referenceResolution.y > 0.0F && Finite(value.scaleFactor) &&
        value.scaleFactor > 0.0F && Finite(value.matchWidthOrHeight) && value.matchWidthOrHeight >= 0.0F &&
        value.matchWidthOrHeight <= 1.0F;
}
bool IsUIComponentValid(const UICanvasGroup& value) noexcept {
    return Finite(value.opacity) && value.opacity >= 0.0F && value.opacity <= 1.0F;
}
bool IsUIComponentValid(const UIHorizontalLayout& value) noexcept {
    return LayoutBase(value.padding, value.horizontalAlignment, value.verticalAlignment) && Finite(value.spacing) && value.spacing >= 0.0F;
}
bool IsUIComponentValid(const UIVerticalLayout& value) noexcept {
    return LayoutBase(value.padding, value.horizontalAlignment, value.verticalAlignment) && Finite(value.spacing) && value.spacing >= 0.0F;
}
bool IsUIComponentValid(const UIGridLayout& value) noexcept {
    return LayoutBase(value.padding, value.horizontalAlignment, value.verticalAlignment) && Finite(value.spacing) &&
        value.spacing.x >= 0.0F && value.spacing.y >= 0.0F && Finite(value.cellSize) && value.cellSize.x > 0.0F &&
        value.cellSize.y > 0.0F && value.columns > 0U;
}
bool IsUIComponentValid(const UIWrapLayout& value) noexcept {
    return LayoutBase(value.padding, value.horizontalAlignment, value.verticalAlignment) && Finite(value.spacing) &&
        value.spacing.x >= 0.0F && value.spacing.y >= 0.0F;
}
bool IsUIComponentValid(const UIOverlayLayout& value) noexcept {
    return LayoutBase(value.padding, value.horizontalAlignment, value.verticalAlignment);
}
bool IsUIComponentValid(const UILayoutElement& value) noexcept {
    const auto valid = [](float size) noexcept { return Finite(size) && size >= -1.0F; };
    return valid(value.minimumWidth) && valid(value.minimumHeight) && valid(value.preferredWidth) &&
        valid(value.preferredHeight) && valid(value.flexibleWidth) && valid(value.flexibleHeight);
}
bool IsUIComponentValid(const UIContentSizeFitter& value) noexcept {
    return EnumAtMost(value.horizontalFit, UIFitMode::PreferredSize) && EnumAtMost(value.verticalFit, UIFitMode::PreferredSize);
}
bool IsUIComponentValid(const UIAspectRatioFitter& value) noexcept {
    return EnumAtMost(value.mode, UIAspectFitMode::EnvelopeParent) && Finite(value.aspectRatio) && value.aspectRatio > 0.0F;
}
bool IsUIComponentValid(const UISprite& value) noexcept { return Normalized(value.color); }
bool IsUIComponentValid(const UIImage& value) noexcept {
    return Finite(value.uvRect) && value.uvRect.width >= 0.0F && value.uvRect.height >= 0.0F &&
        EnumAtMost(value.scaleMode, UIImageScaleMode::Cover) && NonNegative(value.nineSlice) && Normalized(value.color);
}
bool IsUIComponentValid(const UIRawImage& value) noexcept {
    return Finite(value.uvRect) && value.uvRect.width >= 0.0F && value.uvRect.height >= 0.0F && Normalized(value.color);
}
bool IsUIComponentValid(const UIText& value) noexcept {
    const std::string_view content = UITextContent(value);
    std::array<char32_t, UIText::MaxUtf8Bytes> decoded{};
    return std::find(value.content.begin(), value.content.end(), '\0') != value.content.end() &&
        kb::input::DecodeUtf8(content, decoded).wellFormed && Finite(value.fontSize) &&
        value.fontSize > 0.0F && Normalized(value.color) && EnumAtMost(value.horizontalAlignment, UITextHorizontalAlignment::Right) &&
        EnumAtMost(value.verticalAlignment, UITextVerticalAlignment::Bottom) && EnumAtMost(value.wrapMode, UITextWrapMode::Character) &&
        Finite(value.lineSpacing) && value.lineSpacing > 0.0F;
}
bool IsUIComponentValid(const UIBorder& value) noexcept {
    return Normalized(value.backgroundColor) && Normalized(value.borderColor) && NonNegative(value.borderWidth) &&
        Finite(value.cornerRadius) && value.cornerRadius.x >= 0.0F && value.cornerRadius.y >= 0.0F &&
        value.cornerRadius.z >= 0.0F && value.cornerRadius.w >= 0.0F && Finite(value.opacity) &&
        value.opacity >= 0.0F && value.opacity <= 1.0F;
}
bool IsUIComponentValid(const UIMask&) noexcept { return true; }
bool IsUIComponentValid(const UIShadow& value) noexcept {
    return Finite(value.offset) && Normalized(value.color) && Finite(value.blur) && value.blur >= 0.0F;
}
bool IsUIComponentValid(const UIOutline& value) noexcept {
    return Normalized(value.color) && Finite(value.width) && value.width >= 0.0F;
}
bool IsUIComponentValid(const UIBackgroundBlur& value) noexcept { return Finite(value.radius) && value.radius >= 0.0F; }
bool IsUIComponentValid(const UISelectable& value) noexcept {
    return EnumAtMost(value.navigationMode, UINavigationMode::Explicit) &&
        std::find(value.eventName.begin(), value.eventName.end(), '\0') != value.eventName.end() &&
        Normalized(value.normalColor) && Normalized(value.highlightedColor) && Normalized(value.pressedColor) &&
        Normalized(value.selectedColor) && Normalized(value.disabledColor) && Finite(value.colorFadeSeconds) &&
        value.colorFadeSeconds >= 0.0F;
}
bool IsUIComponentValid(const UIButton&) noexcept { return true; }
bool IsUIComponentValid(const UIToggle&) noexcept { return true; }
bool IsUIComponentValid(const UISlider& value) noexcept {
    return Range(value.minimum, value.maximum, value.value) && EnumAtMost(value.direction, UIAxisDirection::TopToBottom);
}
bool IsUIComponentValid(const UIScrollbar& value) noexcept {
    return Finite(value.value) && value.value >= 0.0F && value.value <= 1.0F && Finite(value.size) && value.size >= 0.0F &&
        value.size <= 1.0F && EnumAtMost(value.direction, UIAxisDirection::TopToBottom);
}
bool IsUIComponentValid(const UIScrollView& value) noexcept {
    return Finite(value.scrollX) && Finite(value.scrollY) && Finite(value.scrollSensitivity) && value.scrollSensitivity >= 0.0F;
}
bool IsUIComponentValid(const UIInputField&) noexcept { return true; }
bool IsUIComponentValid(const UIDropdown&) noexcept { return true; }
bool IsUIComponentValid(const UIProgressBar& value) noexcept { return Range(value.minimum, value.maximum, value.value); }
bool IsUIComponentValid(const UIWidgetSwitcher&) noexcept { return true; }

bool IsUIComponentSetValid(const UIComponentSet& value) noexcept {
    const std::size_t layoutCount = static_cast<std::size_t>(value.horizontalLayout.has_value()) +
        static_cast<std::size_t>(value.verticalLayout.has_value()) + static_cast<std::size_t>(value.gridLayout.has_value()) +
        static_cast<std::size_t>(value.wrapLayout.has_value()) + static_cast<std::size_t>(value.overlayLayout.has_value());
    return layoutCount <= 1U && OptionalValid(value.rectTransform) && OptionalValid(value.canvas) &&
        OptionalValid(value.canvasScaler) && OptionalValid(value.canvasGroup) && OptionalValid(value.horizontalLayout) &&
        OptionalValid(value.verticalLayout) && OptionalValid(value.gridLayout) && OptionalValid(value.wrapLayout) &&
        OptionalValid(value.overlayLayout) && OptionalValid(value.layoutElement) && OptionalValid(value.contentSizeFitter) &&
        OptionalValid(value.aspectRatioFitter) && OptionalValid(value.sprite) && OptionalValid(value.image) &&
        OptionalValid(value.rawImage) && OptionalValid(value.text) && OptionalValid(value.border) && OptionalValid(value.mask) &&
        OptionalValid(value.shadow) && OptionalValid(value.outline) && OptionalValid(value.backgroundBlur) &&
        OptionalValid(value.selectable) && OptionalValid(value.button) && OptionalValid(value.toggle) &&
        OptionalValid(value.slider) && OptionalValid(value.scrollbar) && OptionalValid(value.scrollView) &&
        OptionalValid(value.inputField) && OptionalValid(value.dropdown) && OptionalValid(value.progressBar) &&
        OptionalValid(value.widgetSwitcher);
}

} // namespace kb::scene
