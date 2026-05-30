#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <string>

namespace kb::editor {

class FloatingWindowFactory {
public:
    FloatingWindowFactory() = delete;

#if defined(_WIN32)
    [[nodiscard]] static HWND Create(HINSTANCE instance, HWND owner, const wchar_t* className, const std::string& titleText, const DockRect& rect) noexcept;
#endif
};

} // namespace kb::editor
