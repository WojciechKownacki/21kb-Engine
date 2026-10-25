#include "runtime/MiniaudioMeterNode.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace kb::audio_miniaudio {
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
namespace {
std::uint64_t AddSaturated(std::uint64_t value, std::uint64_t increment) noexcept {
    return value + std::min(increment, std::numeric_limits<std::uint64_t>::max() - value);
}
} // namespace

bool MiniaudioMeterAccumulator::Configure(std::uint32_t channels, bool enabled) noexcept {
    if (channels == 0U || channels > writer_.channel.size()) return false;
    writer_ = {};
    writer_.channels = channels;
    writer_.enabled = enabled;
    Publish();
    return true;
}

void MiniaudioMeterAccumulator::Reset() noexcept {
    const auto channels = writer_.channels;
    const auto enabled = writer_.enabled;
    writer_ = {};
    writer_.channels = channels;
    writer_.enabled = enabled;
    Publish();
}

void MiniaudioMeterAccumulator::Observe(const float* input, std::uint32_t frames) noexcept {
    if (!writer_.enabled || input == nullptr || frames == 0U) return;
    for (std::uint32_t channel = 0U; channel < writer_.channels; ++channel) {
        auto& value = writer_.channel[channel];
        double energy = 0.0;
        float peak = 0.0F;
        std::uint64_t clipped = 0U, nonFinite = 0U;
        for (std::uint32_t frame = 0U; frame < frames; ++frame) {
            const auto sample = input[static_cast<std::size_t>(frame) * writer_.channels + channel];
            if (!std::isfinite(sample)) { ++nonFinite; continue; }
            const auto magnitude = std::abs(sample);
            peak = std::max(peak, magnitude);
            energy += static_cast<double>(sample) * sample;
            if (magnitude > 1.0F) ++clipped;
        }
        value.peak = peak;
        value.rms = static_cast<float>(std::sqrt(energy / frames));
        value.heldPeak = std::max(value.heldPeak, peak);
        value.clippedSamples = AddSaturated(value.clippedSamples, clipped);
        value.nonFiniteSamples = AddSaturated(value.nonFiniteSamples, nonFinite);
    }
    writer_.blockFrames = frames;
    writer_.processedFrames = AddSaturated(writer_.processedFrames, frames);
    Publish();
}

void MiniaudioMeterAccumulator::Publish() noexcept {
    // Sequential consistency gives the version and every payload field one
    // common order, including on weakly ordered CPUs. No plain data is shared.
    sequence_.fetch_add(1U);
    enabled_.store(writer_.enabled);
    channels_.store(writer_.channels);
    blockFrames_.store(writer_.blockFrames);
    processedFrames_.store(writer_.processedFrames);
    for (std::size_t i = 0U; i < writer_.channel.size(); ++i) {
        const auto& from = writer_.channel[i];
        auto& to = channel_[i];
        to.peak.store(from.peak); to.rms.store(from.rms); to.heldPeak.store(from.heldPeak);
        to.clippedSamples.store(from.clippedSamples); to.nonFiniteSamples.store(from.nonFiniteSamples);
    }
    sequence_.fetch_add(1U);
}

bool MiniaudioMeterAccumulator::TryRead(MiniaudioMeterSnapshot& output) const noexcept {
    for (unsigned attempt = 0U; attempt < 8U; ++attempt) {
        const auto begin = sequence_.load();
        if ((begin & 1U) != 0U) continue;
        MiniaudioMeterSnapshot candidate;
        candidate.enabled = enabled_.load(); candidate.channels = channels_.load();
        candidate.blockFrames = blockFrames_.load(); candidate.processedFrames = processedFrames_.load();
        for (std::size_t i = 0U; i < candidate.channel.size(); ++i) {
            auto& to = candidate.channel[i];
            const auto& from = channel_[i];
            to.peak = from.peak.load(); to.rms = from.rms.load(); to.heldPeak = from.heldPeak.load();
            to.clippedSamples = from.clippedSamples.load(); to.nonFiniteSamples = from.nonFiniteSamples.load();
        }
        if (sequence_.load() == begin) { output = candidate; return true; }
    }
    return false;
}

MiniaudioMeterNode::~MiniaudioMeterNode() { Reset(); }

ma_result MiniaudioMeterNode::Initialize(ma_node_graph& graph, bool enabled, const ma_allocation_callbacks* allocations) {
    const auto channels = ma_node_graph_get_channels(&graph);
    if (channels == 0U || channels > 8U) return MA_INVALID_ARGS;
    Reset();
    customAllocator_ = allocations != nullptr;
    if (allocations != nullptr) allocations_ = *allocations;
    static_cast<void>(meter_.Configure(channels, enabled));
    state_.meter = &meter_;
    static ma_node_vtable vtable{&Process, nullptr, 1U, 1U, MA_NODE_FLAG_PASSTHROUGH};
    auto config = ma_node_config_init();
    config.vtable = &vtable; config.pInputChannels = &channels; config.pOutputChannels = &channels;
    const auto result = ma_node_init(&graph, &config, customAllocator_ ? &allocations_ : nullptr, &state_.node);
    ready_ = result == MA_SUCCESS;
    return result;
}

void MiniaudioMeterNode::Process(ma_node* node, const float** input, ma_uint32* inputFrames,
    float** output, ma_uint32* outputFrames) {
    static_assert(std::is_standard_layout_v<State>);
    static_cast<void>(output);
    static_cast<void>(outputFrames);
    auto& state = *static_cast<State*>(node);
    if (input != nullptr) state.meter->Observe(input[0], *inputFrames);
}

void MiniaudioMeterNode::Reset() noexcept {
    if (ready_) ma_node_uninit(&state_.node, customAllocator_ ? &allocations_ : nullptr);
    ready_ = false;
    static_cast<void>(meter_.Configure(1U, false));
}

} // namespace kb::audio_miniaudio
