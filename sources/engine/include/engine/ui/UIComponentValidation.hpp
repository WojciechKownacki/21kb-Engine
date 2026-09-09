#pragma once

#include "engine/ui/UIComponentSet.hpp"

namespace kb::scene {

[[nodiscard]] bool IsUIComponentValid(const UIRectTransform& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UICanvas& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UICanvasScaler& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UICanvasGroup& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIHorizontalLayout& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIVerticalLayout& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIGridLayout& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIWrapLayout& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIOverlayLayout& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UILayoutElement& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIContentSizeFitter& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIAspectRatioFitter& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UISprite& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIImage& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIRawImage& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIText& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIBorder& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIMask& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIShadow& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIOutline& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIBackgroundBlur& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UISelectable& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIButton& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIToggle& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UISlider& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIScrollbar& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIScrollView& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIInputField& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIDropdown& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIProgressBar& component) noexcept;
[[nodiscard]] bool IsUIComponentValid(const UIWidgetSwitcher& component) noexcept;
[[nodiscard]] bool IsUIComponentSetValid(const UIComponentSet& components) noexcept;

} // namespace kb::scene
