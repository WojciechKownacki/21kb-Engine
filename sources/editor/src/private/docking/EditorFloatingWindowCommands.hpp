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

class EditorFloatingWindowCommands {
public:
#if defined(_WIN32)
    EditorFloatingWindowCommands(FloatingWindowRegistry& registry, HINSTANCE instance, HWND owner) noexcept;

    [[nodiscard]] bool Create(std::uint32_t panelId, const std::string& title, const DockRect& rect);
    void Destroy(std::uint32_t panelId);
#endif

private:
#if defined(_WIN32)
    FloatingWindowRegistry& registry_;
    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
#endif
};

} // namespace kb::editor
