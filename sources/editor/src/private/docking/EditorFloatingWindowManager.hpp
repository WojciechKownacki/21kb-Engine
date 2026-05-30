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

class EditorFloatingWindowManager {
public:
    static constexpr const wchar_t* WindowClassName = L"KBEditorFloatingWindow";
    static constexpr int DragWidth = 400;
    static constexpr int DragHeight = 300;
    static constexpr int StripCursorY = 12;

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
    FloatingWindowRegistry registry_;
#endif
};

} // namespace kb::editor
