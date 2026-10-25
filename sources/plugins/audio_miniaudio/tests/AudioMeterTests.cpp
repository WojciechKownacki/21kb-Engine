#include "runtime/MiniaudioMeterNode.hpp"
#include "AudioDspTestSupport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>
#if !defined(__EMSCRIPTEN__)
#include <thread>
#endif

namespace {
using kb::audio_miniaudio::MiniaudioMeterAccumulator;
using kb::audio_miniaudio::MiniaudioMeterNode;
using kb::audio_miniaudio::MiniaudioMeterSnapshot;
using kb::audio_miniaudio::tests::AllocationCounter;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

MiniaudioMeterSnapshot Read(const MiniaudioMeterAccumulator& meter) {
    MiniaudioMeterSnapshot result;
    Require(meter.TryRead(result), "Idle meter snapshot was busy");
    return result;
}

struct Graph {
    ma_node_graph graph{};
    ma_audio_buffer_ref buffer{};
    ma_data_source_node source{};
    MiniaudioMeterNode meter;
    bool graphReady = false, bufferReady = false, sourceReady = false;
    ~Graph() {
        meter.Reset();
        if (sourceReady) ma_data_source_node_uninit(&source, nullptr);
        if (bufferReady) ma_audio_buffer_ref_uninit(&buffer);
        if (graphReady) ma_node_graph_uninit(&graph, nullptr);
    }
    void Initialize(std::span<const float> input, std::uint32_t channels, bool tap, bool capture) {
        const auto config = ma_node_graph_config_init(channels);
        graphReady = ma_node_graph_init(&config, nullptr, &graph) == MA_SUCCESS;
        Require(graphReady, "Meter graph did not initialize");
        bufferReady = ma_audio_buffer_ref_init(ma_format_f32, channels, input.data(), input.size() / channels, &buffer) == MA_SUCCESS;
        Require(bufferReady, "Meter input did not initialize");
        const auto sourceConfig = ma_data_source_node_config_init(&buffer);
        sourceReady = ma_data_source_node_init(&graph, &sourceConfig, nullptr, &source) == MA_SUCCESS;
        Require(sourceReady, "Meter source did not initialize");
        auto* endpoint = ma_node_graph_get_endpoint(&graph);
        if (tap) {
            Require(meter.Initialize(graph, capture) == MA_SUCCESS
                && ma_node_attach_output_bus(meter.Native(), 0U, endpoint, 0U) == MA_SUCCESS, "Meter output did not attach");
            endpoint = meter.Native();
        }
        Require(ma_node_attach_output_bus(&source, 0U, endpoint, 0U) == MA_SUCCESS, "Meter input did not attach");
    }
    std::vector<float> Pcm(std::uint32_t frames, std::uint32_t channels) {
        std::vector<float> output(static_cast<std::size_t>(frames) * channels);
        ma_uint64 read = 0U;
        Require(ma_node_graph_read_pcm_frames(&graph, output.data(), frames, &read) == MA_SUCCESS && read == frames,
            "Meter graph did not produce actual requested PCM");
        return output;
    }
};

void SignalAndReset() {
    MiniaudioMeterAccumulator meter;
    Require(meter.Configure(2U, true), "Stereo meter did not configure");
    const std::array<float, 8U> input{0.5F, 2.0F, -0.5F, -2.0F, 0.5F, 0.0F, -0.5F, 0.0F};
    meter.Observe(input.data(), 4U);
    auto report = Read(meter);
    Require(report.enabled && report.channels == 2U && report.blockFrames == 4U && report.processedFrames == 4U,
        "Meter did not report actual frame counts");
    Require(report.channel[0].peak == 0.5F && report.channel[0].rms == 0.5F && report.channel[0].clippedSamples == 0U
        && report.channel[1].peak == 2.0F && std::abs(report.channel[1].rms - std::sqrt(2.0F)) < 1e-6F
        && report.channel[1].clippedSamples == 2U, "Stereo peak/RMS/clipping was incorrect");
    const std::array<float, 2U> silence{};
    meter.Observe(silence.data(), 1U);
    report = Read(meter);
    Require(report.processedFrames == 5U && report.channel[1].peak == 0.0F && report.channel[1].rms == 0.0F
        && report.channel[1].heldPeak == 2.0F && report.channel[1].clippedSamples == 2U, "Silence lost peak hold or cumulative clipping");
    meter.Reset(); report = Read(meter);
    Require(report.enabled && report.channels == 2U && report.processedFrames == 0U && report.channel[1].heldPeak == 0.0F
        && report.channel[1].clippedSamples == 0U, "Reset did not clear captured history");
    Require(!meter.Configure(0U, true) && !meter.Configure(9U, true) && Read(meter).channels == 2U,
        "Invalid format changed the meter");
    meter.Observe(nullptr, 1U); meter.Observe(input.data(), 0U);
    Require(Read(meter).processedFrames == 0U, "Empty observations advanced the meter");
    Require(meter.Configure(2U, false), "Capture did not disable");
    meter.Observe(input.data(), 4U); report = Read(meter);
    Require(!report.enabled && report.processedFrames == 0U && report.channel[1].heldPeak == 0.0F,
        "Disabled capture measured samples");
}

void NonFiniteAndRange() {
    MiniaudioMeterAccumulator meter;
    Require(meter.Configure(1U, true), "Mono meter did not configure");
    const std::array<float, 7U> input{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), 1.0F, -1.0F, 2.0F, -2.0F};
    meter.Observe(input.data(), 7U);
    auto report = Read(meter);
    Require(report.channel[0].nonFiniteSamples == 3U && report.channel[0].clippedSamples == 2U
        && report.channel[0].peak == 2.0F && std::abs(report.channel[0].rms - std::sqrt(10.0F / 7.0F)) < 1e-6F,
        "Non-finite diagnostics or exact full-scale clipping threshold changed");
    const auto maximum = std::numeric_limits<float>::max();
    meter.Observe(&maximum, 1U); report = Read(meter);
    Require(report.channel[0].peak == maximum && report.channel[0].rms == maximum
        && report.channel[0].nonFiniteSamples == 3U, "Finite full-range PCM overflowed the meter");
}

void ActualGraphTransparency() {
    std::size_t comparisons = 0U;
    for (const std::uint32_t channels : {1U, 2U, 8U}) {
        for (const std::uint32_t frames : {1U, 17U, 1024U}) {
            std::vector<float> input(static_cast<std::size_t>(frames) * channels);
            for (std::size_t i = 0U; i < input.size(); ++i) input[i] = static_cast<float>(std::sin(static_cast<double>(i) * 0.71) * 2.0);
            for (bool enabled : {false, true}) {
                Graph direct, observed;
                direct.Initialize(input, channels, false, false); observed.Initialize(input, channels, true, enabled);
                const auto expected = direct.Pcm(frames, channels), actual = observed.Pcm(frames, channels);
                Require(std::memcmp(expected.data(), actual.data(), expected.size() * sizeof(float)) == 0,
                    "Native passthrough changed actual graph PCM");
                const auto report = Read(observed.meter.Meter());
                Require(report.enabled == enabled && report.channels == channels && report.processedFrames == (enabled ? frames : 0U),
                    "Native meter did not observe actual graph frames including single-frame EOF");
                if (enabled) {
                    for (std::uint32_t channel = 0U; channel < channels; ++channel) {
                        float peak = 0.0F; std::uint64_t clipped = 0U;
                        for (std::uint32_t frame = 0U; frame < frames; ++frame) {
                            const auto sample = actual[static_cast<std::size_t>(frame) * channels + channel];
                            peak = std::max(peak, std::abs(sample)); if (std::abs(sample) > 1.0F) ++clipped;
                        }
                        Require(report.channel[channel].heldPeak == peak && report.channel[channel].clippedSamples == clipped,
                            "Native graph meter differs from independently inspected output");
                    }
                }
                ++comparisons;
            }
        }
    }
    std::cout << "Meter native graphs=" << comparisons << " PCM=bit-identical singleFrameEOF=passed\n";
}

void AllocationAndLifetime() {
    ma_node_graph graph{};
    const auto config = ma_node_graph_config_init(2U);
    Require(ma_node_graph_init(&config, nullptr, &graph) == MA_SUCCESS, "Allocation graph did not initialize");
    AllocationCounter successful;
    auto allocations = ma_allocation_callbacks{&successful, &AllocationCounter::Allocate, &AllocationCounter::Reallocate, &AllocationCounter::Free};
    {
        MiniaudioMeterNode meter;
        Require(meter.Initialize(graph, true, &allocations) == MA_SUCCESS, "Meter allocation failed");
        const auto calls = successful.calls;
        const std::array<float, 2U> frame{1.5F, -1.5F};
        for (unsigned i = 0U; i < 1000U; ++i) { meter.Meter().Observe(frame.data(), 1U); static_cast<void>(Read(meter.Meter())); }
        Require(successful.calls == calls, "Meter processing allocated memory");
        meter.Reset(); meter.Reset();
        Require(successful.live == 0U && successful.bytes == 0U, "Meter retained native allocations");
    }
    for (std::size_t fail = 1U; fail <= successful.calls; ++fail) {
        AllocationCounter counter; counter.failAt = fail; allocations.pUserData = &counter;
        {
            MiniaudioMeterNode meter;
            Require(meter.Initialize(graph, true, &allocations) != MA_SUCCESS, "Injected meter allocation failure was ignored");
        }
        Require(counter.live == 0U && counter.bytes == 0U, "Failed meter initialization leaked");
    }
    ma_node_graph_uninit(&graph, nullptr);
    std::cout << "Meter allocation faults=" << successful.calls << " callbackAllocations=0 retainedBytes=0\n";
}

#if !defined(__EMSCRIPTEN__)
void ConcurrentSnapshots() {
    MiniaudioMeterAccumulator meter;
    Require(meter.Configure(2U, true), "Concurrent meter did not configure");
    std::atomic<bool> started{false}, observed{false}, done{false};
    std::thread writer{[&] {
        std::array<float, 64U> input{};
        started.store(true);
        for (unsigned block = 0U; block < 20000U; ++block) {
            const float value = (block & 1U) == 0U ? 2.0F : 3.0F;
            for (std::size_t i = 0U; i < input.size(); i += 2U) { input[i] = value; input[i+1U] = -value; }
            meter.Observe(input.data(), 32U);
            if (block == 10000U) while (!observed.load()) std::this_thread::yield();
        }
        done.store(true);
    }};
    bool valid = true; std::size_t successes = 0U, busy = 0U;
    while (!started.load()) std::this_thread::yield();
    while (!done.load()) {
        MiniaudioMeterSnapshot report;
        report.processedFrames = 123U;
        if (!meter.TryRead(report)) { valid = valid && report.processedFrames == 123U; ++busy; continue; }
        ++successes;
        if (report.processedFrames == 0U) continue;
        observed.store(true);
        valid = valid && report.processedFrames % 32U == 0U && report.blockFrames == 32U
            && report.channel[0].peak == report.channel[1].peak && report.channel[0].rms == report.channel[0].peak
            && report.channel[1].rms == report.channel[1].peak && report.channel[0].clippedSamples == report.processedFrames
            && report.channel[1].clippedSamples == report.processedFrames && report.channel[0].heldPeak >= report.channel[0].peak;
    }
    writer.join();
    const auto final = Read(meter);
    Require(valid && successes > 0U && observed.load() && final.processedFrames == 640000U && final.channel[0].clippedSamples == 640000U,
        "Concurrent snapshots mixed blocks or failed to preserve busy output");
    std::cout << "Meter concurrent frames=" << final.processedFrames << " snapshots=" << successes << " busy=" << busy << '\n';
}
#endif
} // namespace

int main() {
    try {
        SignalAndReset(); NonFiniteAndRange(); ActualGraphTransparency(); AllocationAndLifetime();
#if !defined(__EMSCRIPTEN__)
        ConcurrentSnapshots();
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
