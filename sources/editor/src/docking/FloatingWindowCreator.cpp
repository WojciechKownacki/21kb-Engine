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
    ShowWindow(floating, SW_SHOW);
    UpdateWindow(floating);
    return true;
}

} // namespace kb::editor

#endif
