#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>

namespace kb::editor {

struct DockPointerDrag {
    DockHitKind kind = DockHitKind::None;
    std::uint32_t panelId = 0;
    int offsetX = 0;
    int offsetY = 0;
    int startX = 0;
    int startY = 0;
    std::uint32_t splitterNodeId = 0;
    std::uint32_t sourceLeafId = 0;
    std::uint32_t sourceTabIndex = 0;
    DockRect sourceStrip{};
#if defined(_WIN32)
    HWND sourceWindow = nullptr;
#endif
    bool detached = false;
    bool manualTabDrag = false;
};

} // namespace kb::editor
