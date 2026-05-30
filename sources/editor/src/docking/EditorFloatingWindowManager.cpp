#include "docking/EditorFloatingWindowManager.hpp"

#if defined(_WIN32)
#include "windowing/FloatingWindowControlHitTester.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

#include <dwmapi.h>
#include <windowsx.h>

#include <cstdio>
#include <vector>

namespace kb::editor {
namespace {

void PrintLastWin32Error(const char* action) {
    const DWORD error = GetLastError();
    char message[512]{};
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        message,
        static_cast<DWORD>(sizeof(message)),
        nullptr);

    std::fprintf(stderr, "%s failed. Win32 error %lu: %s\n", action, error, message);
}

} // namespace

void EditorFloatingWindowManager::Configure(HINSTANCE instance, HWND owner, const EditorMetrics& metrics) noexcept {
    instance_ = instance;
    owner_ = owner;
    metrics_ = &metrics;
}

void EditorFloatingWindowManager::Shutdown() {
    std::vector<HWND> windows;
    windows.reserve(panelToWindow_.size());
    for (const auto& entry : panelToWindow_) {
        if (entry.second != nullptr) {
            windows.push_back(entry.second);
        }
    }

    panelToWindow_.clear();
    windowToPanel_.clear();

    for (HWND window : windows) {
        DestroyWindow(window);
    }
}

bool EditorFloatingWindowManager::IsFloatingWindow(HWND window) const noexcept {
    return windowToPanel_.contains(window);
}

std::uint32_t EditorFloatingWindowManager::PanelId(HWND window) const noexcept {
    const auto it = windowToPanel_.find(window);
    return it == windowToPanel_.end() ? 0U : it->second;
}

HWND EditorFloatingWindowManager::WindowForPanel(std::uint32_t panelId) const noexcept {
    const auto it = panelToWindow_.find(panelId);
    return it == panelToWindow_.end() ? nullptr : it->second;
}

LRESULT EditorFloatingWindowManager::HitTest(HWND window, LPARAM lparam) const {
    if (metrics_ == nullptr) {
        return HTCLIENT;
    }

    POINT screenPoint{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    RECT frame{};
    GetWindowRect(window, &frame);

    const int x = screenPoint.x - frame.left;
    const int y = screenPoint.y - frame.top;
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    const int border = metrics_->floatingResizeBorder;

    const bool left = x >= 0 && x < border;
    const bool right = x >= width - border && x < width;
    const bool top = y >= 0 && y < border;
    const bool bottom = y >= height - border && y < height;

    if (top && left) {
        return HTTOPLEFT;
    }
    if (top && right) {
        return HTTOPRIGHT;
    }
    if (bottom && left) {
        return HTBOTTOMLEFT;
    }
    if (bottom && right) {
        return HTBOTTOMRIGHT;
    }
    if (left) {
        return HTLEFT;
    }
    if (right) {
        return HTRIGHT;
    }
    if (top) {
        return HTTOP;
    }
    if (bottom) {
        return HTBOTTOM;
    }

    POINT clientPoint = screenPoint;
    ScreenToClient(window, &clientPoint);
    RECT client{};
    GetClientRect(window, &client);

    const int controlsLeft = client.right - FloatingWindowControlLayout::TotalWidth(*metrics_);
    if (FloatingWindowControlHitTester{}.HitTest(*metrics_, client.right, clientPoint.x, clientPoint.y) != FloatingWindowControlKind::None) {
        return HTCLIENT;
    }
    if (clientPoint.y >= 0 && clientPoint.y < metrics_->tabStripHeight && clientPoint.x >= metrics_->tabWidth && clientPoint.x < controlsLeft) {
        return HTCAPTION;
    }

    return HTCLIENT;
}

void EditorFloatingWindowManager::OnDestroyed(HWND window) {
    const std::uint32_t panelId = PanelId(window);
    if (panelId == 0) {
        return;
    }

    windowToPanel_.erase(window);
    panelToWindow_.erase(panelId);
}

std::optional<EditorFloatingWindowManager::ResizeEvent> EditorFloatingWindowManager::OnResized(HWND window, int width, int height) const noexcept {
    const std::uint32_t panelId = PanelId(window);
    if (panelId == 0) {
        return std::nullopt;
    }
    return ResizeEvent{ .panelId = panelId, .width = width, .height = height };
}

bool EditorFloatingWindowManager::Create(std::uint32_t panelId, const std::string& titleText, const DockRect& rect) {
    if (panelToWindow_.contains(panelId)) {
        return false;
    }

    constexpr DWORD floatingStyle = WS_POPUP | WS_SYSMENU;
    const std::wstring title(titleText.begin(), titleText.end());
    const LONG_PTR ownerApplication = GetWindowLongPtrW(owner_, GWLP_USERDATA);
    HWND floating = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        EditorFloatingWindowManager::WindowClassName,
        title.c_str(),
        floatingStyle,
        rect.x,
        rect.y,
        rect.width,
        rect.height,
        owner_,
        nullptr,
        instance_,
        reinterpret_cast<void*>(ownerApplication));

    if (floating == nullptr) {
        PrintLastWin32Error("CreateWindowExW floating");
        return false;
    }

    SetWindowLongPtrW(floating, GWLP_USERDATA, ownerApplication);

    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(floating, 20, &darkMode, sizeof(darkMode));

    panelToWindow_[panelId] = floating;
    windowToPanel_[floating] = panelId;
    ShowWindow(floating, SW_SHOW);
    UpdateWindow(floating);
    return true;
}

void EditorFloatingWindowManager::Destroy(std::uint32_t panelId) {
    const auto it = panelToWindow_.find(panelId);
    if (it == panelToWindow_.end()) {
        return;
    }

    HWND floating = it->second;
    panelToWindow_.erase(it);
    windowToPanel_.erase(floating);
    if (floating != nullptr) {
        DestroyWindow(floating);
    }
}

std::optional<DockRect> EditorFloatingWindowManager::RectForPanel(std::uint32_t panelId) const {
    HWND floating = WindowForPanel(panelId);
    if (floating == nullptr) {
        return std::nullopt;
    }

    RECT rect{};
    GetWindowRect(floating, &rect);
    return DockRect{
        .x = rect.left,
        .y = rect.top,
        .width = rect.right - rect.left,
        .height = rect.bottom - rect.top,
    };
}

} // namespace kb::editor

#endif
