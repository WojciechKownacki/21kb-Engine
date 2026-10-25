#pragma once

#include <miniaudio.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace kb::audio_miniaudio {

struct MiniaudioChannelMeter {
    float peak = 0.0F, rms = 0.0F, heldPeak = 0.0F;
    std::uint64_t clippedSamples = 0U, nonFiniteSamples = 0U;
};

struct MiniaudioMeterSnapshot {
    bool enabled = false;
    std::uint32_t channels = 0U, blockFrames = 0U;
    std::uint64_t processedFrames = 0U;
    std::array<MiniaudioChannelMeter, 8U> channel{};
};

// One PCM writer; configure/reset require external exclusion from that writer.
// Readers only access atomics and never delay the PCM callback. Failed bounded
// reads preserve the caller's previous snapshot and must be reported as busy.
class MiniaudioMeterAccumulator {
public:
    [[nodiscard]] bool Configure(std::uint32_t channels, bool enabled) noexcept;
    void Reset() noexcept;
    void Observe(const float* interleaved, std::uint32_t frames) noexcept;
    [[nodiscard]] bool TryRead(MiniaudioMeterSnapshot& output) const noexcept;

private:
    void Publish() noexcept;
    struct AtomicChannel {
        std::atomic<float> peak{0.0F}, rms{0.0F}, heldPeak{0.0F};
        std::atomic<std::uint64_t> clippedSamples{0U}, nonFiniteSamples{0U};
    };
    MiniaudioMeterSnapshot writer_;
    std::atomic<std::uint64_t> sequence_{0U}, processedFrames_{0U};
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint32_t> channels_{0U}, blockFrames_{0U};
    std::array<AtomicChannel, 8U> channel_{};
};

// Native passthrough: observes real graph input without changing PCM or counts.
class MiniaudioMeterNode {
public:
    MiniaudioMeterNode() = default;
    ~MiniaudioMeterNode();
    MiniaudioMeterNode(const MiniaudioMeterNode&) = delete;
    MiniaudioMeterNode& operator=(const MiniaudioMeterNode&) = delete;
    [[nodiscard]] ma_result Initialize(ma_node_graph& graph, bool enabled,
        const ma_allocation_callbacks* allocations = nullptr);
    void Reset() noexcept;
    [[nodiscard]] ma_node* Native() noexcept { return &state_.node; }
    [[nodiscard]] MiniaudioMeterAccumulator& Meter() noexcept { return meter_; }
    [[nodiscard]] const MiniaudioMeterAccumulator& Meter() const noexcept { return meter_; }

private:
    struct State { ma_node_base node{}; MiniaudioMeterAccumulator* meter = nullptr; };
    static void Process(ma_node* node, const float** input, ma_uint32* inputFrames,
        float** output, ma_uint32* outputFrames);
    State state_;
    MiniaudioMeterAccumulator meter_;
    ma_allocation_callbacks allocations_{};
    bool customAllocator_ = false, ready_ = false;
};

} // namespace kb::audio_miniaudio
