#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <string>

namespace kb::editor {

class FloatingWindowFactory {
public:
    FloatingWindowFactory() = delete;

#if defined(_WIN32)
    // WS_THICKFRAME is what lets DefWindowProc act on the HTLEFT/HTBOTTOMRIGHT codes
    // that FloatingWindowHitTestResolver returns for the border strip; without it the
    // edges are inert and an undocked panel is stuck at its initial size. The frame it
    // brings with it is taken back by EditorFloatingWindowFrame, so the editor draws
    // the whole window itself.
    static constexpr DWORD Style =
        WS_POPUP | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    static constexpr DWORD ExtendedStyle = WS_EX_TOOLWINDOW;

    [[nodiscard]] static HWND Create(HINSTANCE instance, HWND owner, const wchar_t* className, const std::string& titleText, const DockRect& rect) noexcept;
#endif
};

} // namespace kb::editor
