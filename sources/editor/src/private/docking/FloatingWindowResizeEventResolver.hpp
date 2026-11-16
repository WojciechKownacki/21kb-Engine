#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <optional>

namespace kb::editor {

struct FloatingWindowResizeEvent {
    std::uint32_t panelId = 0;
    int width = 0;
    int height = 0;
};

class FloatingWindowResizeEventResolver {
public:
#if defined(_WIN32)
    // The resize of the window that shows `panelId`; none when it shows no panel.
    [[nodiscard]] static std::optional<FloatingWindowResizeEvent> Resolve(std::uint32_t panelId, int width, int height) noexcept;
#endif
};

} // namespace kb::editor
