#pragma once

#include "HubState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::hub {

class HubRenderer {
public:
    HubRenderer() = delete;

    static void Paint(HWND window, HDC dc, HubState& state);
    [[nodiscard]] static int ProjectIndexAt(const HubState& state, int x, int y) noexcept;
    [[nodiscard]] static int ExplorerIndexAt(const HubState& state, int x, int y) noexcept;
};

} // namespace kb::hub
