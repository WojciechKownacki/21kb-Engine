#include "engine/ui/UIComponentPropertyCatalog.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/ui/UIComponentValidation.hpp"

#include <array>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

template <typename T> [[nodiscard]] std::optional<T>& Slot(UIComponentSet& components) noexcept;

template <typename T> [[nodiscard]] const std::optional<T>& Slot(const UIComponentSet& components) noexcept;

#define KB_UI_SLOT(Component, Field)                                                                                   \
    template <> std::optional<Component>& Slot<Component>(UIComponentSet & components) noexcept {                      \
        return components.Field;                                                                                       \
    }                                                                                                                  \
    template <> const std::optional<Component>& Slot<Component>(const UIComponentSet& components) noexcept {           \
        return components.Field;                                                                                       \
    }

KB_UI_SLOT(UIRectTransform, rectTransform)
KB_UI_SLOT(UICanvas, canvas)
KB_UI_SLOT(UICanvasScaler, canvasScaler)
KB_UI_SLOT(UICanvasGroup, canvasGroup)
KB_UI_SLOT(UIHorizontalLayout, horizontalLayout)
KB_UI_SLOT(UIVerticalLayout, verticalLayout)
KB_UI_SLOT(UIGridLayout, gridLayout)
KB_UI_SLOT(UIWrapLayout, wrapLayout)
KB_UI_SLOT(UIOverlayLayout, overlayLayout)
KB_UI_SLOT(UILayoutElement, layoutElement)
KB_UI_SLOT(UIContentSizeFitter, contentSizeFitter)
KB_UI_SLOT(UIAspectRatioFitter, aspectRatioFitter)
KB_UI_SLOT(UISprite, sprite)
KB_UI_SLOT(UIImage, image)
KB_UI_SLOT(UIRawImage, rawImage)
KB_UI_SLOT(UIText, text)
KB_UI_SLOT(UIBorder, border)
KB_UI_SLOT(UIMask, mask)
KB_UI_SLOT(UIShadow, shadow)
KB_UI_SLOT(UIOutline, outline)
KB_UI_SLOT(UIBackgroundBlur, backgroundBlur)
KB_UI_SLOT(UISelectable, selectable)
KB_UI_SLOT(UIButton, button)
KB_UI_SLOT(UIToggle, toggle)
KB_UI_SLOT(UISlider, slider)
KB_UI_SLOT(UIScrollbar, scrollbar)
KB_UI_SLOT(UIScrollView, scrollView)
KB_UI_SLOT(UIInputField, inputField)
KB_UI_SLOT(UIDropdown, dropdown)
KB_UI_SLOT(UIProgressBar, progressBar)
KB_UI_SLOT(UIWidgetSwitcher, widgetSwitcher)

#undef KB_UI_SLOT

struct UIPropertyBinding {
    UIComponentPropertyDescriptor descriptor;
    bool (*read)(const UIComponentSet&, UIComponentPropertyValue&) = nullptr;
    UIComponentPropertyWriteResult (*write)(UIComponentSet&, const UIComponentPropertyValue&) = nullptr;
};

template <typename Value>
[[nodiscard]] bool ConvertPropertyValue(const UIComponentPropertyValue& source, Value& output) noexcept {
    if constexpr (std::is_same_v<Value, bool>) {
        if (const bool* value = std::get_if<bool>(&source)) {
            output = *value;
            return true;
        }
    } else if constexpr (std::is_same_v<Value, std::int32_t>) {
        if (const std::int32_t* value = std::get_if<std::int32_t>(&source)) {
            output = *value;
            return true;
        }
    } else if constexpr (std::is_same_v<Value, std::uint32_t>) {
        if (const std::uint32_t* value = std::get_if<std::uint32_t>(&source)) {
            output = *value;
            return true;
        }
    } else if constexpr (std::is_same_v<Value, std::uint64_t>) {
        if (const std::uint64_t* value = std::get_if<std::uint64_t>(&source)) {
            output = *value;
            return true;
        }
    } else if constexpr (std::is_same_v<Value, float>) {
        if (const float* value = std::get_if<float>(&source)) {
            output = *value;
            return true;
        }
        if (const std::int32_t* value = std::get_if<std::int32_t>(&source)) {
            output = static_cast<float>(*value);
            return true;
        }
        if (const std::uint32_t* value = std::get_if<std::uint32_t>(&source)) {
            output = static_cast<float>(*value);
            return true;
        }
    } else if constexpr (std::is_enum_v<Value>) {
        if (const std::int32_t* value = std::get_if<std::int32_t>(&source)) {
            output = static_cast<Value>(*value);
            return true;
        }
    }
    return false;
}

template <typename Value> [[nodiscard]] UIComponentPropertyValue MakePropertyValue(Value value) {
    if constexpr (std::is_enum_v<Value>) {
        return UIComponentPropertyValue{static_cast<std::int32_t>(value)};
    } else {
        return UIComponentPropertyValue{value};
    }
}

template <typename Component>
[[nodiscard]] UIComponentPropertyWriteResult Commit(UIComponentSet& components, Component candidate) {
    if (!IsUIComponentValid(candidate)) {
        return UIComponentPropertyWriteResult::InvalidValue;
    }
    Slot<Component>(components) = std::move(candidate);
    return UIComponentPropertyWriteResult::Succeeded;
}

template <typename Component, typename Value, Value Component::* Member>
[[nodiscard]] constexpr UIPropertyBinding MemberBinding(
    std::string_view name,
    UIComponentPropertyType type,
    std::optional<kb::assets::AssetKind> assetKind = std::nullopt,
    std::string_view assetDependencyRole = {}) noexcept {
    return UIPropertyBinding{
        .descriptor = {
            .name = name,
            .type = type,
            .writable = true,
            .assetKind = assetKind,
            .assetDependencyRole = assetDependencyRole,
        },
        .read =
            [](const UIComponentSet& components, UIComponentPropertyValue& output) {
                const std::optional<Component>& slot = Slot<Component>(components);
                if (!slot)
                    return false;
                output = MakePropertyValue(slot.value().*Member);
                return true;
            },
        .write =
            [](UIComponentSet& components, const UIComponentPropertyValue& value) {
                const std::optional<Component>& slot = Slot<Component>(components);
                if (!slot)
                    return UIComponentPropertyWriteResult::ComponentMissing;
                Component candidate = *slot;
                if (!ConvertPropertyValue(value, candidate.*Member)) {
                    return UIComponentPropertyWriteResult::TypeMismatch;
                }
                return Commit(components, std::move(candidate));
            },
    };
}

template <typename Component, typename Parent, Parent Component::* ParentMember, typename Value, Value Parent::* Member>
[[nodiscard]] constexpr UIPropertyBinding NestedMemberBinding(std::string_view name) noexcept {
    return UIPropertyBinding{
        .descriptor = {name, UIComponentPropertyType::Float, true},
        .read =
            [](const UIComponentSet& components, UIComponentPropertyValue& output) {
                const std::optional<Component>& slot = Slot<Component>(components);
                if (!slot)
                    return false;
                output = UIComponentPropertyValue{(slot.value().*ParentMember).*Member};
                return true;
            },
        .write =
            [](UIComponentSet& components, const UIComponentPropertyValue& value) {
                const std::optional<Component>& slot = Slot<Component>(components);
                if (!slot)
                    return UIComponentPropertyWriteResult::ComponentMissing;
                Component candidate = *slot;
                if (!ConvertPropertyValue(value, (candidate.*ParentMember).*Member)) {
                    return UIComponentPropertyWriteResult::TypeMismatch;
                }
                return Commit(components, std::move(candidate));
            },
    };
}

template <typename Component, std::string_view (*Read)(const Component&) noexcept,
          bool (*Write)(Component&, std::string_view) noexcept>
[[nodiscard]] constexpr UIPropertyBinding StringBinding(std::string_view name) noexcept {
    return UIPropertyBinding{
        .descriptor = {name, UIComponentPropertyType::String, true},
        .read =
            [](const UIComponentSet& components, UIComponentPropertyValue& output) {
                const std::optional<Component>& slot = Slot<Component>(components);
                if (!slot)
                    return false;
                output = UIComponentPropertyValue{std::string{Read(*slot)}};
                return true;
            },
        .write =
            [](UIComponentSet& components, const UIComponentPropertyValue& value) {
                const std::optional<Component>& slot = Slot<Component>(components);
                if (!slot)
                    return UIComponentPropertyWriteResult::ComponentMissing;
                const std::string* text = std::get_if<std::string>(&value);
                if (text == nullptr)
                    return UIComponentPropertyWriteResult::TypeMismatch;
                Component candidate = *slot;
                if (!Write(candidate, *text))
                    return UIComponentPropertyWriteResult::InvalidValue;
                return Commit(components, std::move(candidate));
            },
    };
}

#define KB_BOOL(Component, Field)                                                                                      \
    MemberBinding<Component, bool, &Component::Field>(#Field, UIComponentPropertyType::Bool)
#define KB_INT(Component, Field)                                                                                       \
    MemberBinding<Component, std::int32_t, &Component::Field>(#Field, UIComponentPropertyType::Int)
#define KB_UINT(Component, Field)                                                                                      \
    MemberBinding<Component, std::uint32_t, &Component::Field>(#Field, UIComponentPropertyType::UInt32)
#define KB_FLOAT(Component, Field)                                                                                     \
    MemberBinding<Component, float, &Component::Field>(#Field, UIComponentPropertyType::Float)
#define KB_ENUM(Component, Field)                                                                                      \
    MemberBinding<Component, decltype(Component::Field), &Component::Field>(#Field, UIComponentPropertyType::Int)
#define KB_TYPED_ASSET(Component, Field, Kind, Role)                                                                   \
    MemberBinding<Component, std::uint64_t, &Component::Field>(                                                        \
        #Field, UIComponentPropertyType::Asset, kb::assets::AssetKind::Kind, Role)
#define KB_ENTITY(Component, Field)                                                                                    \
    MemberBinding<Component, std::uint64_t, &Component::Field>(#Field, UIComponentPropertyType::Entity)
#define KB_NESTED(Component, ParentType, ParentField, Field)                                                           \
    NestedMemberBinding<Component, ParentType, &Component::ParentField, float, &ParentType::Field>(#ParentField        \
                                                                                                   "." #Field)

template <float kb::math::Vec2::* Axis>
[[nodiscard]] constexpr UIPropertyBinding SizeDeltaBinding(std::string_view name) noexcept {
    return {
        .descriptor = {name, UIComponentPropertyType::Float, true},
        .read = [](const UIComponentSet& components, UIComponentPropertyValue& output) {
            if (!components.rectTransform) return false;
            const auto& rect = *components.rectTransform;
            output = rect.offsetMax.*Axis - rect.offsetMin.*Axis;
            return true;
        },
        .write = [](UIComponentSet& components, const UIComponentPropertyValue& value) {
            if (!components.rectTransform) return UIComponentPropertyWriteResult::ComponentMissing;
            float size = 0.0F;
            if (!ConvertPropertyValue(value, size)) return UIComponentPropertyWriteResult::TypeMismatch;
            auto rect = *components.rectTransform;
            rect.offsetMax.*Axis = rect.offsetMin.*Axis + size;
            return Commit(components, rect);
        },
    };
}

constexpr std::array kRectTransformProperties{
    KB_NESTED(UIRectTransform, kb::math::Vec2, anchorMin, x),
    KB_NESTED(UIRectTransform, kb::math::Vec2, anchorMin, y),
    KB_NESTED(UIRectTransform, kb::math::Vec2, anchorMax, x),
    KB_NESTED(UIRectTransform, kb::math::Vec2, anchorMax, y),
    SizeDeltaBinding<&kb::math::Vec2::x>("sizeDelta.x"),
    SizeDeltaBinding<&kb::math::Vec2::y>("sizeDelta.y"),
    KB_NESTED(UIRectTransform, kb::math::Vec2, offsetMin, x),
    KB_NESTED(UIRectTransform, kb::math::Vec2, offsetMin, y),
    KB_NESTED(UIRectTransform, kb::math::Vec2, offsetMax, x),
    KB_NESTED(UIRectTransform, kb::math::Vec2, offsetMax, y),
    KB_NESTED(UIRectTransform, kb::math::Vec2, pivot, x),
    KB_NESTED(UIRectTransform, kb::math::Vec2, pivot, y),
    KB_NESTED(UIRectTransform, kb::math::Vec2, scale, x),
    KB_NESTED(UIRectTransform, kb::math::Vec2, scale, y),
    KB_FLOAT(UIRectTransform, rotationDegrees),
    KB_INT(UIRectTransform, zOrder),
};
constexpr std::array kCanvasProperties{KB_INT(UICanvas, sortingOrder), KB_BOOL(UICanvas, pixelPerfect)};
constexpr std::array kCanvasScalerProperties{
    KB_ENUM(UICanvasScaler, scaleMode),
    KB_NESTED(UICanvasScaler, kb::math::Vec2, referenceResolution, x),
    KB_NESTED(UICanvasScaler, kb::math::Vec2, referenceResolution, y),
    KB_FLOAT(UICanvasScaler, scaleFactor),
    KB_FLOAT(UICanvasScaler, matchWidthOrHeight),
};
constexpr std::array kCanvasGroupProperties{
    KB_FLOAT(UICanvasGroup, opacity),
    KB_BOOL(UICanvasGroup, interactable),
    KB_BOOL(UICanvasGroup, blocksRaycasts),
    KB_BOOL(UICanvasGroup, ignoreParentGroups),
};

#define KB_EDGES(Component, Field)                                                                                     \
    KB_NESTED(Component, UIEdges, Field, left), KB_NESTED(Component, UIEdges, Field, top),                             \
        KB_NESTED(Component, UIEdges, Field, right), KB_NESTED(Component, UIEdges, Field, bottom)
#define KB_COLOR(Component, Field)                                                                                     \
    KB_NESTED(Component, kb::math::Color, Field, r), KB_NESTED(Component, kb::math::Color, Field, g),                  \
        KB_NESTED(Component, kb::math::Color, Field, b), KB_NESTED(Component, kb::math::Color, Field, a)
#define KB_VEC4(Component, Field)                                                                                      \
    KB_NESTED(Component, kb::math::Vec4, Field, x), KB_NESTED(Component, kb::math::Vec4, Field, y),                    \
        KB_NESTED(Component, kb::math::Vec4, Field, z), KB_NESTED(Component, kb::math::Vec4, Field, w)
#define KB_RECT(Component, Field)                                                                                      \
    KB_NESTED(Component, kb::math::Rect, Field, x), KB_NESTED(Component, kb::math::Rect, Field, y),                    \
        KB_NESTED(Component, kb::math::Rect, Field, width), KB_NESTED(Component, kb::math::Rect, Field, height)

constexpr std::array kHorizontalLayoutProperties{
    KB_EDGES(UIHorizontalLayout, padding),
    KB_FLOAT(UIHorizontalLayout, spacing),
    KB_ENUM(UIHorizontalLayout, horizontalAlignment),
    KB_ENUM(UIHorizontalLayout, verticalAlignment),
    KB_BOOL(UIHorizontalLayout, controlChildWidth),
    KB_BOOL(UIHorizontalLayout, controlChildHeight),
    KB_BOOL(UIHorizontalLayout, expandChildWidth),
    KB_BOOL(UIHorizontalLayout, expandChildHeight),
};
constexpr std::array kVerticalLayoutProperties{
    KB_EDGES(UIVerticalLayout, padding),
    KB_FLOAT(UIVerticalLayout, spacing),
    KB_ENUM(UIVerticalLayout, horizontalAlignment),
    KB_ENUM(UIVerticalLayout, verticalAlignment),
    KB_BOOL(UIVerticalLayout, controlChildWidth),
    KB_BOOL(UIVerticalLayout, controlChildHeight),
    KB_BOOL(UIVerticalLayout, expandChildWidth),
    KB_BOOL(UIVerticalLayout, expandChildHeight),
};
constexpr std::array kGridLayoutProperties{
    KB_EDGES(UIGridLayout, padding),
    KB_NESTED(UIGridLayout, kb::math::Vec2, spacing, x),
    KB_NESTED(UIGridLayout, kb::math::Vec2, spacing, y),
    KB_NESTED(UIGridLayout, kb::math::Vec2, cellSize, x),
    KB_NESTED(UIGridLayout, kb::math::Vec2, cellSize, y),
    KB_UINT(UIGridLayout, columns),
    KB_ENUM(UIGridLayout, horizontalAlignment),
    KB_ENUM(UIGridLayout, verticalAlignment),
};
constexpr std::array kWrapLayoutProperties{
    KB_EDGES(UIWrapLayout, padding),
    KB_NESTED(UIWrapLayout, kb::math::Vec2, spacing, x),
    KB_NESTED(UIWrapLayout, kb::math::Vec2, spacing, y),
    KB_ENUM(UIWrapLayout, horizontalAlignment),
    KB_ENUM(UIWrapLayout, verticalAlignment),
};
constexpr std::array kOverlayLayoutProperties{
    KB_EDGES(UIOverlayLayout, padding),
    KB_ENUM(UIOverlayLayout, horizontalAlignment),
    KB_ENUM(UIOverlayLayout, verticalAlignment),
};
constexpr std::array kLayoutElementProperties{
    KB_FLOAT(UILayoutElement, minimumWidth),   KB_FLOAT(UILayoutElement, minimumHeight),
    KB_FLOAT(UILayoutElement, preferredWidth), KB_FLOAT(UILayoutElement, preferredHeight),
    KB_FLOAT(UILayoutElement, flexibleWidth),  KB_FLOAT(UILayoutElement, flexibleHeight),
    KB_INT(UILayoutElement, layoutPriority),   KB_BOOL(UILayoutElement, ignoreLayout),
};
constexpr std::array kContentSizeFitterProperties{
    KB_ENUM(UIContentSizeFitter, horizontalFit),
    KB_ENUM(UIContentSizeFitter, verticalFit),
};
constexpr std::array kAspectRatioFitterProperties{
    KB_ENUM(UIAspectRatioFitter, mode),
    KB_FLOAT(UIAspectRatioFitter, aspectRatio),
};
constexpr std::array kSpriteProperties{
    KB_TYPED_ASSET(UISprite, spriteAssetId, Texture, "ui.sprite"),
    KB_COLOR(UISprite, color),
    KB_BOOL(UISprite, preserveAspect),
};
constexpr std::array kImageProperties{
    KB_TYPED_ASSET(UIImage, imageAssetId, Texture, "ui.image"), KB_RECT(UIImage, uvRect), KB_ENUM(UIImage, scaleMode),
    KB_BOOL(UIImage, preserveAspect), KB_EDGES(UIImage, nineSlice), KB_COLOR(UIImage, color),
};
constexpr std::array kRawImageProperties{
    KB_TYPED_ASSET(UIRawImage, imageAssetId, Texture, "ui.rawImage"),
    KB_RECT(UIRawImage, uvRect),
    KB_COLOR(UIRawImage, color),
};
constexpr std::array kTextProperties{
    StringBinding<UIText, &UITextContent, &SetUITextContent>("content"),
    KB_TYPED_ASSET(UIText, fontAssetId, Font, "ui.font"),
    KB_FLOAT(UIText, fontSize),
    KB_COLOR(UIText, color),
    KB_ENUM(UIText, horizontalAlignment),
    KB_ENUM(UIText, verticalAlignment),
    KB_ENUM(UIText, wrapMode),
    KB_FLOAT(UIText, lineSpacing),
    KB_BOOL(UIText, richText),
};
constexpr std::array kBorderProperties{
    KB_COLOR(UIBorder, backgroundColor), KB_COLOR(UIBorder, borderColor), KB_EDGES(UIBorder, borderWidth),
    KB_VEC4(UIBorder, cornerRadius),     KB_FLOAT(UIBorder, opacity),
};
constexpr std::array kMaskProperties{KB_BOOL(UIMask, showGraphic)};
constexpr std::array kShadowProperties{
    KB_NESTED(UIShadow, kb::math::Vec2, offset, x),
    KB_NESTED(UIShadow, kb::math::Vec2, offset, y),
    KB_COLOR(UIShadow, color),
    KB_FLOAT(UIShadow, blur),
};
constexpr std::array kOutlineProperties{KB_COLOR(UIOutline, color), KB_FLOAT(UIOutline, width)};
constexpr std::array kBackgroundBlurProperties{KB_FLOAT(UIBackgroundBlur, radius)};
constexpr std::array kSelectableProperties{
    KB_BOOL(UISelectable, raycastTarget),     KB_BOOL(UISelectable, interactable),
    KB_ENUM(UISelectable, navigationMode),    KB_ENTITY(UISelectable, navigationUp),
    KB_ENTITY(UISelectable, navigationDown),  KB_ENTITY(UISelectable, navigationLeft),
    KB_ENTITY(UISelectable, navigationRight), StringBinding<UISelectable, &UIEventName, &SetUIEventName>("eventName"),
    KB_COLOR(UISelectable, normalColor),      KB_COLOR(UISelectable, highlightedColor),
    KB_COLOR(UISelectable, pressedColor),     KB_COLOR(UISelectable, selectedColor),
    KB_COLOR(UISelectable, disabledColor),    KB_FLOAT(UISelectable, colorFadeSeconds),
};
constexpr std::array kButtonProperties{KB_BOOL(UIButton, submitOnRelease)};
constexpr std::array kToggleProperties{KB_BOOL(UIToggle, toggled)};
constexpr std::array kSliderProperties{
    KB_FLOAT(UISlider, minimum),  KB_FLOAT(UISlider, maximum),     KB_FLOAT(UISlider, value),
    KB_ENUM(UISlider, direction), KB_BOOL(UISlider, wholeNumbers),
};
constexpr std::array kScrollbarProperties{
    KB_FLOAT(UIScrollbar, value),
    KB_FLOAT(UIScrollbar, size),
    KB_ENUM(UIScrollbar, direction),
};
constexpr std::array kScrollViewProperties{
    KB_FLOAT(UIScrollView, scrollX),   KB_FLOAT(UIScrollView, scrollY), KB_FLOAT(UIScrollView, scrollSensitivity),
    KB_BOOL(UIScrollView, horizontal), KB_BOOL(UIScrollView, vertical), KB_BOOL(UIScrollView, inertia),
};
constexpr std::array kInputFieldProperties{
    KB_UINT(UIInputField, characterLimit),
    KB_BOOL(UIInputField, multiline),
    KB_BOOL(UIInputField, readOnly),
};
constexpr std::array kDropdownProperties{KB_UINT(UIDropdown, selectedIndex)};
constexpr std::array kProgressBarProperties{
    KB_FLOAT(UIProgressBar, minimum),
    KB_FLOAT(UIProgressBar, maximum),
    KB_FLOAT(UIProgressBar, value),
};
constexpr std::array kWidgetSwitcherProperties{KB_UINT(UIWidgetSwitcher, visibleChildIndex)};

#undef KB_RECT
#undef KB_VEC4
#undef KB_COLOR
#undef KB_EDGES
#undef KB_NESTED
#undef KB_ENTITY
#undef KB_TYPED_ASSET
#undef KB_ENUM
#undef KB_FLOAT
#undef KB_UINT
#undef KB_INT
#undef KB_BOOL

[[nodiscard]] std::span<const UIPropertyBinding> Bindings(UIComponentType component) noexcept {
    switch (component) {
    case UIComponentType::RectTransform:
        return kRectTransformProperties;
    case UIComponentType::Canvas:
        return kCanvasProperties;
    case UIComponentType::CanvasScaler:
        return kCanvasScalerProperties;
    case UIComponentType::CanvasGroup:
        return kCanvasGroupProperties;
    case UIComponentType::HorizontalLayout:
        return kHorizontalLayoutProperties;
    case UIComponentType::VerticalLayout:
        return kVerticalLayoutProperties;
    case UIComponentType::GridLayout:
        return kGridLayoutProperties;
    case UIComponentType::WrapLayout:
        return kWrapLayoutProperties;
    case UIComponentType::OverlayLayout:
        return kOverlayLayoutProperties;
    case UIComponentType::LayoutElement:
        return kLayoutElementProperties;
    case UIComponentType::ContentSizeFitter:
        return kContentSizeFitterProperties;
    case UIComponentType::AspectRatioFitter:
        return kAspectRatioFitterProperties;
    case UIComponentType::Sprite:
        return kSpriteProperties;
    case UIComponentType::Image:
        return kImageProperties;
    case UIComponentType::RawImage:
        return kRawImageProperties;
    case UIComponentType::Text:
        return kTextProperties;
    case UIComponentType::Border:
        return kBorderProperties;
    case UIComponentType::Mask:
        return kMaskProperties;
    case UIComponentType::Shadow:
        return kShadowProperties;
    case UIComponentType::Outline:
        return kOutlineProperties;
    case UIComponentType::BackgroundBlur:
        return kBackgroundBlurProperties;
    case UIComponentType::Selectable:
        return kSelectableProperties;
    case UIComponentType::Button:
        return kButtonProperties;
    case UIComponentType::Toggle:
        return kToggleProperties;
    case UIComponentType::Slider:
        return kSliderProperties;
    case UIComponentType::Scrollbar:
        return kScrollbarProperties;
    case UIComponentType::ScrollView:
        return kScrollViewProperties;
    case UIComponentType::InputField:
        return kInputFieldProperties;
    case UIComponentType::Dropdown:
        return kDropdownProperties;
    case UIComponentType::ProgressBar:
        return kProgressBarProperties;
    case UIComponentType::WidgetSwitcher:
        return kWidgetSwitcherProperties;
    }
    return {};
}

[[nodiscard]] const UIPropertyBinding* FindBinding(UIComponentType component, std::string_view property) noexcept {
    for (const UIPropertyBinding& binding : Bindings(component)) {
        if (binding.descriptor.name == property)
            return &binding;
    }
    return nullptr;
}

template <typename Visitor>
[[nodiscard]] bool VisitSlot(UIComponentSet& components, UIComponentType component, Visitor&& visitor) {
#define KB_VISIT(Type, Field)                                                                                          \
    case UIComponentType::Type:                                                                                        \
        return visitor(components.Field)
    switch (component) {
        KB_VISIT(RectTransform, rectTransform);
        KB_VISIT(Canvas, canvas);
        KB_VISIT(CanvasScaler, canvasScaler);
        KB_VISIT(CanvasGroup, canvasGroup);
        KB_VISIT(HorizontalLayout, horizontalLayout);
        KB_VISIT(VerticalLayout, verticalLayout);
        KB_VISIT(GridLayout, gridLayout);
        KB_VISIT(WrapLayout, wrapLayout);
        KB_VISIT(OverlayLayout, overlayLayout);
        KB_VISIT(LayoutElement, layoutElement);
        KB_VISIT(ContentSizeFitter, contentSizeFitter);
        KB_VISIT(AspectRatioFitter, aspectRatioFitter);
        KB_VISIT(Sprite, sprite);
        KB_VISIT(Image, image);
        KB_VISIT(RawImage, rawImage);
        KB_VISIT(Text, text);
        KB_VISIT(Border, border);
        KB_VISIT(Mask, mask);
        KB_VISIT(Shadow, shadow);
        KB_VISIT(Outline, outline);
        KB_VISIT(BackgroundBlur, backgroundBlur);
        KB_VISIT(Selectable, selectable);
        KB_VISIT(Button, button);
        KB_VISIT(Toggle, toggle);
        KB_VISIT(Slider, slider);
        KB_VISIT(Scrollbar, scrollbar);
        KB_VISIT(ScrollView, scrollView);
        KB_VISIT(InputField, inputField);
        KB_VISIT(Dropdown, dropdown);
        KB_VISIT(ProgressBar, progressBar);
        KB_VISIT(WidgetSwitcher, widgetSwitcher);
    }
#undef KB_VISIT
    return false;
}

template <typename Visitor>
[[nodiscard]] bool VisitSlot(const UIComponentSet& components, UIComponentType component, Visitor&& visitor) {
    return VisitSlot(const_cast<UIComponentSet&>(components), component,
                     [&visitor](const auto& slot) { return visitor(slot); });
}

} // namespace

std::span<const UIComponentPropertyDescriptor> UIComponentPropertyCatalog(UIComponentType component) noexcept {
    constexpr std::size_t kComponentCount = static_cast<std::size_t>(UIComponentType::WidgetSwitcher) + 1U;
    static const std::array<std::vector<UIComponentPropertyDescriptor>, kComponentCount> kDescriptors = [] {
        std::array<std::vector<UIComponentPropertyDescriptor>, kComponentCount> result;
        for (std::size_t index = 0U; index < result.size(); ++index) {
            for (const UIPropertyBinding& binding : Bindings(static_cast<UIComponentType>(index))) {
                result[index].push_back(binding.descriptor);
            }
        }
        return result;
    }();
    const std::size_t index = static_cast<std::size_t>(component);
    return index < kDescriptors.size() ? std::span<const UIComponentPropertyDescriptor>{kDescriptors[index]}
                                       : std::span<const UIComponentPropertyDescriptor>{};
}

const UIComponentPropertyDescriptor* FindUIComponentProperty(UIComponentType component,
                                                             std::string_view property) noexcept {
    const UIPropertyBinding* binding = FindBinding(component, property);
    return binding != nullptr ? &binding->descriptor : nullptr;
}

UIComponentAssetReferenceValidationResult ValidateUIComponentAssetReference(
    const kb::assets::AssetRegistry& registry,
    const UIComponentPropertyDescriptor& property,
    std::uint64_t rawAssetId) noexcept {
    if (property.type != UIComponentPropertyType::Asset || !property.assetKind.has_value()) {
        return UIComponentAssetReferenceValidationResult::NotAssetProperty;
    }
    if (rawAssetId == 0U) {
        return UIComponentAssetReferenceValidationResult::Succeeded;
    }
    const kb::assets::AssetMetadata* metadata = registry.Find(kb::assets::AssetId{rawAssetId});
    if (metadata == nullptr) {
        return UIComponentAssetReferenceValidationResult::MissingAsset;
    }
    return kb::assets::AssetMatchesKind(*metadata, *property.assetKind)
        ? UIComponentAssetReferenceValidationResult::Succeeded
        : UIComponentAssetReferenceValidationResult::WrongAssetKind;
}

bool HasUIComponent(const UIComponentSet& components, UIComponentType component) noexcept {
    return VisitSlot(components, component, [](const auto& slot) { return slot.has_value(); });
}

bool AddUIComponent(UIComponentSet& components, UIComponentType component) {
    UIComponentSet candidate = components;
    const bool added = VisitSlot(candidate, component, [](auto& slot) {
        if (slot)
            return false;
        slot.emplace();
        return true;
    });
    if (!added || !IsUIComponentSetValid(candidate))
        return false;
    components = std::move(candidate);
    return true;
}

bool RemoveUIComponent(UIComponentSet& components, UIComponentType component) noexcept {
    return VisitSlot(components, component, [](auto& slot) {
        if (!slot)
            return false;
        slot.reset();
        return true;
    });
}

bool ReadUIComponentProperty(const UIComponentSet& components, UIComponentType component, std::string_view property,
                             UIComponentPropertyValue& output) {
    const UIPropertyBinding* binding = FindBinding(component, property);
    return binding != nullptr && binding->read(components, output);
}

UIComponentPropertyWriteResult WriteUIComponentProperty(UIComponentSet& components, UIComponentType component,
                                                        std::string_view property,
                                                        const UIComponentPropertyValue& value) {
    const UIPropertyBinding* binding = FindBinding(component, property);
    if (binding == nullptr)
        return UIComponentPropertyWriteResult::PropertyMissing;
    if (!binding->descriptor.writable)
        return UIComponentPropertyWriteResult::ReadOnly;
    return binding->write(components, value);
}

} // namespace kb::scene
