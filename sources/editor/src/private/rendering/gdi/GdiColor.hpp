#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class GdiColor {
public:
    GdiColor() = delete;

    [[nodiscard]] static constexpr COLORREF ToColorRef(EditorColor color) {
        return RGB(color.r, color.g, color.b);
    }
};

#endif

} // namespace kb::editor
