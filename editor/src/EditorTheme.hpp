#pragma once

#include <cstdint>

namespace kb::editor {

struct EditorColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

struct EditorTheme {
    EditorColor background;
    EditorColor chrome;
    EditorColor panel;
    EditorColor strip;
    EditorColor tabInactive;
    EditorColor tabActive;
    EditorColor menuBar;
    EditorColor toolbar;
    EditorColor toolbarButton;
    EditorColor splitter;
    EditorColor gridLine;
    EditorColor borderChrome;
    EditorColor borderPanel;
    EditorColor textPrimary;
    EditorColor textSecondary;
    EditorColor textDisabled;
    EditorColor accent;
};

struct EditorMetrics {
    int menuHeight = 40;
    int toolbarHeight = 48;
    int tabStripHeight = 28;
    int tabWidth = 156;
    int splitterSize = 6;
    int panelPadding = 18;
};

[[nodiscard]] constexpr EditorColor Color(float r, float g, float b) {
    return EditorColor{
        static_cast<std::uint8_t>(r * 255.0F),
        static_cast<std::uint8_t>(g * 255.0F),
        static_cast<std::uint8_t>(b * 255.0F),
    };
}

[[nodiscard]] constexpr EditorTheme MakeVerthDarkTheme() {
    return EditorTheme{
        .background = Color(0.050F, 0.058F, 0.072F),
        .chrome = Color(0.085F, 0.095F, 0.110F),
        .panel = Color(0.108F, 0.120F, 0.140F),
        .strip = Color(0.072F, 0.082F, 0.095F),
        .tabInactive = Color(0.082F, 0.092F, 0.108F),
        .tabActive = Color(0.140F, 0.155F, 0.180F),
        .menuBar = Color(0.068F, 0.077F, 0.092F),
        .toolbar = Color(0.075F, 0.084F, 0.100F),
        .toolbarButton = Color(0.100F, 0.112F, 0.132F),
        .splitter = Color(0.045F, 0.052F, 0.065F),
        .gridLine = Color(0.170F, 0.188F, 0.220F),
        .borderChrome = Color(0.200F, 0.225F, 0.260F),
        .borderPanel = Color(0.165F, 0.185F, 0.215F),
        .textPrimary = Color(0.920F, 0.940F, 0.970F),
        .textSecondary = Color(0.650F, 0.700F, 0.780F),
        .textDisabled = Color(0.420F, 0.460F, 0.520F),
        .accent = Color(0.950F, 0.750F, 0.200F),
    };
}

} // namespace kb::editor
