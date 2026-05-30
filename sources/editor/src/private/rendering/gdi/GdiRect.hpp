#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class GdiRect {
public:
    GdiRect() = delete;

    [[nodiscard]] static RECT Inset(RECT rect, int amount) noexcept;
    [[nodiscard]] static RECT FromDockRect(const DockRect& rect) noexcept;
};

#endif

} // namespace kb::editor
