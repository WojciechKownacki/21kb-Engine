#include "docking/FloatingWindowRegistry.hpp"

#if defined(_WIN32)

namespace kb::editor {

bool FloatingWindowRegistry::ContainsWindow(HWND window) const noexcept {
    return windowToGroup_.contains(window);
}

bool FloatingWindowRegistry::ContainsGroup(std::uint32_t groupId) const noexcept {
    return groupToWindow_.contains(groupId);
}

std::uint32_t FloatingWindowRegistry::GroupId(HWND window) const noexcept {
    const auto it = windowToGroup_.find(window);
    return it == windowToGroup_.end() ? 0U : it->second;
}

HWND FloatingWindowRegistry::WindowForGroup(std::uint32_t groupId) const noexcept {
    const auto it = groupToWindow_.find(groupId);
    return it == groupToWindow_.end() ? nullptr : it->second;
}

std::vector<HWND> FloatingWindowRegistry::Windows() const {
    std::vector<HWND> windows;
    windows.reserve(groupToWindow_.size());
    for (const auto& entry : groupToWindow_) {
        if (entry.second != nullptr) {
            windows.push_back(entry.second);
        }
    }
    return windows;
}

std::vector<std::uint32_t> FloatingWindowRegistry::Groups() const {
    std::vector<std::uint32_t> groups;
    groups.reserve(groupToWindow_.size());
    for (const auto& entry : groupToWindow_) {
        groups.push_back(entry.first);
    }
    return groups;
}

void FloatingWindowRegistry::Add(std::uint32_t groupId, HWND window) {
    groupToWindow_[groupId] = window;
    windowToGroup_[window] = groupId;
}

void FloatingWindowRegistry::RemoveWindow(HWND window) noexcept {
    const std::uint32_t groupId = GroupId(window);
    if (groupId == 0) {
        return;
    }

    windowToGroup_.erase(window);
    groupToWindow_.erase(groupId);
}

void FloatingWindowRegistry::RemoveGroup(std::uint32_t groupId) noexcept {
    const HWND window = WindowForGroup(groupId);
    if (window == nullptr) {
        return;
    }

    groupToWindow_.erase(groupId);
    windowToGroup_.erase(window);
}

void FloatingWindowRegistry::Clear() noexcept {
    groupToWindow_.clear();
    windowToGroup_.clear();
}

} // namespace kb::editor

#endif
