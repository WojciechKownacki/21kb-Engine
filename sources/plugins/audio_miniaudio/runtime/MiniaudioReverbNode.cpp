#include "runtime/MiniaudioReverbNode.hpp"

#include <extras/nodes/ma_reverb_node/ma_reverb_node.h>

#include <cmath>
#include <new>

namespace kb::audio_miniaudio {

struct MiniaudioReverbNode::State { ma_reverb_node node; };

MiniaudioReverbNode::~MiniaudioReverbNode() { Reset(); }
std::size_t MiniaudioReverbNode::StateBytes() noexcept { return sizeof(State); }
ma_node* MiniaudioReverbNode::Native() noexcept { return state_ != nullptr ? &state_->node : nullptr; }
const ma_allocation_callbacks* MiniaudioReverbNode::Allocator() const noexcept {
    return customAllocator_ ? &allocations_ : nullptr;
}

ma_result MiniaudioReverbNode::Initialize(ma_node_graph& graph, std::uint32_t rate,
    const MiniaudioFilterDesc& desc, const ma_allocation_callbacks* allocations) {
    const auto channels = ma_node_graph_get_channels(&graph);
    const auto normalized = [](float value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
    if (channels < 1U || channels > 2U || rate < 22050U || rate > 192000U
        || !normalized(desc.roomSize) || !normalized(desc.damping) || !normalized(desc.width)
        || !normalized(desc.wet) || !normalized(desc.dry)) return MA_INVALID_ARGS;
    Reset();
    customAllocator_ = allocations != nullptr;
    if (allocations != nullptr) allocations_ = *allocations;
    void* memory = ma_malloc(sizeof(State), Allocator());
    if (memory == nullptr) return MA_OUT_OF_MEMORY;
    state_ = new (memory) State;
    const auto config = ma_reverb_node_config_init(channels, rate);
    const auto result = ma_reverb_node_init(&graph, &config, Allocator(), &state_->node);
    if (result != MA_SUCCESS) return result;
    ready_ = true;
    // The vendor graph initializer uses defaults, so apply every requested
    // parameter explicitly. Normalize its internal wet/dry scaling to bus gains.
    verblib_set_room_size(&state_->node.reverb, desc.roomSize);
    verblib_set_damping(&state_->node.reverb, desc.damping);
    verblib_set_width(&state_->node.reverb, desc.width);
    verblib_set_wet(&state_->node.reverb, desc.wet / verblib_scalewet);
    verblib_set_dry(&state_->node.reverb, desc.dry / verblib_scaledry);
    return MA_SUCCESS;
}

void MiniaudioReverbNode::Reset() noexcept {
    if (state_ == nullptr) return;
    if (ready_) ma_reverb_node_uninit(&state_->node, Allocator());
    ready_ = false;
    state_->~State();
    ma_free(state_, Allocator());
    state_ = nullptr;
}

} // namespace kb::audio_miniaudio
