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
    EditorColor header;
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
    // The single top bar that holds the menus, the transport controls and Save.
    int menuHeight = 48;
    // Console error and warning counts along the window's bottom edge.
    int statusBarHeight = 24;
    int floatingChromeHeight = 24;
    int floatingControlWidth = 34;
    int floatingResizeBorder = 6;
    int tabStripHeight = 28;
    int tabMinWidth = 92;
    int tabWidth = 156;
    int splitterSize = 3;
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
        .background = EditorColor{ 21, 21, 21 },
        .chrome = EditorColor{ 15, 15, 15 },
        .panel = EditorColor{ 36, 36, 36 },
        .strip = EditorColor{ 21, 21, 21 },
        .tabInactive = EditorColor{ 21, 21, 21 },
        .tabActive = EditorColor{ 36, 36, 36 },
        .menuBar = EditorColor{ 21, 21, 21 },
        .toolbar = EditorColor{ 36, 36, 36 },
        .toolbarButton = EditorColor{ 56, 56, 56 },
        .header = EditorColor{ 47, 47, 47 },
        .splitter = EditorColor{ 21, 21, 21 },
        .gridLine = EditorColor{ 47, 47, 47 },
        .borderChrome = EditorColor{ 47, 47, 47 },
        .borderPanel = EditorColor{ 56, 56, 56 },
        .textPrimary = EditorColor{ 226, 226, 226 },
        .textSecondary = EditorColor{ 192, 192, 192 },
        .textDisabled = EditorColor{ 128, 128, 128 },
        .accent = EditorColor{ 0, 112, 224 },
    };
}

} // namespace kb::editor
