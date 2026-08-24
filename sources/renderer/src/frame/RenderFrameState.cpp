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
    for (std::size_t index = 0U; index < bgfxViewRemap_.size(); ++index) {
        bgfxViewRemap_[index] = static_cast<std::uint16_t>(index);
    }
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

    std::array<std::uint16_t, ViewId::Max> orderedSlots{};
    std::ranges::copy(viewOrder, orderedSlots.begin());
    const std::span<std::uint16_t> activeSlots{
        orderedSlots.data(), viewOrder.size() };
    std::ranges::sort(activeSlots);

    std::array<std::uint16_t, ViewId::Max> candidateRemap = bgfxViewRemap_;
    for (std::size_t index = 0U; index < viewOrder.size(); ++index) {
        candidateRemap[activeSlots[index]] = viewOrder[index];
    }

    usedViews_ = candidateViews;
    bgfxViewRemap_ = candidateRemap;
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

const std::array<std::uint16_t, ViewId::Max>&
RenderFrameState::BgfxViewRemap() const noexcept {
    return bgfxViewRemap_;
}

} // namespace kb::render
