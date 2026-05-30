#include "docking/EditorFloatingWindowManager.hpp"

#if defined(_WIN32)
#include "windowing/FloatingWindowFactory.hpp"
#include "windowing/FloatingWindowHitTestResolver.hpp"

#include <vector>

namespace kb::editor {

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

    return FloatingWindowHitTestResolver::Resolve(window, lparam, *metrics_);
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

    HWND floating = FloatingWindowFactory::Create(instance_, owner_, EditorFloatingWindowManager::WindowClassName, titleText, rect);
    if (floating == nullptr) {
        return false;
    }

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
