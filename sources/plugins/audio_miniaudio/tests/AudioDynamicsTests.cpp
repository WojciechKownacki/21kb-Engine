#include "runtime/MiniaudioFilterChain.hpp"
#include "AudioDspTestSupport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using kb::audio_miniaudio::MiniaudioFilterChain;
using kb::audio_miniaudio::tests::AllocationCounter;
using Desc = kb::audio_miniaudio::MiniaudioFilterDesc;
using Kind = kb::audio_miniaudio::MiniaudioFilterKind;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

struct Graph {
    ma_node_graph graph{};
    ma_audio_buffer_ref buffer{};
    ma_data_source_node source{};
    MiniaudioFilterChain chain;
    std::vector<float> input;
    bool graphReady = false, bufferReady = false, sourceReady = false;
    std::uint32_t channels = 0U;
    ~Graph() {
        chain.Reset();
        if (sourceReady) ma_data_source_node_uninit(&source, nullptr);
        if (bufferReady) ma_audio_buffer_ref_uninit(&buffer);
        if (graphReady) ma_node_graph_uninit(&graph, nullptr);
    }
    void Initialize(std::span<const float> samples, std::uint32_t count) {
        channels = count;
        input.assign(samples.begin(), samples.end());
        const auto config = ma_node_graph_config_init(count);
        graphReady = ma_node_graph_init(&config, nullptr, &graph) == MA_SUCCESS;
        Require(graphReady, "Dynamics graph initialization failed");
        bufferReady = ma_audio_buffer_ref_init(ma_format_f32, count, input.data(), input.size() / count, &buffer) == MA_SUCCESS;
        Require(bufferReady, "Dynamics input initialization failed");
        const auto node = ma_data_source_node_config_init(&buffer);
        sourceReady = ma_data_source_node_init(&graph, &node, nullptr, &source) == MA_SUCCESS;
        Require(sourceReady, "Dynamics source initialization failed");
    }
    void Configure(std::span<const Desc> effects, std::uint32_t rate, const ma_allocation_callbacks* allocations = nullptr) {
        Require(chain.Initialize(graph, rate, effects, allocations) == MA_SUCCESS
            && chain.Attach(&source, ma_node_graph_get_endpoint(&graph)) == MA_SUCCESS, "Dynamics chain configuration failed");
    }
    std::vector<float> Read(std::size_t frames, std::size_t block) {
        std::vector<float> output(frames * channels);
        for (std::size_t offset = 0U; offset < frames;) {
            const auto count = std::min(block, frames - offset);
            ma_uint64 read = 0U;
            Require(ma_node_graph_read_pcm_frames(&graph, output.data() + offset * channels, count, &read) == MA_SUCCESS
                && read == count, "Dynamics did not produce actual complete PCM");
            offset += count;
        }
        for (float sample : output) Require(std::isfinite(sample), "Dynamics signal is non-finite ");
        return output;
    }
};

double StaticGain(const Desc& effect, double peak) {
    if (effect.bypass) return 1.0;
    const double threshold = std::pow(10.0, (effect.kind == Kind::Limiter ? effect.ceilingDb : effect.thresholdDb) / 20.0);
    const double db = peak == 0.0 ? -1000.0 : 20.0 * std::log10(peak);
    if (effect.kind == Kind::Limiter) return peak <= threshold ? 1.0 : threshold / peak;
    const double reduced = std::max(0.0, db - effect.thresholdDb) * (1.0 - 1.0 / effect.ratio);
    return std::pow(10.0, (effect.makeupDb - reduced) / 20.0);
}

std::vector<float> FirstFrame(std::span<const Desc> effects, float amplitude, std::uint32_t rate = 48000U,
    std::uint32_t channels = 2U) {
    std::vector<float> input(channels);
    for (std::size_t i = 0U; i < input.size(); ++i) input[i] = i % 2U == 0U ? amplitude : -amplitude * 0.5F;
    Graph graph;
    graph.Initialize(input, channels);
    graph.Configure(effects, rate);
    const auto output = graph.Read(1U, 1U);
    double scale = 1.0;
    for (const auto& effect : effects) scale *= StaticGain(effect, std::abs(static_cast<double>(amplitude) * scale));
    double maximum = 0.0;
    for (std::size_t i = 0U; i < input.size(); ++i) {
        const double expected = input[i] * scale;
        maximum = std::max(maximum, std::abs(static_cast<double>(output[i]) - expected));
        Require(std::abs(static_cast<double>(output[i]) - expected) < 2e-6, "Dynamics static curve or linked-channel gain changed");
    }
    const auto active = static_cast<std::size_t>(std::count_if(effects.begin(), effects.end(), [](const auto& e) { return !e.bypass; }));
    Require(graph.chain.ActiveNodeCount() == active, "Dynamics stages were overwritten or bypassed incorrectly");
    std::cout << "Dynamics static stages=" << effects.size() << " rate=" << rate << " channels=" << channels
        << " amplitude=" << amplitude << " maximumDifference=" << maximum << '\n';
    return output;
}

void CurvesAndOrdering() {
    const Desc compressor{.kind = Kind::Compressor, .attackMs = 0.0};
    const Desc limiter{.kind = Kind::Limiter, .ceilingDb = -6.0};
    for (const std::uint32_t rate : {8000U, 48000U, 192000U}) {
        for (const std::uint32_t channels : {1U, 2U, 8U}) {
            for (const float amplitude : {0.0F, 0.001F, 0.1F, 0.4F, 1.0F, 2.0F, 32.0F}) {
                static_cast<void>(FirstFrame(std::array{compressor}, amplitude, rate, channels));
                static_cast<void>(FirstFrame(std::array{limiter}, amplitude, rate, channels));
            }
        }
    }
    static_cast<void>(FirstFrame(std::array{Desc{.kind = Kind::Compressor, .thresholdDb = -60.0, .ratio = 20.0, .attackMs = 0.0}}, 0.8F));
    static_cast<void>(FirstFrame(std::array{Desc{.kind = Kind::Compressor, .thresholdDb = 0.0, .ratio = 20.0, .attackMs = 0.0}}, 32.0F));
    static_cast<void>(FirstFrame(std::array{Desc{.kind = Kind::Compressor, .ratio = 1.0, .attackMs = 0.0, .makeupDb = 24.0}}, 0.01F));
    static_cast<void>(FirstFrame(std::array{Desc{.kind = Kind::Limiter, .ceilingDb = -24.0}}, 2.0F));
    static_cast<void>(FirstFrame(std::array{Desc{.kind = Kind::Limiter, .ceilingDb = 0.0}}, 2.0F));
    auto bypass = compressor; bypass.bypass = true;
    static_cast<void>(FirstFrame(std::array{bypass}, 0.8F));
    auto boosted = compressor; boosted.makeupDb = 12.0;
    const auto limitedLast = FirstFrame(std::array{boosted, limiter}, 0.8F);
    const auto boostedLast = FirstFrame(std::array{limiter, boosted}, 0.8F);
    Require(std::abs(limitedLast[0] - boostedLast[0]) > 0.3F, "Nonlinear effect order was lost");
    static_cast<void>(FirstFrame(std::array{compressor, Desc{.kind = Kind::Compressor, .thresholdDb = -18.0, .ratio = 2.0, .attackMs = 0.0}}, 0.8F));
    static_cast<void>(FirstFrame(std::vector<Desc>(16U, compressor), 0.8F));
}

void Envelope(bool limiter, std::uint32_t rate, std::size_t block, double attackMs = 10.0, double releaseMs = 100.0) {
    const Desc effect = limiter ? Desc{.kind = Kind::Limiter, .releaseMs = releaseMs, .ceilingDb = -6.0}
        : Desc{.kind = Kind::Compressor, .attackMs = attackMs, .releaseMs = releaseMs};
    const auto highFrames = static_cast<std::size_t>(rate) / 20U;
    const auto frames = highFrames * 3U;
    std::vector<float> input(frames * 2U);
    for (std::size_t i = 0U; i < frames; ++i) {
        input[i * 2U] = i < highFrames ? 0.8F : 0.01F;
        input[i * 2U + 1U] = -0.5F * input[i * 2U];
    }
    Graph graph; graph.Initialize(input, 2U); graph.Configure(std::array{effect}, rate);
    const auto output = graph.Read(frames, block);
    const double desired = StaticGain(effect, static_cast<double>(input[0]));
    const auto gainAfter = [&](std::size_t count) {
        return limiter || attackMs == 0.0 ? desired : desired + (1.0 - desired) * std::exp(-1000.0 * static_cast<double>(count) / (attackMs * rate));
    };
    const double lastHigh = gainAfter(highFrames);
    double maximum = 0.0;
    for (std::size_t i = 0U; i < frames; ++i) {
        const double gain = i < highFrames ? gainAfter(i + 1U)
            : 1.0 + (lastHigh - 1.0) * std::exp(-1000.0 * static_cast<double>(i - highFrames + 1U) / (releaseMs * rate));
        for (std::size_t channel = 0U; channel < 2U; ++channel) {
            const auto index = i * 2U + channel;
            maximum = std::max(maximum, std::abs(static_cast<double>(output[index]) - input[index] * gain));
        }
    }
    Require(maximum < 2e-6, "Dynamics envelope differs from closed-form exponential attack/release");
    std::cout << "Dynamics envelope limiter=" << limiter << " rate=" << rate << " block=" << block
        << " attackMs=" << attackMs << " releaseMs=" << releaseMs << " maximumDifference=" << maximum << '\n';
}

void ExtremeInputAndCeiling() {
    Graph graph;
    std::vector<float> input(2048U * 2U);
    for (std::size_t i = 0U; i < 2048U; ++i) {
        const float amplitude = i % 3U == 0U ? 0.01F : i % 3U == 1U ? -2.0F : std::numeric_limits<float>::max();
        input[i * 2U] = amplitude; input[i * 2U + 1U] = -0.5F * amplitude;
    }
    input[100U] = std::numeric_limits<float>::quiet_NaN();
    input[200U] = std::numeric_limits<float>::infinity();
    graph.Initialize(input, 2U);
    graph.Configure(std::array{Desc{.kind = Kind::Limiter, .releaseMs = 1.0, .ceilingDb = -6.0}}, 48000U);
    const auto output = graph.Read(2048U, 7U);
    const double ceiling = std::pow(10.0, -6.0 / 20.0);
    for (std::size_t i = 0U; i < output.size(); ++i) Require(std::abs(output[i]) <= ceiling + 1e-7, "Limiter exceeded its sample-peak ceiling");
    Require(output[100U] == 0.0F && output[101U] == 0.0F && output[200U] == 0.0F && output[201U] == 0.0F
        && output[202U] != 0.0F, "Invalid input poisoned linked dynamics state or later PCM");
    Graph boosted;
    const std::array maximum{std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    boosted.Initialize(maximum, 2U);
    boosted.Configure(std::array{Desc{.kind = Kind::Compressor, .ratio = 1.0, .makeupDb = 24.0}}, 48000U);
    const auto finite = boosted.Read(16U, 7U);
    Require(finite[0] == maximum[0] && finite[1] == maximum[1], "Compressor overflow did not saturate the finite PCM range");
    for (std::size_t i = 2U; i < finite.size(); ++i) Require(finite[i] == 0.0F, "Dynamics generated a tail after input EOF");
    std::cout << "Dynamics extreme ceiling=" << ceiling << " sanitizedFrames=2 finiteOverflow=1\n";
}

void InvalidAndAllocationFailures(bool limiter) {
    AllocationCounter counter;
    const ma_allocation_callbacks allocations{&counter, &AllocationCounter::Allocate, &AllocationCounter::Reallocate, &AllocationCounter::Free};
    Graph actual, reference;
    std::vector<float> input(32768U * 2U);
    for (std::size_t i = 0U; i < input.size(); ++i) input[i] = static_cast<float>(0.8 * std::sin(static_cast<double>(i) * 0.1));
    actual.Initialize(input, 2U); reference.Initialize(input, 2U);
    const Desc original{.kind = limiter ? Kind::Limiter : Kind::Compressor};
    actual.Configure(std::array{original}, 48000U, &allocations); reference.Configure(std::array{original}, 48000U);
    const auto live = counter.live, bytes = counter.bytes;
    const auto unchanged = [&] {
        Require(actual.Read(128U, 7U) == reference.Read(128U, 7U)
            && actual.chain.ActiveNodeCount() == 1U && counter.live == live && counter.bytes == bytes,
            "Rejected dynamics edit changed PCM or leaked candidate storage");
    };
    std::size_t rejected = 0U;
    const auto reject = [&](const Desc& candidate) {
        const auto calls = counter.calls;
        Require(actual.chain.Initialize(actual.graph, 48000U, std::array{candidate}, &allocations) == MA_INVALID_ARGS
            && counter.calls == calls, "Invalid dynamics settings reached allocation");
        unchanged(); ++rejected;
    };
    const std::array<std::pair<double Desc::*, std::pair<double, double>>, 6U> bounds{{
        {&Desc::thresholdDb, {-60.1, 0.1}}, {&Desc::ratio, {0.9, 20.1}}, {&Desc::attackMs, {-0.1, 200.1}},
        {&Desc::releaseMs, {0.9, 5000.1}}, {&Desc::makeupDb, {-0.1, 24.1}}, {&Desc::ceilingDb, {-24.1, 0.1}}}};
    for (const auto& [member, values] : bounds) {
        for (const double value : {values.first, values.second, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            auto invalid = original; invalid.*member = value; reject(invalid);
        }
    }
    auto invalid = original; invalid.cutoffHz = 2000.0; reject(invalid);
    invalid = original; invalid.order = 1U; reject(invalid);
    invalid = original; invalid.wet = 0.7F; reject(invalid);
    invalid = original; invalid.delayMs = 10.0; reject(invalid);
    invalid = original; invalid.roomSize = 0.7F; reject(invalid);
    invalid = original; invalid.kind = Kind::LowPass; invalid.releaseMs = 10.0; reject(invalid);
    invalid = original; invalid.bypass = true; invalid.ratio = 0.0; reject(invalid);
    invalid = original;
    if (limiter) invalid.makeupDb = 1.0; else invalid.ceilingDb = -6.0;
    reject(invalid);
    const std::array replacement{Desc{.kind = Kind::Compressor}, Desc{.kind = Kind::Delay, .delayMs = 1.0}, Desc{.kind = Kind::Limiter}};
    MiniaudioFilterChain probe;
    const auto start = counter.calls;
    Require(probe.Initialize(actual.graph, 48000U, replacement, &allocations) == MA_SUCCESS, "Dynamics allocation probe failed");
    const auto count = counter.calls - start; probe.Reset(); unchanged();
    for (std::size_t fault = 1U; fault <= count; ++fault) {
        counter.failAt = counter.calls + fault;
        Require(actual.chain.Initialize(actual.graph, 48000U, replacement, &allocations) == MA_OUT_OF_MEMORY, "Dynamics allocation fault was hidden");
        unchanged();
        std::cout << "Dynamics allocation limiter=" << limiter << " fault=" << fault << " live=" << live << " bytes=" << bytes << '\n';
    }
    counter.failAt = 0U; actual.chain.Reset();
    Require(counter.live == 0U && counter.bytes == 0U, "Dynamics reset retained allocations");
    std::cout << "Dynamics invalid limiter=" << limiter << " rejected=" << rejected << " finalLive=" << counter.live << '\n';
}
} // namespace

int main() {
    try {
        CurvesAndOrdering();
        for (const auto rate : {8000U, 48000U, 192000U}) {
            for (const auto block : {1U, 7U, 127U, 1024U}) { Envelope(false, rate, block); Envelope(true, rate, block); }
        }
        Envelope(false, 48000U, 31U, 0.0, 1.0);
        Envelope(false, 48000U, 31U, 200.0, 5000.0);
        ExtremeInputAndCeiling();
        InvalidAndAllocationFailures(false); InvalidAndAllocationFailures(true);
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
