#include "windowing/FloatingWindowFactory.hpp"

#if defined(_WIN32)
#include "platform/win32/Win32ErrorReporter.hpp"

#include <dwmapi.h>

namespace kb::editor {

HWND FloatingWindowFactory::Create(HINSTANCE instance, HWND owner, const wchar_t* className, const std::string& titleText, const DockRect& rect) noexcept {
    // WS_THICKFRAME is what lets DefWindowProc act on the HTLEFT/HTBOTTOMRIGHT... codes that
    // FloatingWindowHitTestResolver already returns for the border strip; without it the edges are inert
    // and an undocked panel is stuck at its initial size.
    constexpr DWORD floatingStyle = WS_POPUP | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    const std::wstring title(titleText.begin(), titleText.end());
    const LONG_PTR ownerApplication = GetWindowLongPtrW(owner, GWLP_USERDATA);
    HWND floating = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        className,
        title.c_str(),
        floatingStyle,
        rect.x,
        rect.y,
        rect.width,
        rect.height,
        owner,
        nullptr,
        instance,
        reinterpret_cast<void*>(ownerApplication));

    if (floating == nullptr) {
        Win32ErrorReporter::PrintLastError("CreateWindowExW floating");
        return nullptr;
    }

    SetWindowLongPtrW(floating, GWLP_USERDATA, ownerApplication);

    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(floating, 20, &darkMode, sizeof(darkMode));
    return floating;
}

} // namespace kb::editor

#endif
