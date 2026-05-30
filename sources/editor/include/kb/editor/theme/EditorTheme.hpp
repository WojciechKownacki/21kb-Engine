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
    int floatingChromeHeight = 24;
    int floatingControlWidth = 34;
    int floatingResizeBorder = 6;
    int tabStripHeight = 28;
    int tabMinWidth = 92;
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

[[nodiscard]] constexpr EditorTheme MakeEditorDarkTheme() {
    return EditorTheme{
        .background = Color(0.059F, 0.063F, 0.071F),
        .chrome = Color(0.078F, 0.084F, 0.094F),
        .panel = Color(0.122F, 0.127F, 0.137F),
        .strip = Color(0.086F, 0.090F, 0.102F),
        .tabInactive = Color(0.098F, 0.104F, 0.116F),
        .tabActive = Color(0.180F, 0.188F, 0.204F),
        .menuBar = Color(0.067F, 0.071F, 0.080F),
        .toolbar = Color(0.082F, 0.086F, 0.098F),
        .toolbarButton = Color(0.118F, 0.125F, 0.141F),
        .splitter = Color(0.043F, 0.047F, 0.055F),
        .gridLine = Color(0.164F, 0.176F, 0.196F),
        .borderChrome = Color(0.204F, 0.216F, 0.235F),
        .borderPanel = Color(0.255F, 0.267F, 0.286F),
        .textPrimary = Color(0.910F, 0.925F, 0.945F),
        .textSecondary = Color(0.670F, 0.700F, 0.745F),
        .textDisabled = Color(0.455F, 0.480F, 0.525F),
        .accent = Color(0.900F, 0.680F, 0.180F),
    };
}

} // namespace kb::editor
