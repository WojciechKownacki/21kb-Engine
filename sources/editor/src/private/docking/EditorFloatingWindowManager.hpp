#pragma once

#include "docking/FloatingWindowRegistry.hpp"
#include "docking/EditorFloatingWindowCommands.hpp"
#include "docking/EditorFloatingWindowLifecycle.hpp"
#include "docking/EditorFloatingWindowQueries.hpp"
#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorDockModel;

// The native windows of the floating tab groups. The dock model owns the groups; a window
// only shows the group it is registered for, so what it shows is always what the model holds.
class EditorFloatingWindowManager {
public:
    static constexpr const wchar_t* WindowClassName = L"KBEditorFloatingWindow";
    static constexpr int DragWidth = 640;
    static constexpr int DragHeight = 480;

#if defined(_WIN32)
    [[nodiscard]] EditorFloatingWindowQueries Queries() const noexcept;
    [[nodiscard]] EditorFloatingWindowCommands Commands() noexcept;
    [[nodiscard]] EditorFloatingWindowLifecycle Lifecycle() noexcept;
#endif

private:
#if defined(_WIN32)
    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    const EditorMetrics* metrics_ = nullptr;
    const EditorDockModel* dockModel_ = nullptr;
    FloatingWindowRegistry registry_;
#endif
};

} // namespace kb::editor
