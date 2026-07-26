#include "app/EditorApplicationLifecycle.hpp"

#if defined(_WIN32)
#include "app/EditorApplicationWindowProc.hpp"
#include "platform/win32/EditorMainWindow.hpp"
#include "platform/win32/EditorWindowClassRegistry.hpp"

#include <string>
#include <string_view>

namespace kb::editor {
namespace {

constexpr wchar_t kWindowTitle[] = L"21kb Engine";
constexpr int kInitialWindowWidth = 1600;
constexpr int kInitialWindowHeight = 960;

} // namespace

bool EditorApplicationLifecycle::Initialize(EditorApplicationState& state) {
    state.instance = GetModuleHandleW(nullptr);

    if (!state.windowClasses.Register(state.instance, &EditorApplicationWindowProc::Handle)) {
        return false;
    }

    state.window = EditorMainWindow::Create(state.instance, EditorWindowClassRegistry::MainWindowClassName, kWindowTitle, kInitialWindowWidth, kInitialWindowHeight, &state);
    if (state.window == nullptr) {
        state.windowClasses.Unregister();
        return false;
    }
    if (RegisterTouchWindow(state.window, 0U) == 0) {
        DestroyWindow(state.window);
        state.window = nullptr;
        state.windowClasses.Unregister();
        return false;
    }

    EditorMainWindow::EnableDarkMode(state.window);
    state.sceneViewport.Configure(state.instance, state.window, &state.renderBackendSettings);
    state.sceneViewport.SetErrorReporter([&state](std::string_view message) {
        state.sceneContext.Console().Error("Renderer", std::string{ message });
    });
    state.sceneViewport.SetAaTraceReporter([&state](std::string_view message) {
        state.sceneContext.Console().Info("AA", std::string{ message });
    });
    state.floatingWindows.Lifecycle().Configure(state.instance, state.window, state.metrics);
    state.dockController.Configure(state.window, state.dockModel, state.floatingWindows, state.metrics);

    ShowWindow(state.window, SW_SHOWMAXIMIZED);
    UpdateWindow(state.window);

    state.running = true;
    return true;
}

void EditorApplicationLifecycle::Shutdown(EditorApplicationState& state) {
    static_cast<void>(state.sceneContext.RestorePlayModeSceneSession());
    static_cast<void>(state.sceneContext.SaveDirtySceneDocument("application shutdown"));
    state.sceneViewport.Shutdown();
    state.floatingWindows.Lifecycle().Shutdown();

    if (state.window != nullptr) {
        static_cast<void>(UnregisterTouchWindow(state.window));
        DestroyWindow(state.window);
        state.window = nullptr;
    }

    state.windowClasses.Unregister();
    state.instance = nullptr;
    state.running = false;
}

} // namespace kb::editor

#endif
