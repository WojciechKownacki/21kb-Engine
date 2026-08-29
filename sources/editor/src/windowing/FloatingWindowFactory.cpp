#include "windowing/FloatingWindowFactory.hpp"

#if defined(_WIN32)
#include "platform/win32/Win32ErrorReporter.hpp"

#include <dwmapi.h>

namespace kb::editor {

HWND FloatingWindowFactory::Create(HINSTANCE instance, HWND owner, const wchar_t* className, const std::string& titleText, const DockRect& rect) noexcept {
    const std::wstring title(titleText.begin(), titleText.end());
    const LONG_PTR ownerApplication = GetWindowLongPtrW(owner, GWLP_USERDATA);
    HWND floating = CreateWindowExW(
        ExtendedStyle,
        className,
        title.c_str(),
        Style,
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

    // DWMWA_USE_IMMERSIVE_DARK_MODE
    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(floating, 20, &darkMode, sizeof(darkMode));
    // DWMWA_BORDER_COLOR / DWMWA_COLOR_NONE: the editor draws this window's outline
    // itself, and the system's own border would sit on top of it as a lighter line
    // along the top edge. Older builds do not know the attribute and simply say so.
    const COLORREF noBorder = 0xFFFFFFFE;
    static_cast<void>(DwmSetWindowAttribute(floating, 34, &noBorder, sizeof(noBorder)));
    return floating;
}

} // namespace kb::editor

#endif
