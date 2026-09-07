#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace kb::scene {

enum class UIControlKind : std::uint8_t {
    Container,
    Text,
    Image,
    Button,
    Toggle,
    Slider,
    List,
    InputField,
    ScrollView,
    ModalDialog,
    Canvas,
    Border,
    Overlay,
    HorizontalBox,
    VerticalBox,
    Grid,
    Wrap,
    Spacer,
    SizeBox,
    ScaleBox,
    ProgressBar,
    Dropdown,
    Scrollbar,
    WidgetSwitcher,
};

inline constexpr std::array<std::pair<UIControlKind, std::string_view>, 24U> kUIControlKindNames{
    std::pair{UIControlKind::Container, "Container"},
    std::pair{UIControlKind::Text, "Text"},
    std::pair{UIControlKind::Image, "Image"},
    std::pair{UIControlKind::Button, "Button"},
    std::pair{UIControlKind::Toggle, "Toggle"},
    std::pair{UIControlKind::Slider, "Slider"},
    std::pair{UIControlKind::List, "List"},
    std::pair{UIControlKind::InputField, "InputField"},
    std::pair{UIControlKind::ScrollView, "ScrollView"},
    std::pair{UIControlKind::ModalDialog, "ModalDialog"},
    std::pair{UIControlKind::Canvas, "Canvas"},
    std::pair{UIControlKind::Border, "Border"},
    std::pair{UIControlKind::Overlay, "Overlay"},
    std::pair{UIControlKind::HorizontalBox, "HorizontalBox"},
    std::pair{UIControlKind::VerticalBox, "VerticalBox"},
    std::pair{UIControlKind::Grid, "Grid"},
    std::pair{UIControlKind::Wrap, "Wrap"},
    std::pair{UIControlKind::Spacer, "Spacer"},
    std::pair{UIControlKind::SizeBox, "SizeBox"},
    std::pair{UIControlKind::ScaleBox, "ScaleBox"},
    std::pair{UIControlKind::ProgressBar, "ProgressBar"},
    std::pair{UIControlKind::Dropdown, "Dropdown"},
    std::pair{UIControlKind::Scrollbar, "Scrollbar"},
    std::pair{UIControlKind::WidgetSwitcher, "WidgetSwitcher"},
};

[[nodiscard]] constexpr std::string_view UIControlKindName(UIControlKind kind) noexcept {
    for (const auto& [value, name] : kUIControlKindNames) {
        if (value == kind)
            return name;
    }
    return {};
}

[[nodiscard]] constexpr bool TryParseUIControlKind(std::string_view name, UIControlKind& output) noexcept {
    for (const auto& [value, candidate] : kUIControlKindNames) {
        if (candidate == name) {
            output = value;
            return true;
        }
    }
    return false;
}

} // namespace kb::scene
