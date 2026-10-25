#include "runtime/MiniaudioDelayNode.hpp"

#include <cmath>

namespace kb::audio_miniaudio {

MiniaudioDelayNode::~MiniaudioDelayNode() { Reset(); }

const ma_allocation_callbacks* MiniaudioDelayNode::Allocator() const noexcept {
    return customAllocator_ ? &allocations_ : nullptr;
}

ma_result MiniaudioDelayNode::Initialize(ma_node_graph& graph, std::uint32_t rate,
    std::uint32_t frames, const MiniaudioFilterDesc& desc, const ma_allocation_callbacks* allocations) {
    const auto channels = ma_node_graph_get_channels(&graph);
    if (rate < 8000U || rate > 192000U || channels == 0U || channels > 8U
        || frames == 0U || frames > 2U * rate
        || !std::isfinite(desc.feedback) || desc.feedback < 0.0F || desc.feedback > 0.95F
        || !std::isfinite(desc.wet) || desc.wet < 0.0F || desc.wet > 1.0F
        || !std::isfinite(desc.dry) || desc.dry < 0.0F || desc.dry > 1.0F) return MA_INVALID_ARGS;
    Reset();
    customAllocator_ = allocations != nullptr;
    if (allocations != nullptr) allocations_ = *allocations;
    const auto split = ma_splitter_node_config_init(channels);
    auto result = ma_splitter_node_init(&graph, &split, Allocator(), &input_);
    if (result != MA_SUCCESS) return result;
    inputReady_ = true;

    auto delayed = ma_delay_node_config_init(channels, rate, frames, desc.feedback);
    // The vendor's dry parameter feeds its delay line. Keep it at one and mix
    // the immediate dry path separately so feedback never changes first-tap time.
    delayed.delay.delayStart = MA_TRUE;
    delayed.delay.dry = 1.0F;
    delayed.delay.wet = desc.wet;
    result = ma_delay_node_init(&graph, &delayed, Allocator(), &delay_);
    if (result != MA_SUCCESS) return result; // Failed node initialization releases its delay line.
    delayReady_ = true;

    auto merge = ma_splitter_node_config_init(channels);
    merge.outputBusCount = 1U;
    result = ma_splitter_node_init(&graph, &merge, Allocator(), &output_);
    if (result != MA_SUCCESS) return result;
    outputReady_ = true;
    result = ma_node_set_output_bus_volume(&input_, 0U, desc.dry);
    if (result != MA_SUCCESS) return result;
    result = ma_node_attach_output_bus(&input_, 0U, &output_, 0U);
    if (result != MA_SUCCESS) return result;
    result = ma_node_attach_output_bus(&input_, 1U, &delay_, 0U);
    if (result != MA_SUCCESS) return result;
    return ma_node_attach_output_bus(&delay_, 0U, &output_, 0U);
}

void MiniaudioDelayNode::Reset() noexcept {
    if (outputReady_) { ma_splitter_node_uninit(&output_, Allocator()); outputReady_ = false; }
    if (delayReady_) { ma_delay_node_uninit(&delay_, Allocator()); delayReady_ = false; }
    if (inputReady_) { ma_splitter_node_uninit(&input_, Allocator()); inputReady_ = false; }
}

} // namespace kb::audio_miniaudio
