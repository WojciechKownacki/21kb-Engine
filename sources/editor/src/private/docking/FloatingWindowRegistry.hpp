#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kb::editor {

class FloatingWindowRegistry {
public:
#if defined(_WIN32)
    [[nodiscard]] bool ContainsWindow(HWND window) const noexcept;
    [[nodiscard]] bool ContainsPanel(std::uint32_t panelId) const noexcept;
    [[nodiscard]] std::uint32_t PanelId(HWND window) const noexcept;
    [[nodiscard]] HWND WindowForPanel(std::uint32_t panelId) const noexcept;
    [[nodiscard]] std::vector<HWND> Windows() const;

    void Add(std::uint32_t panelId, HWND window);
    void RemoveWindow(HWND window) noexcept;
    void RemovePanel(std::uint32_t panelId) noexcept;
    void Clear() noexcept;
#endif

private:
#if defined(_WIN32)
    std::unordered_map<std::uint32_t, HWND> panelToWindow_;
    std::unordered_map<HWND, std::uint32_t> windowToPanel_;
#endif
};

} // namespace kb::editor
