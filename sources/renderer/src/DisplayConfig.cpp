#include "kb/render/DisplayConfig.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

std::uint32_t DisplayConfig::ComputeResetFlags() const noexcept {
    std::uint32_t flags = 0;
    if (syncMode == DisplaySyncMode::VSync) {
        flags |= BGFX_RESET_VSYNC;
    }
    if (flushAfterRender) {
        flags |= BGFX_RESET_FLUSH_AFTER_RENDER;
    }

    switch (msaaSamples) {
    case 2:
        flags |= BGFX_RESET_MSAA_X2;
        break;
    case 4:
        flags |= BGFX_RESET_MSAA_X4;
        break;
    case 8:
        flags |= BGFX_RESET_MSAA_X8;
        break;
    case 16:
        flags |= BGFX_RESET_MSAA_X16;
        break;
    default:
        break;
    }

    return flags;
}

} // namespace kb::render
