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
    int menuHeight = 24;
    int toolbarHeight = 48;
    int floatingChromeHeight = 24;
    int floatingControlWidth = 34;
    int floatingResizeBorder = 6;
    int tabStripHeight = 28;
    int tabMinWidth = 92;
    int tabWidth = 156;
    int splitterSize = 6;
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
        .background = EditorColor{ 9, 12, 18 },
        .chrome = EditorColor{ 13, 17, 25 },
        .panel = EditorColor{ 18, 23, 32 },
        .strip = EditorColor{ 22, 27, 37 },
        .tabInactive = EditorColor{ 18, 23, 32 },
        .tabActive = EditorColor{ 31, 37, 50 },
        .menuBar = EditorColor{ 10, 13, 20 },
        .toolbar = EditorColor{ 16, 21, 30 },
        .toolbarButton = EditorColor{ 29, 35, 47 },
        .splitter = EditorColor{ 5, 8, 13 },
        .gridLine = EditorColor{ 37, 44, 58 },
        .borderChrome = EditorColor{ 36, 43, 56 },
        .borderPanel = EditorColor{ 49, 58, 75 },
        .textPrimary = EditorColor{ 235, 238, 247 },
        .textSecondary = EditorColor{ 184, 192, 211 },
        .textDisabled = EditorColor{ 105, 115, 137 },
        .accent = EditorColor{ 117, 92, 255 },
    };
}

} // namespace kb::editor
