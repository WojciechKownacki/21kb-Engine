#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

class FloatingWindowRectReader {
public:
    FloatingWindowRectReader() = delete;

#if defined(_WIN32)
    [[nodiscard]] static std::optional<DockRect> Read(HWND window);
#endif
};

} // namespace kb::editor
