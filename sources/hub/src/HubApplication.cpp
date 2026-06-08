#include "HubApplication.hpp"

#include "HubGeometry.hpp"
#include "HubProjectActions.hpp"
#include "HubProjectFilters.hpp"
#include "HubProjectStore.hpp"
#include "HubRenderer.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <dwmapi.h>
#include <objbase.h>
#include <windowsx.h>
#endif

namespace kb::hub {
namespace {

constexpr const wchar_t* kWindowClassName = L"KB21HubWindow";
constexpr int kDefaultWidth = 1250;
constexpr int kDefaultHeight = 760;
constexpr int kProjectRowHeight = 98;

[[nodiscard]] int MouseX(LPARAM value) noexcept {
    return GET_X_LPARAM(value);
}

[[nodiscard]] int MouseY(LPARAM value) noexcept {
    return GET_Y_LPARAM(value);
}

void EnablePerMonitorDpiAwareness() noexcept {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return;
    }

    using SetDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const auto setDpiAwarenessContext = reinterpret_cast<SetDpiAwarenessContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setDpiAwarenessContext != nullptr) {
        static_cast<void>(setDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    }
}

} // namespace

bool HubApplication::Initialize(HINSTANCE instance, int showCommand) {
    instance_ = instance;
    EnablePerMonitorDpiAwareness();
    const HRESULT comResult = OleInitialize(nullptr);
    comInitialized_ = SUCCEEDED(comResult);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = HubApplication::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    RECT rect{ 0, 0, kDefaultWidth, kDefaultHeight };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        L"21kb Hub",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        if (comInitialized_) {
            OleUninitialize();
            comInitialized_ = false;
        }
        return false;
    }

    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(window_, 20, &darkMode, sizeof(darkMode));
    RefreshProjects();
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    return true;
}

int HubApplication::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK HubApplication::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    HubApplication* app = nullptr;
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        app = static_cast<HubApplication*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<HubApplication*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    if (app == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return app->HandleMessage(window, message, wParam, lParam);
}

LRESULT HubApplication::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        state_.width = LOWORD(lParam);
        state_.height = HIWORD(lParam);
        ClampScroll();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 980;
        info->ptMinTrackSize.y = 640;
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        HubRenderer::Paint(window, dc, state_);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        state_.scrollY -= delta / 3;
        ClampScroll();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        const int projectIndex = HubRenderer::ProjectIndexAt(state_, MouseX(lParam), MouseY(lParam));
        if (projectIndex >= 0) {
            state_.selectedProject = projectIndex;
            LaunchSelectedProject(window);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const int x = MouseX(lParam);
        const int y = MouseY(lParam);
        if (Contains(state_.layout.newProjectButton, x, y)) {
            state_.searchFocused = false;
            OpenCreateDialog(window);
        } else if (Contains(state_.layout.openProjectButton, x, y)) {
            state_.searchFocused = false;
            OpenExistingProject(window);
        } else if (Contains(state_.layout.launchProjectButton, x, y)) {
            state_.searchFocused = false;
            LaunchSelectedProject(window);
        } else if (Contains(state_.layout.removeProjectButton, x, y)) {
            state_.searchFocused = false;
            RemoveSelectedProject(window);
        } else if (Contains(state_.layout.searchField, x, y)) {
            state_.searchFocused = true;
        } else {
            state_.searchFocused = false;
            const int projectIndex = HubRenderer::ProjectIndexAt(state_, x, y);
            if (projectIndex >= 0) {
                state_.selectedProject = projectIndex;
            }
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_CHAR:
        if (state_.searchFocused) {
            const wchar_t character = static_cast<wchar_t>(wParam);
            if (character >= 32) {
                state_.searchQuery.push_back(character);
                ClampScroll();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }
        return 0;
    case WM_KEYDOWN:
        if (state_.searchFocused) {
            if (wParam == VK_ESCAPE) {
                state_.searchFocused = false;
            } else if (wParam == VK_BACK && !state_.searchQuery.empty()) {
                state_.searchQuery.pop_back();
                ClampScroll();
            } else if (wParam == VK_RETURN) {
                state_.searchFocused = false;
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_RETURN) {
            LaunchSelectedProject(window);
            return 0;
        }
        if (wParam == VK_DELETE) {
            RemoveSelectedProject(window);
            return 0;
        }
        return 0;
    case WM_DESTROY:
        HubProjectStore::Save(state_.projects);
        if (comInitialized_) {
            OleUninitialize();
            comInitialized_ = false;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

void HubApplication::OpenCreateDialog(HWND window) {
    const std::optional<std::filesystem::path> projectFile = HubProjectActions::BrowseNewProjectFile(window);
    if (!projectFile.has_value()) {
        return;
    }

    const HubCreateProjectResult result = HubProjectActions::CreateProjectFile(*projectFile);
    if (!result.succeeded) {
        state_.status = result.error;
        MessageBoxW(window, result.error.c_str(), L"21kb Hub", MB_ICONERROR | MB_OK);
        InvalidateRect(window, nullptr, FALSE);
        return;
    }

    HubProjectStore::AddOrPromote(state_.projects, result.projectFile);
    HubProjectStore::Save(state_.projects);
    state_.selectedProject = 0;
    state_.scrollY = 0;
    state_.status = L"Project created.";
    InvalidateRect(window, nullptr, FALSE);
}

void HubApplication::OpenExistingProject(HWND window) {
    const std::optional<std::filesystem::path> projectFile = HubProjectActions::BrowseProjectFile(window);
    if (!projectFile.has_value()) {
        return;
    }

    HubProjectStore::AddOrPromote(state_.projects, *projectFile);
    HubProjectStore::Save(state_.projects);
    state_.selectedProject = 0;
    state_.scrollY = 0;
    state_.status = L"Project added.";
}

void HubApplication::LaunchSelectedProject(HWND window) {
    if (state_.selectedProject < 0 || state_.selectedProject >= static_cast<int>(state_.projects.size())) {
        return;
    }

    const HubProjectItem& project = state_.projects[static_cast<std::size_t>(state_.selectedProject)];
    if (!project.valid) {
        state_.status = L"Selected project is invalid.";
        return;
    }

    std::wstring error;
    if (HubProjectActions::LaunchEditor(window, project.projectFile, error)) {
        state_.status = L"Editor launched.";
    } else {
        state_.status = error;
    }
}

void HubApplication::RemoveSelectedProject(HWND window) {
    if (state_.selectedProject < 0 || state_.selectedProject >= static_cast<int>(state_.projects.size())) {
        return;
    }

    const HubProjectItem& project = state_.projects[static_cast<std::size_t>(state_.selectedProject)];
    const std::wstring message = L"Remove \"" + project.name + L"\" from 21kb Hub?\n\nProject files on disk will not be deleted.";
    const int answer = MessageBoxW(window, message.c_str(), L"Remove Project", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    if (answer != IDYES) {
        return;
    }

    const int removedIndex = state_.selectedProject;
    state_.projects.erase(state_.projects.begin() + removedIndex);
    if (state_.projects.empty()) {
        state_.selectedProject = -1;
    } else {
        state_.selectedProject = std::min(removedIndex, static_cast<int>(state_.projects.size()) - 1);
    }
    state_.scrollY = 0;
    state_.status = L"Project removed from Hub.";
    HubProjectStore::Save(state_.projects);
    ClampScroll();
    InvalidateRect(window, nullptr, FALSE);
}

void HubApplication::RefreshProjects() {
    state_.projects = HubProjectStore::Load();
    state_.selectedProject = state_.projects.empty() ? -1 : 0;
    state_.scrollY = 0;
}

void HubApplication::ClampScroll() noexcept {
    const int visibleHeight = std::max(0, state_.height - 290);
    const int contentHeight = HubProjectFilters::VisibleCount(state_) * kProjectRowHeight;
    const int maxScroll = std::max(0, contentHeight - visibleHeight);
    state_.scrollY = std::clamp(state_.scrollY, 0, maxScroll);
}

} // namespace kb::hub
