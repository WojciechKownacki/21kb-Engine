#include "docking/FloatingWindowRegistry.hpp"

#if defined(_WIN32)

namespace kb::editor {

bool FloatingWindowRegistry::ContainsWindow(HWND window) const noexcept {
    return windowToPanel_.contains(window);
}

bool FloatingWindowRegistry::ContainsPanel(std::uint32_t panelId) const noexcept {
    return panelToWindow_.contains(panelId);
}

std::uint32_t FloatingWindowRegistry::PanelId(HWND window) const noexcept {
    const auto it = windowToPanel_.find(window);
    return it == windowToPanel_.end() ? 0U : it->second;
}

HWND FloatingWindowRegistry::WindowForPanel(std::uint32_t panelId) const noexcept {
    const auto it = panelToWindow_.find(panelId);
    return it == panelToWindow_.end() ? nullptr : it->second;
}

std::vector<HWND> FloatingWindowRegistry::Windows() const {
    std::vector<HWND> windows;
    windows.reserve(panelToWindow_.size());
    for (const auto& entry : panelToWindow_) {
        if (entry.second != nullptr) {
            windows.push_back(entry.second);
        }
    }
    return windows;
}

void FloatingWindowRegistry::Add(std::uint32_t panelId, HWND window) {
    panelToWindow_[panelId] = window;
    windowToPanel_[window] = panelId;
}

void FloatingWindowRegistry::RemoveWindow(HWND window) noexcept {
    const std::uint32_t panelId = PanelId(window);
    if (panelId == 0) {
        return;
    }

    windowToPanel_.erase(window);
    panelToWindow_.erase(panelId);
}

void FloatingWindowRegistry::RemovePanel(std::uint32_t panelId) noexcept {
    const HWND window = WindowForPanel(panelId);
    if (window == nullptr) {
        return;
    }

    panelToWindow_.erase(panelId);
    windowToPanel_.erase(window);
}

void FloatingWindowRegistry::Clear() noexcept {
    panelToWindow_.clear();
    windowToPanel_.clear();
}

} // namespace kb::editor

#endif
