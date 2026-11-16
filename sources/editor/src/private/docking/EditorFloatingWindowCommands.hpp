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

class EditorDockModel;

class EditorFloatingWindowCommands {
public:
#if defined(_WIN32)
    EditorFloatingWindowCommands(FloatingWindowRegistry& registry, HINSTANCE instance, HWND owner, const EditorDockModel* dockModel) noexcept;

    // Opens the window of the group the panel is in; false when the panel is not in a floating
    // window or the window is open already.
    [[nodiscard]] bool Create(std::uint32_t panelId, const std::string& title, const DockRect& rect);
    // Closes the windows whose group the model no longer has: the last tab was closed or docked.
    void DestroyOrphaned();
#endif

private:
#if defined(_WIN32)
    FloatingWindowRegistry& registry_;
    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    const EditorDockModel* dockModel_ = nullptr;
#endif
};

} // namespace kb::editor
