#include "docking/FloatingWindowCreator.hpp"

#if defined(_WIN32)
#include "windowing/FloatingWindowFactory.hpp"

namespace kb::editor {

bool FloatingWindowCreator::Create(
    FloatingWindowRegistry& registry,
    HINSTANCE instance,
    HWND owner,
    const wchar_t* windowClassName,
    std::uint32_t panelId,
    const std::string& titleText,
    const DockRect& rect) {
    if (registry.ContainsPanel(panelId)) {
        return false;
    }

    HWND floating = FloatingWindowFactory::Create(instance, owner, windowClassName, titleText, rect);
    if (floating == nullptr) {
        return false;
    }

    registry.Add(panelId, floating);
    // Only now is the window known to be a floating one, so ask Windows to work out
    // the frame again: the first pass during creation still used the default one.
    SetWindowPos(
        floating, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(floating, SW_SHOW);
    UpdateWindow(floating);
    return true;
}

} // namespace kb::editor

#endif
