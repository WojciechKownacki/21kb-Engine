#pragma once

#include "docking/FloatingWindowRegistry.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorDockModel;

class EditorFloatingWindowLifecycle {
public:
#if defined(_WIN32)
    EditorFloatingWindowLifecycle(FloatingWindowRegistry& registry, HINSTANCE& instance, HWND& owner, const EditorMetrics*& metrics, const EditorDockModel*& dockModel) noexcept;

    void Configure(HINSTANCE instance, HWND owner, const EditorMetrics& metrics, const EditorDockModel& dockModel) noexcept;
    void Shutdown();
    void OnDestroyed(HWND window);
#endif

private:
#if defined(_WIN32)
    FloatingWindowRegistry& registry_;
    HINSTANCE& instance_;
    HWND& owner_;
    const EditorMetrics*& metrics_;
    const EditorDockModel*& dockModel_;
#endif
};

} // namespace kb::editor
