#pragma once

#include "kb/render/ViewIdPolicy.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

struct RenderViewportPlan;

class RenderFrameState {
public:
    void Begin(std::uint64_t frameIndex);
    void End() noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool RegisterViewportPlan(const RenderViewportPlan& plan);
    [[nodiscard]] bool RegisterViewOrder(std::span<const std::uint16_t> viewOrder);

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] std::uint64_t FrameIndex() const noexcept;
    [[nodiscard]] std::span<const std::uint16_t> ViewOrder() const noexcept;

private:
    std::array<bool, ViewId::Max> usedViews_{};
    std::vector<std::uint16_t> viewOrder_;
    std::uint64_t frameIndex_ = 0;
    bool active_ = false;
};

} // namespace kb::render
