#include "engine/ui/UIComponentCatalog.hpp"

#include <algorithm>

namespace kb::scene {
namespace {

#define KB_UI_DESCRIPTOR(Value, Component, Name) UIComponentDescriptor{UIComponentType::Value, Component::StableId, Name}
constexpr std::array kComponents{
    KB_UI_DESCRIPTOR(RectTransform, UIRectTransform, "Rect Transform"), KB_UI_DESCRIPTOR(Canvas, UICanvas, "Canvas"),
    KB_UI_DESCRIPTOR(CanvasScaler, UICanvasScaler, "Canvas Scaler"), KB_UI_DESCRIPTOR(CanvasGroup, UICanvasGroup, "Canvas Group"),
    KB_UI_DESCRIPTOR(HorizontalLayout, UIHorizontalLayout, "Horizontal Layout"), KB_UI_DESCRIPTOR(VerticalLayout, UIVerticalLayout, "Vertical Layout"),
    KB_UI_DESCRIPTOR(GridLayout, UIGridLayout, "Grid Layout"), KB_UI_DESCRIPTOR(WrapLayout, UIWrapLayout, "Wrap Layout"),
    KB_UI_DESCRIPTOR(OverlayLayout, UIOverlayLayout, "Overlay Layout"), KB_UI_DESCRIPTOR(LayoutElement, UILayoutElement, "Layout Element"),
    KB_UI_DESCRIPTOR(ContentSizeFitter, UIContentSizeFitter, "Content Size Fitter"), KB_UI_DESCRIPTOR(AspectRatioFitter, UIAspectRatioFitter, "Aspect Ratio Fitter"),
    KB_UI_DESCRIPTOR(Sprite, UISprite, "Sprite"), KB_UI_DESCRIPTOR(Image, UIImage, "Image"), KB_UI_DESCRIPTOR(RawImage, UIRawImage, "Raw Image"),
    KB_UI_DESCRIPTOR(Text, UIText, "Text"), KB_UI_DESCRIPTOR(Border, UIBorder, "Border"), KB_UI_DESCRIPTOR(Mask, UIMask, "Mask"),
    KB_UI_DESCRIPTOR(Shadow, UIShadow, "Shadow"), KB_UI_DESCRIPTOR(Outline, UIOutline, "Outline"),
    KB_UI_DESCRIPTOR(BackgroundBlur, UIBackgroundBlur, "Background Blur"), KB_UI_DESCRIPTOR(Selectable, UISelectable, "Selectable"),
    KB_UI_DESCRIPTOR(Button, UIButton, "Button"), KB_UI_DESCRIPTOR(Toggle, UIToggle, "Toggle"), KB_UI_DESCRIPTOR(Slider, UISlider, "Slider"),
    KB_UI_DESCRIPTOR(Scrollbar, UIScrollbar, "Scrollbar"), KB_UI_DESCRIPTOR(ScrollView, UIScrollView, "Scroll View"),
    KB_UI_DESCRIPTOR(InputField, UIInputField, "Input Field"), KB_UI_DESCRIPTOR(Dropdown, UIDropdown, "Dropdown"),
    KB_UI_DESCRIPTOR(ProgressBar, UIProgressBar, "Progress Bar"), KB_UI_DESCRIPTOR(WidgetSwitcher, UIWidgetSwitcher, "Widget Switcher"),
};
#undef KB_UI_DESCRIPTOR

constexpr std::array kCanvas{UIComponentType::RectTransform, UIComponentType::Canvas, UIComponentType::CanvasScaler};
constexpr std::array kText{UIComponentType::RectTransform, UIComponentType::Text};
constexpr std::array kImage{UIComponentType::RectTransform, UIComponentType::Image};
constexpr std::array kRawImage{UIComponentType::RectTransform, UIComponentType::RawImage};
constexpr std::array kButton{UIComponentType::RectTransform, UIComponentType::Border, UIComponentType::Text, UIComponentType::Selectable, UIComponentType::Button};
constexpr std::array kToggle{UIComponentType::RectTransform, UIComponentType::Border, UIComponentType::Selectable, UIComponentType::Toggle};
constexpr std::array kSlider{UIComponentType::RectTransform, UIComponentType::Border, UIComponentType::Selectable, UIComponentType::Slider};
constexpr std::array kScrollbar{UIComponentType::RectTransform, UIComponentType::Border, UIComponentType::Selectable, UIComponentType::Scrollbar};
constexpr std::array kScrollView{UIComponentType::RectTransform, UIComponentType::Border, UIComponentType::Mask, UIComponentType::Selectable, UIComponentType::ScrollView};
constexpr std::array kInputField{UIComponentType::RectTransform, UIComponentType::Border, UIComponentType::Text, UIComponentType::Selectable, UIComponentType::InputField};
constexpr std::array kDropdown{UIComponentType::RectTransform, UIComponentType::Border, UIComponentType::Selectable, UIComponentType::Dropdown};
constexpr std::array kProgress{UIComponentType::RectTransform, UIComponentType::Border, UIComponentType::ProgressBar};
constexpr std::array kHorizontal{UIComponentType::RectTransform, UIComponentType::HorizontalLayout};
constexpr std::array kVertical{UIComponentType::RectTransform, UIComponentType::VerticalLayout};
constexpr std::array kGrid{UIComponentType::RectTransform, UIComponentType::GridLayout};
constexpr std::array kWrap{UIComponentType::RectTransform, UIComponentType::WrapLayout};
constexpr std::array kOverlay{UIComponentType::RectTransform, UIComponentType::OverlayLayout};
constexpr std::array kBorder{UIComponentType::RectTransform, UIComponentType::Border};
constexpr std::array kBlur{UIComponentType::RectTransform, UIComponentType::BackgroundBlur};
constexpr std::array kSwitcher{UIComponentType::RectTransform, UIComponentType::WidgetSwitcher};

constexpr std::array kPresets{
    UIComponentPresetDescriptor{UIComponentPreset::Canvas, "Canvas", kCanvas},
    UIComponentPresetDescriptor{UIComponentPreset::Text, "Text", kText},
    UIComponentPresetDescriptor{UIComponentPreset::Image, "Image", kImage},
    UIComponentPresetDescriptor{UIComponentPreset::RawImage, "Raw Image", kRawImage},
    UIComponentPresetDescriptor{UIComponentPreset::Button, "Button", kButton},
    UIComponentPresetDescriptor{UIComponentPreset::Toggle, "Toggle", kToggle},
    UIComponentPresetDescriptor{UIComponentPreset::Slider, "Slider", kSlider},
    UIComponentPresetDescriptor{UIComponentPreset::Scrollbar, "Scrollbar", kScrollbar},
    UIComponentPresetDescriptor{UIComponentPreset::ScrollView, "Scroll View", kScrollView},
    UIComponentPresetDescriptor{UIComponentPreset::InputField, "Input Field", kInputField},
    UIComponentPresetDescriptor{UIComponentPreset::Dropdown, "Dropdown", kDropdown},
    UIComponentPresetDescriptor{UIComponentPreset::ProgressBar, "Progress Bar", kProgress},
    UIComponentPresetDescriptor{UIComponentPreset::HorizontalBox, "Horizontal Box", kHorizontal},
    UIComponentPresetDescriptor{UIComponentPreset::VerticalBox, "Vertical Box", kVertical},
    UIComponentPresetDescriptor{UIComponentPreset::Grid, "Grid", kGrid},
    UIComponentPresetDescriptor{UIComponentPreset::Wrap, "Wrap", kWrap},
    UIComponentPresetDescriptor{UIComponentPreset::Overlay, "Overlay", kOverlay},
    UIComponentPresetDescriptor{UIComponentPreset::Border, "Border", kBorder},
    UIComponentPresetDescriptor{UIComponentPreset::Blur, "Blur", kBlur},
    UIComponentPresetDescriptor{UIComponentPreset::WidgetSwitcher, "Widget Switcher", kSwitcher},
};

void AddPresetSurface(UIComponentSet& components) {
    UIBorder& border = components.border.emplace();
    border.backgroundColor = {0.16F, 0.18F, 0.22F, 1.0F};
    border.borderColor = {0.42F, 0.46F, 0.54F, 1.0F};
    border.borderWidth = {1.0F, 1.0F, 1.0F, 1.0F};
    border.cornerRadius = {3.0F, 3.0F, 3.0F, 3.0F};
}

} // namespace

std::span<const UIComponentDescriptor> UIComponentCatalog() noexcept { return kComponents; }

const UIComponentDescriptor* FindUIComponentDescriptor(UIComponentType type) noexcept {
    const auto found = std::ranges::find(kComponents, type, &UIComponentDescriptor::type);
    return found != kComponents.end() ? &*found : nullptr;
}

const UIComponentDescriptor* FindUIComponentDescriptor(std::string_view idOrName) noexcept {
    const auto found = std::ranges::find_if(kComponents, [idOrName](const UIComponentDescriptor& descriptor) {
        return descriptor.stableId == idOrName || descriptor.displayName == idOrName;
    });
    return found != kComponents.end() ? &*found : nullptr;
}

std::span<const UIComponentPresetDescriptor> UIComponentPresetCatalog() noexcept { return kPresets; }

const UIComponentPresetDescriptor* FindUIComponentPreset(std::string_view name) noexcept {
    const auto found = std::ranges::find(kPresets, name, &UIComponentPresetDescriptor::name);
    return found != kPresets.end() ? &*found : nullptr;
}

UIComponentSet BuildUIComponentPreset(UIComponentPreset preset) {
    UIComponentSet output;
    output.rectTransform.emplace();
    // Every widget used to arrive as UIRectTransform's default 100x100 square, so a menu of
    // six buttons was six identical squares stacked on one point and the author's first job
    // was always to retype sizes. The proportions that make a widget recognisable belong with
    // the composition that defines it - this is the one place both creation paths (the
    // hierarchy Create menu and the Add Component preset tiles) go through.
    // Units are logical UI units against the 1920x1080 reference resolution.
    const auto size = [&output](float width, float height) noexcept {
        output.rectTransform->offsetMax = {width, height};
    };
    switch (preset) {
    case UIComponentPreset::Canvas:
        output.rectTransform->anchorMax = {1.0F, 1.0F}; output.rectTransform->offsetMax = {};
        output.canvas.emplace(); output.canvasScaler.emplace(); break;
    case UIComponentPreset::Text:
        size(160.0F, 32.0F);
        static_cast<void>(SetUITextContent(output.text.emplace(), "Text")); break;
    case UIComponentPreset::Image: size(120.0F, 120.0F); output.image.emplace(); break;
    case UIComponentPreset::RawImage: size(120.0F, 120.0F); output.rawImage.emplace(); break;
    case UIComponentPreset::Button:
        size(160.0F, 40.0F);
        AddPresetSurface(output); output.selectable.emplace(); output.button.emplace();
        static_cast<void>(SetUITextContent(output.text.emplace(), "Button"));
        output.text->horizontalAlignment = UITextHorizontalAlignment::Center;
        output.text->verticalAlignment = UITextVerticalAlignment::Center;
        break;
    case UIComponentPreset::Toggle: size(28.0F, 28.0F); AddPresetSurface(output); output.selectable.emplace(); output.toggle.emplace(); break;
    case UIComponentPreset::Slider: size(220.0F, 20.0F); AddPresetSurface(output); output.selectable.emplace(); output.slider.emplace(); break;
    case UIComponentPreset::Scrollbar: size(200.0F, 16.0F); AddPresetSurface(output); output.selectable.emplace(); output.scrollbar.emplace(); break;
    case UIComponentPreset::ScrollView: size(320.0F, 240.0F); AddPresetSurface(output); output.mask.emplace(); output.selectable.emplace(); output.scrollView.emplace(); break;
    case UIComponentPreset::InputField: size(240.0F, 36.0F); AddPresetSurface(output); output.text.emplace(); output.selectable.emplace(); output.inputField.emplace(); break;
    case UIComponentPreset::Dropdown: size(200.0F, 36.0F); AddPresetSurface(output); output.selectable.emplace(); output.dropdown.emplace(); break;
    case UIComponentPreset::ProgressBar: size(240.0F, 16.0F); AddPresetSurface(output); output.progressBar.emplace(); break;
    case UIComponentPreset::HorizontalBox: size(360.0F, 80.0F); output.horizontalLayout.emplace(); break;
    case UIComponentPreset::VerticalBox: size(200.0F, 320.0F); output.verticalLayout.emplace(); break;
    case UIComponentPreset::Grid: size(320.0F, 320.0F); output.gridLayout.emplace(); break;
    case UIComponentPreset::Wrap: size(320.0F, 200.0F); output.wrapLayout.emplace(); break;
    case UIComponentPreset::Overlay: size(320.0F, 200.0F); output.overlayLayout.emplace(); break;
    case UIComponentPreset::Border: size(240.0F, 140.0F); AddPresetSurface(output); break;
    case UIComponentPreset::Blur: size(240.0F, 140.0F); output.backgroundBlur.emplace(); break;
    case UIComponentPreset::WidgetSwitcher: size(320.0F, 200.0F); output.widgetSwitcher.emplace(); break;
    }
    return output;
}

} // namespace kb::scene
