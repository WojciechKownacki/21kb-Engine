#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/HeroIconKind.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <string_view>

namespace kb::editor {

#if defined(_WIN32)

enum class EditorDialogButtonTone {
    Neutral,
    Primary,
    Destructive,
};

struct EditorDialogHeaderDescriptor {
    RECT bounds{};
    RECT closeButton{};
    std::string_view title{};
    std::string_view description{};
    HeroIconKind icon = HeroIconKind::RectangleGroup;
    bool showIcon = false;
    bool closeHovered = false;
};

struct EditorDialogChromeLayout {
    RECT bounds{};
    RECT titleStrip{};
    RECT activeTab{};
    RECT icon{};
    RECT title{};
    RECT closeButton{};
    RECT contextStrip{};
    RECT description{};
};

struct EditorDialogListRowDescriptor {
    RECT bounds{};
    std::string_view title{};
    std::string_view subtitle{};
    HeroIconKind icon = HeroIconKind::Cube;
    bool selected = false;
    bool hovered = false;
    bool enabled = true;
    bool showIcon = true;
};

struct EditorDialogListRowLayout {
    RECT bounds{};
    RECT selectionMarker{};
    RECT icon{};
    RECT title{};
    RECT subtitle{};
};

class EditorDialogStyle final {
public:
    EditorDialogStyle() = delete;

    static constexpr int TitleBarHeight = 30;
    static constexpr int ContextBarHeight = 34;
    static constexpr int HeaderHeight = TitleBarHeight + ContextBarHeight;
    static constexpr int FooterHeight = 30;
    static constexpr int ListRowHeight = 36;
    static constexpr int MenuRowHeight = 26;
    static constexpr int ControlHeight = 24;
    static constexpr int Padding = 10;
    static constexpr int CloseButtonSize = 22;
    static constexpr int ScrollbarWidth = 8;

    [[nodiscard]] static COLORREF Color(EditorColor color) noexcept;
    [[nodiscard]] static COLORREF Blend(COLORREF first, COLORREF second, int secondPercent) noexcept;
    [[nodiscard]] static EditorDialogChromeLayout ResolveHeader(
        const EditorDialogHeaderDescriptor& descriptor) noexcept;
    [[nodiscard]] static EditorDialogListRowLayout ResolveListRow(
        const EditorDialogListRowDescriptor& descriptor) noexcept;

    static void PaintSurface(HDC dc, const RECT& bounds, const EditorTheme& theme);
    static void PaintTitleBar(HDC dc, const EditorTheme& theme, const EditorDialogHeaderDescriptor& descriptor);
    static void PaintHeader(HDC dc, const EditorTheme& theme, const EditorDialogHeaderDescriptor& descriptor);
    static void PaintToolbar(HDC dc, const RECT& bounds, const EditorTheme& theme);
    static void PaintFooter(HDC dc, const RECT& bounds, const EditorTheme& theme);
    static void PaintButton(
        HDC dc,
        const RECT& bounds,
        const EditorTheme& theme,
        std::string_view label,
        EditorDialogButtonTone tone = EditorDialogButtonTone::Neutral,
        bool hovered = false,
        bool enabled = true);
    static void PaintField(
        HDC dc,
        const RECT& bounds,
        const EditorTheme& theme,
        std::string_view value,
        bool focused = false,
        bool placeholder = false);
    static void PaintListFrame(HDC dc, const RECT& bounds, const EditorTheme& theme);
    static void PaintListRow(HDC dc, const EditorTheme& theme, const EditorDialogListRowDescriptor& descriptor);
    static void PaintMenuRow(
        HDC dc,
        const RECT& bounds,
        const EditorTheme& theme,
        std::string_view label,
        HeroIconKind icon,
        bool hovered,
        bool enabled = true,
        bool showIcon = true);
    static void PaintCheckbox(
        HDC dc,
        const RECT& bounds,
        const EditorTheme& theme,
        bool checked,
        bool enabled = true);
    static void PaintScrollbar(
        HDC dc,
        const RECT& track,
        const RECT& thumb,
        const EditorTheme& theme,
        bool dragging = false);
    static void PaintDivider(HDC dc, const RECT& bounds, const EditorTheme& theme);
    static void PaintText(
        HDC dc,
        RECT bounds,
        std::string_view text,
        COLORREF color,
        int pointSize = 12,
        int weight = FW_NORMAL,
        UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
};

#endif

} // namespace kb::editor
