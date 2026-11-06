#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

namespace kb::editor {

// Choose the closest work area and keep the entire window, including its
// controls, reachable after a display is removed or its scale changes.
inline DockRect RecoverDisplayRect(DockRect rect, std::span<const DockRect> workAreas) {
    if (workAreas.empty()) return rect;
    const DockRect* nearest = &workAreas.front();
    std::int64_t distance = std::numeric_limits<std::int64_t>::max();
    for (const auto& area : workAreas) {
        const auto dx = static_cast<std::int64_t>(rect.x) -
            std::clamp(rect.x, area.x, area.x + std::max(0, area.width - 1));
        const auto dy = static_cast<std::int64_t>(rect.y) -
            std::clamp(rect.y, area.y, area.y + std::max(0, area.height - 1));
        const auto candidate = dx * dx + dy * dy;
        if (candidate < distance) { distance = candidate; nearest = &area; }
    }
    rect.width = std::clamp(rect.width, 1, std::max(1, nearest->width));
    rect.height = std::clamp(rect.height, 1, std::max(1, nearest->height));
    rect.x = std::clamp(rect.x, nearest->x, nearest->x + nearest->width - rect.width);
    rect.y = std::clamp(rect.y, nearest->y, nearest->y + nearest->height - rect.height);
    return rect;
}

} // namespace kb::editor

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <vector>

namespace kb::editor {
inline DockRect RecoverNativeDisplayRect(DockRect rect) {
    std::vector<DockRect> areas;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            auto& result = *reinterpret_cast<std::vector<DockRect>*>(data);
            result.push_back({info.rcWork.left, info.rcWork.top,
                info.rcWork.right - info.rcWork.left, info.rcWork.bottom - info.rcWork.top});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&areas));
    return RecoverDisplayRect(rect, areas);
}

inline void RecoverNativeWindow(HWND window, const RECT* suggested = nullptr) {
    RECT rect{};
    if (suggested) rect = *suggested;
    else if (!GetWindowRect(window, &rect)) return;
    const auto recovered = RecoverNativeDisplayRect({rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top});
    SetWindowPos(window, nullptr, recovered.x, recovered.y, recovered.width, recovered.height,
        SWP_NOACTIVATE | SWP_NOZORDER);
}
} // namespace kb::editor
#endif
