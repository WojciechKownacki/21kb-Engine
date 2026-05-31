#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/HeroIconKind.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <filesystem>
#include <string_view>

namespace kb::editor {

#if defined(_WIN32)

class ProjectFilesPanelDrawing {
public:
    ProjectFilesPanelDrawing() = delete;

    [[nodiscard]] static COLORREF Color(EditorColor color);
    [[nodiscard]] static int RectWidth(const RECT& rect) noexcept;
    [[nodiscard]] static int RectHeight(const RECT& rect) noexcept;
    [[nodiscard]] static RECT Inset(RECT rect, int x, int y) noexcept;
    [[nodiscard]] static COLORREF Blend(COLORREF a, COLORREF b, int percentB) noexcept;
    [[nodiscard]] static COLORREF FolderColor(bool selected) noexcept;
    [[nodiscard]] static bool SameVirtualPath(const std::filesystem::path& left, const std::filesystem::path& right);

    static void DrawLabel(HDC dc, RECT rect, const char* text, COLORREF color);
    static void DrawCenteredLabel(HDC dc, RECT rect, const char* text, COLORREF color);
    static void DrawTextWithFont(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize, int weight, UINT flags);
    static void DrawHairline(HDC dc, RECT rect, COLORREF color);
    static void DrawEditField(HDC dc, RECT rect, const EditorTheme& theme, std::string_view value);
    static void DrawCenteredEditField(HDC dc, RECT rect, const EditorTheme& theme, std::string_view value);
    static void DrawIconButton(HDC dc, RECT rect, const EditorTheme& theme, HeroIconKind icon, bool active);
    static void DrawTextButton(HDC dc, RECT rect, const EditorTheme& theme, const char* text, bool active);
    static void DrawDisclosureTriangle(HDC dc, RECT rect, COLORREF color, bool expanded);
    static void DrawIconWithShadow(HDC dc, RECT icon, HeroIconKind kind, COLORREF color, int strokeWidth = 2);
};

#endif

} // namespace kb::editor
