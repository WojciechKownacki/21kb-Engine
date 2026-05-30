#pragma once

#include "docking/FloatingWindowRegistry.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <string>

namespace kb::editor {

class FloatingWindowCreator {
public:
#if defined(_WIN32)
    [[nodiscard]] static bool Create(
        FloatingWindowRegistry& registry,
        HINSTANCE instance,
        HWND owner,
        const wchar_t* windowClassName,
        std::uint32_t panelId,
        const std::string& titleText,
        const DockRect& rect);
#endif
};

} // namespace kb::editor
