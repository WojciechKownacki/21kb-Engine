#include "docking/FloatingWindowRectReader.hpp"

#if defined(_WIN32)

namespace kb::editor {

std::optional<DockRect> FloatingWindowRectReader::Read(HWND window) {
    if (window == nullptr) {
        return std::nullopt;
    }

    RECT rect{};
    GetWindowRect(window, &rect);
    return DockRect{
        .x = rect.left,
        .y = rect.top,
        .width = rect.right - rect.left,
        .height = rect.bottom - rect.top,
    };
}

} // namespace kb::editor

#endif
