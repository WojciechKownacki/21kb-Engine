#include "kb/render/frame/RenderFrameState.hpp"

#include "kb/render/frame/RenderFramePipeline.hpp"

#include <algorithm>

namespace kb::render {

void RenderFrameState::Begin(std::uint64_t frameIndex) {
    Reset();
    frameIndex_ = frameIndex;
    active_ = true;
    viewOrder_.reserve(ViewId::Max);
}

void RenderFrameState::End() noexcept {
    Reset();
}

void RenderFrameState::Reset() noexcept {
    std::ranges::fill(usedViews_, false);
    viewOrder_.clear();
    frameIndex_ = 0;
    active_ = false;
}

bool RenderFrameState::RegisterViewportPlan(const RenderViewportPlan& plan) {
    return RegisterViewOrder(plan.viewOrder);
}

bool RenderFrameState::RegisterViewOrder(std::span<const std::uint16_t> viewOrder) {
    if (!active_) {
        return false;
    }

    std::array<bool, ViewId::Max> candidateViews = usedViews_;
    for (const std::uint16_t viewId : viewOrder) {
        if (!ViewId::IsValid(viewId) || candidateViews[viewId]) {
            return false;
        }
        candidateViews[viewId] = true;
    }

    usedViews_ = candidateViews;
    viewOrder_.insert(viewOrder_.end(), viewOrder.begin(), viewOrder.end());
    return true;
}

bool RenderFrameState::IsActive() const noexcept {
    return active_;
}

std::uint64_t RenderFrameState::FrameIndex() const noexcept {
    return frameIndex_;
}

std::span<const std::uint16_t> RenderFrameState::ViewOrder() const noexcept {
    return viewOrder_;
}

} // namespace kb::render
