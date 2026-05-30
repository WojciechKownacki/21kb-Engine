#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace kb::editor {

class EditorFloatingWindowManager {
public:
    static constexpr const wchar_t* WindowClassName = L"KBEditorFloatingWindow";
    static constexpr int DragWidth = 400;
    static constexpr int DragHeight = 300;
    static constexpr int StripCursorY = 12;

#if defined(_WIN32)
    struct ResizeEvent {
        std::uint32_t panelId = 0;
        int width = 0;
        int height = 0;
    };

    void Configure(HINSTANCE instance, HWND owner, const EditorMetrics& metrics) noexcept;
    void Shutdown();

    [[nodiscard]] bool IsFloatingWindow(HWND window) const noexcept;
    [[nodiscard]] std::uint32_t PanelId(HWND window) const noexcept;
    [[nodiscard]] HWND WindowForPanel(std::uint32_t panelId) const noexcept;
    [[nodiscard]] LRESULT HitTest(HWND window, LPARAM lparam) const;

    void OnDestroyed(HWND window);
    [[nodiscard]] std::optional<ResizeEvent> OnResized(HWND window, int width, int height) const noexcept;
    [[nodiscard]] bool Create(std::uint32_t panelId, const std::string& title, const DockRect& rect);
    void Destroy(std::uint32_t panelId);
    [[nodiscard]] std::optional<DockRect> RectForPanel(std::uint32_t panelId) const;
#endif

private:
#if defined(_WIN32)
    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    const EditorMetrics* metrics_ = nullptr;
    std::unordered_map<std::uint32_t, HWND> panelToWindow_;
    std::unordered_map<HWND, std::uint32_t> windowToPanel_;
#endif
};

} // namespace kb::editor
