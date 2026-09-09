#pragma once

#include "engine/ui/UIComponentSet.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace kb::scene {

enum class UIComponentType : std::uint8_t {
    RectTransform, Canvas, CanvasScaler, CanvasGroup,
    HorizontalLayout, VerticalLayout, GridLayout, WrapLayout, OverlayLayout,
    LayoutElement, ContentSizeFitter, AspectRatioFitter,
    Sprite, Image, RawImage, Text, Border, Mask, Shadow, Outline, BackgroundBlur,
    Selectable, Button, Toggle, Slider, Scrollbar, ScrollView, InputField, Dropdown, ProgressBar, WidgetSwitcher,
};

struct UIComponentDescriptor {
    UIComponentType type{};
    std::string_view stableId;
    std::string_view displayName;
};

template <typename T>
struct UIComponentTypeOf;

#define KB_UI_COMPONENT_TYPE(Component, Value) \
    template <> struct UIComponentTypeOf<Component> : std::integral_constant<UIComponentType, UIComponentType::Value> {}
KB_UI_COMPONENT_TYPE(UIRectTransform, RectTransform);
KB_UI_COMPONENT_TYPE(UICanvas, Canvas);
KB_UI_COMPONENT_TYPE(UICanvasScaler, CanvasScaler);
KB_UI_COMPONENT_TYPE(UICanvasGroup, CanvasGroup);
KB_UI_COMPONENT_TYPE(UIHorizontalLayout, HorizontalLayout);
KB_UI_COMPONENT_TYPE(UIVerticalLayout, VerticalLayout);
KB_UI_COMPONENT_TYPE(UIGridLayout, GridLayout);
KB_UI_COMPONENT_TYPE(UIWrapLayout, WrapLayout);
KB_UI_COMPONENT_TYPE(UIOverlayLayout, OverlayLayout);
KB_UI_COMPONENT_TYPE(UILayoutElement, LayoutElement);
KB_UI_COMPONENT_TYPE(UIContentSizeFitter, ContentSizeFitter);
KB_UI_COMPONENT_TYPE(UIAspectRatioFitter, AspectRatioFitter);
KB_UI_COMPONENT_TYPE(UISprite, Sprite);
KB_UI_COMPONENT_TYPE(UIImage, Image);
KB_UI_COMPONENT_TYPE(UIRawImage, RawImage);
KB_UI_COMPONENT_TYPE(UIText, Text);
KB_UI_COMPONENT_TYPE(UIBorder, Border);
KB_UI_COMPONENT_TYPE(UIMask, Mask);
KB_UI_COMPONENT_TYPE(UIShadow, Shadow);
KB_UI_COMPONENT_TYPE(UIOutline, Outline);
KB_UI_COMPONENT_TYPE(UIBackgroundBlur, BackgroundBlur);
KB_UI_COMPONENT_TYPE(UISelectable, Selectable);
KB_UI_COMPONENT_TYPE(UIButton, Button);
KB_UI_COMPONENT_TYPE(UIToggle, Toggle);
KB_UI_COMPONENT_TYPE(UISlider, Slider);
KB_UI_COMPONENT_TYPE(UIScrollbar, Scrollbar);
KB_UI_COMPONENT_TYPE(UIScrollView, ScrollView);
KB_UI_COMPONENT_TYPE(UIInputField, InputField);
KB_UI_COMPONENT_TYPE(UIDropdown, Dropdown);
KB_UI_COMPONENT_TYPE(UIProgressBar, ProgressBar);
KB_UI_COMPONENT_TYPE(UIWidgetSwitcher, WidgetSwitcher);
#undef KB_UI_COMPONENT_TYPE

template <typename T>
concept SceneUIComponent = requires { UIComponentTypeOf<T>::value; };

[[nodiscard]] std::span<const UIComponentDescriptor> UIComponentCatalog() noexcept;
[[nodiscard]] const UIComponentDescriptor* FindUIComponentDescriptor(UIComponentType type) noexcept;
[[nodiscard]] const UIComponentDescriptor* FindUIComponentDescriptor(std::string_view idOrName) noexcept;

enum class UIComponentPreset : std::uint8_t {
    Canvas, Text, Image, RawImage, Button, Toggle, Slider, Scrollbar, ScrollView, InputField,
    Dropdown, ProgressBar, HorizontalBox, VerticalBox, Grid, Wrap, Overlay, Border, Blur, WidgetSwitcher,
};

struct UIComponentPresetDescriptor {
    UIComponentPreset preset{};
    std::string_view name;
    std::span<const UIComponentType> components;
};

[[nodiscard]] std::span<const UIComponentPresetDescriptor> UIComponentPresetCatalog() noexcept;
[[nodiscard]] const UIComponentPresetDescriptor* FindUIComponentPreset(std::string_view name) noexcept;
[[nodiscard]] UIComponentSet BuildUIComponentPreset(UIComponentPreset preset);

} // namespace kb::scene
