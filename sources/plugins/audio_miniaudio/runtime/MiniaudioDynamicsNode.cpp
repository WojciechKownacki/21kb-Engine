#include "runtime/MiniaudioDynamicsNode.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace kb::audio_miniaudio {

MiniaudioDynamicsNode::~MiniaudioDynamicsNode() { Reset(); }
const ma_allocation_callbacks* MiniaudioDynamicsNode::Allocator() const noexcept {
    return customAllocator_ ? &allocations_ : nullptr;
}

bool MiniaudioDynamicsNode::Valid(const MiniaudioFilterDesc& desc) noexcept {
    if (!std::isfinite(desc.thresholdDb) || desc.thresholdDb < -60.0 || desc.thresholdDb > 0.0
        || !std::isfinite(desc.ratio) || desc.ratio < 1.0 || desc.ratio > 20.0
        || !std::isfinite(desc.attackMs) || desc.attackMs < 0.0 || desc.attackMs > 200.0
        || !std::isfinite(desc.releaseMs) || desc.releaseMs < 1.0 || desc.releaseMs > 5000.0
        || !std::isfinite(desc.makeupDb) || desc.makeupDb < 0.0 || desc.makeupDb > 24.0
        || !std::isfinite(desc.ceilingDb) || desc.ceilingDb < -24.0 || desc.ceilingDb > 0.0) return false;
    if (desc.kind == MiniaudioFilterKind::Compressor) return desc.ceilingDb == -1.0;
    const bool unusedCompressor = desc.thresholdDb == -12.0 && desc.ratio == 4.0 && desc.attackMs == 10.0 && desc.makeupDb == 0.0;
    if (desc.kind == MiniaudioFilterKind::Limiter) return unusedCompressor;
    return unusedCompressor && desc.releaseMs == 100.0 && desc.ceilingDb == -1.0;
}

ma_result MiniaudioDynamicsNode::Initialize(ma_node_graph& graph, std::uint32_t rate,
    const MiniaudioFilterDesc& desc, const ma_allocation_callbacks* allocations) {
    const auto channels = ma_node_graph_get_channels(&graph);
    if (channels == 0U || channels > 8U || rate < 8000U || rate > 192000U || !Valid(desc)
        || (desc.kind != MiniaudioFilterKind::Compressor && desc.kind != MiniaudioFilterKind::Limiter)) return MA_INVALID_ARGS;
    Reset();
    customAllocator_ = allocations != nullptr;
    if (allocations != nullptr) allocations_ = *allocations;
    state_.channels = channels;
    state_.limiter = desc.kind == MiniaudioFilterKind::Limiter;
    state_.threshold = std::pow(10.0, (state_.limiter ? desc.ceilingDb : desc.thresholdDb) / 20.0);
    state_.exponent = 1.0 - 1.0 / desc.ratio;
    state_.makeup = std::pow(10.0, desc.makeupDb / 20.0);
    state_.attack = desc.attackMs == 0.0 ? 0.0 : std::exp(-1000.0 / (desc.attackMs * rate));
    state_.release = std::exp(-1000.0 / (desc.releaseMs * rate));
    state_.gain = 1.0;
    static ma_node_vtable vtable{&Process, nullptr, 1U, 1U, MA_NODE_FLAG_CONTINUOUS_PROCESSING};
    auto config = ma_node_config_init();
    config.vtable = &vtable;
    config.pInputChannels = &state_.channels;
    config.pOutputChannels = &state_.channels;
    const auto result = ma_node_init(&graph, &config, Allocator(), &state_.node);
    ready_ = result == MA_SUCCESS;
    return result;
}

void MiniaudioDynamicsNode::Process(ma_node* node, const float** input, ma_uint32* inputFrames,
    float** output, ma_uint32* outputFrames) {
    static_assert(std::is_standard_layout_v<State>);
    auto& state = *static_cast<State*>(node); // The graph base is the first member.
    static_cast<void>(inputFrames);
    for (ma_uint32 frame = 0U; frame < *outputFrames; ++frame) {
        const auto offset = static_cast<std::size_t>(frame) * state.channels;
        double peak = 0.0;
        bool finite = true;
        for (std::uint32_t channel = 0U; channel < state.channels; ++channel) {
            const double sample = input[0][offset + channel];
            finite = finite && std::isfinite(sample);
            if (std::isfinite(sample)) peak = std::max(peak, std::abs(sample));
        }
        if (!finite) peak = 0.0;
        const double desired = peak <= state.threshold ? 1.0
            : state.limiter ? state.threshold / peak : std::pow(state.threshold / peak, state.exponent);
        if (state.limiter) {
            state.gain = std::min(desired, state.release * state.gain + (1.0 - state.release));
        } else {
            const double coefficient = desired < state.gain ? state.attack : state.release;
            state.gain = desired + coefficient * (state.gain - desired);
        }
        const double bound = state.limiter ? state.threshold : static_cast<double>(std::numeric_limits<float>::max());
        for (std::uint32_t channel = 0U; channel < state.channels; ++channel) {
            const double value = finite ? input[0][offset + channel] * state.gain * state.makeup : 0.0;
            output[0][offset + channel] = static_cast<float>(std::clamp(value, -bound, bound));
        }
    }
}

void MiniaudioDynamicsNode::Reset() noexcept {
    if (ready_) ma_node_uninit(&state_.node, Allocator());
    ready_ = false;
}

} // namespace kb::audio_miniaudio
