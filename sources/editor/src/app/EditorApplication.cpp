#include "kb/editor/EditorApplication.hpp"

#if defined(_WIN32)
#include "app/EditorWindowMessageRouter.hpp"
#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "platform/win32/EditorMainWindow.hpp"
#include "platform/win32/EditorWindowClassRegistry.hpp"
#include "rendering/EditorGdiRenderer.hpp"
#include "scene/EditorHierarchySelectionController.hpp"
#include "scene/EditorSceneContext.hpp"

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#include <memory>

namespace kb::editor {
namespace {

constexpr wchar_t kWindowTitle[] = L"21kb Engine";
constexpr int kInitialWindowWidth = 1600;
constexpr int kInitialWindowHeight = 960;

} // namespace

struct EditorApplication::Impl {
    bool Initialize();
    void Run();
    void Shutdown();

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    LRESULT HandleWindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    EditorWindowClassRegistry windowClasses;
    EditorDockModel dockModel;
    EditorSceneContext sceneContext;
    EditorTheme theme = MakeEditorDarkTheme();
    EditorMetrics metrics;
    EditorGdiRenderer renderer;
    EditorFloatingWindowManager floatingWindows;
    EditorDockController dockController;
    EditorHierarchySelectionController hierarchySelection;
    bool running = false;
};

EditorApplication::EditorApplication()
    : impl_(std::make_unique<Impl>()) {
}

EditorApplication::~EditorApplication() {
    Shutdown();
}

bool EditorApplication::Initialize() {
    return impl_->Initialize();
}

void EditorApplication::Run() {
    impl_->Run();
}

void EditorApplication::Shutdown() {
    if (impl_ != nullptr) {
        impl_->Shutdown();
    }
}

bool EditorApplication::Impl::Initialize() {
    instance = GetModuleHandleW(nullptr);

    if (!windowClasses.Register(instance, &EditorApplication::Impl::WindowProc)) {
        return false;
    }

    window = EditorMainWindow::Create(instance, EditorWindowClassRegistry::MainWindowClassName, kWindowTitle, kInitialWindowWidth, kInitialWindowHeight, this);
    if (window == nullptr) {
        windowClasses.Unregister();
        return false;
    }

    EditorMainWindow::EnableDarkMode(window);
    floatingWindows.Configure(instance, window, metrics);
    dockController.Configure(window, dockModel, floatingWindows, metrics);

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    running = true;
    return true;
}

void EditorApplication::Impl::Run() {
    MSG message{};
    while (running && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void EditorApplication::Impl::Shutdown() {
    floatingWindows.Shutdown();

    if (window != nullptr) {
        DestroyWindow(window);
        window = nullptr;
    }

    windowClasses.Unregister();
    instance = nullptr;

    running = false;
}

LRESULT CALLBACK EditorApplication::Impl::WindowProc(HWND windowHandle, UINT message, WPARAM wparam, LPARAM lparam) {
    Impl* app = nullptr;

    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = static_cast<Impl*>(createStruct->lpCreateParams);
        if (app != nullptr && app->window == nullptr) {
            app->window = windowHandle;
        }
        if (app != nullptr) {
            SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
    } else {
        app = reinterpret_cast<Impl*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
    }

    if (app != nullptr) {
        return app->HandleWindowMessage(windowHandle, message, wparam, lparam);
    }

    return DefWindowProcW(windowHandle, message, wparam, lparam);
}

LRESULT EditorApplication::Impl::HandleWindowMessage(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam) {
    return EditorWindowMessageRouter{
        window,
        running,
        dockModel,
        sceneContext,
        theme,
        metrics,
        renderer,
        floatingWindows,
        dockController,
        hierarchySelection,
    }.Handle(messageWindow, message, wparam, lparam);
}

} // namespace kb::editor

#else

#include <memory>

namespace kb::editor {

struct EditorApplication::Impl {
};

EditorApplication::EditorApplication()
    : impl_(std::make_unique<Impl>()) {
}

EditorApplication::~EditorApplication() = default;

bool EditorApplication::Initialize() {
    return false;
}

void EditorApplication::Run() {
}

void EditorApplication::Shutdown() {
}

} // namespace kb::editor

#endif
