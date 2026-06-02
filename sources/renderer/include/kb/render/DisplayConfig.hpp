#pragma once

#include <cstdint>

namespace kb::render {

enum class DisplaySyncMode : std::uint8_t {
    VSync,
    Uncapped,
    TargetFps
};

struct DisplayConfig {
    DisplaySyncMode syncMode = DisplaySyncMode::VSync;
    std::uint32_t targetFps = 180;
    bool requestGpuDebugLayers = false;
    bool flushAfterRender = false;
    bool allowHeadlessNoop = false;
    std::uint8_t msaaSamples = 0;
    std::int32_t preferredBgfxRendererType = -1;

    [[nodiscard]] std::uint32_t ComputeResetFlags() const noexcept;
};

} // namespace kb::render
