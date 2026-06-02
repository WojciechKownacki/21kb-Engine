#pragma once

#include "kb/render/frame/RenderViewportDesc.hpp"

#include <cstdint>
#include <vector>

namespace kb::render {

struct RenderFrameDesc {
    std::uint64_t frameIndex = 0;
    std::vector<RenderViewportDesc> viewports;

    [[nodiscard]] bool HasViewports() const noexcept {
        return !viewports.empty();
    }
};

} // namespace kb::render
