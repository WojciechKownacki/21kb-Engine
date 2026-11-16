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

// The native window of each floating tab group, by the id of the group in the dock model.
class FloatingWindowRegistry {
public:
#if defined(_WIN32)
    [[nodiscard]] bool ContainsWindow(HWND window) const noexcept;
    [[nodiscard]] bool ContainsGroup(std::uint32_t groupId) const noexcept;
    [[nodiscard]] std::uint32_t GroupId(HWND window) const noexcept;
    [[nodiscard]] HWND WindowForGroup(std::uint32_t groupId) const noexcept;
    [[nodiscard]] std::vector<HWND> Windows() const;
    [[nodiscard]] std::vector<std::uint32_t> Groups() const;

    void Add(std::uint32_t groupId, HWND window);
    void RemoveWindow(HWND window) noexcept;
    void RemoveGroup(std::uint32_t groupId) noexcept;
    void Clear() noexcept;
#endif

private:
#if defined(_WIN32)
    std::unordered_map<std::uint32_t, HWND> groupToWindow_;
    std::unordered_map<HWND, std::uint32_t> windowToGroup_;
#endif
};

} // namespace kb::editor
