#include "runtime/MiniaudioFilterChain.hpp"
#include "AudioDspTestSupport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
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
    bool graphReady = false;
    bool bufferReady = false;
    bool sourceReady = false;
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
        const auto config = ma_node_graph_config_init(channels);
        graphReady = ma_node_graph_init(&config, nullptr, &graph) == MA_SUCCESS;
        Require(graphReady, "Delay test graph did not initialize");
        bufferReady = ma_audio_buffer_ref_init(ma_format_f32, channels, input.data(), input.size() / channels, &buffer) == MA_SUCCESS;
        Require(bufferReady, "Delay test input did not initialize");
        const auto node = ma_data_source_node_config_init(&buffer);
        sourceReady = ma_data_source_node_init(&graph, &node, nullptr, &source) == MA_SUCCESS;
        Require(sourceReady, "Delay test source did not initialize");
    }
    void Configure(std::span<const Desc> effects, std::uint32_t rate, const ma_allocation_callbacks* allocations = nullptr) {
        Require(chain.Initialize(graph, rate, effects, allocations) == MA_SUCCESS
            && chain.Attach(&source, ma_node_graph_get_endpoint(&graph)) == MA_SUCCESS, "Delay chain did not configure");
    }
    std::vector<float> Read(std::size_t frames, std::uint32_t blockSize) {
        std::vector<float> output(frames * channels);
        std::size_t offset = 0U;
        while (offset < frames) {
            const auto count = std::min<std::size_t>(blockSize, frames - offset);
            ma_uint64 read = 0U;
            Require(ma_node_graph_read_pcm_frames(&graph, output.data() + offset * channels, count, &read) == MA_SUCCESS
                && read == count, "Delay did not produce complete native PCM blocks");
            offset += count;
        }
        for (const float sample : output) Require(std::isfinite(sample), "Delay produced non-finite PCM");
        return output;
    }
};

std::vector<double> Reference(std::span<const float> input, std::size_t frames, std::uint32_t channels,
    std::uint32_t rate, std::span<const Desc> effects) {
    std::vector<double> samples(frames * channels);
    std::copy(input.begin(), input.end(), samples.begin());
    for (const auto& effect : effects) {
        if (effect.bypass) continue;
        if (effect.kind != Kind::Delay) {
            Require(effect.order == 2U, "Delay reference requires second-order filters");
            double b0, b1, b2, a1, a2;
            constexpr double pi = 3.141592653589793;
            if (effect.kind == Kind::PeakingEqualizer) {
                const double w = 2.0 * pi * effect.cutoffHz / rate;
                const double amplitude = std::pow(10.0, effect.gainDb / 40.0);
                const double alpha = std::sin(w) / (2.0 * effect.quality);
                const double denominator = 1.0 + alpha / amplitude;
                b0 = (1.0 + alpha * amplitude) / denominator;
                b1 = -2.0 * std::cos(w) / denominator;
                b2 = (1.0 - alpha * amplitude) / denominator;
                a1 = b1;
                a2 = (1.0 - alpha / amplitude) / denominator;
            } else {
                // Bilinear transform of an analog second-order Butterworth filter.
                const double k = std::tan(pi * effect.cutoffHz / rate);
                const double denominator = 1.0 + std::sqrt(2.0) * k + k * k;
                b0 = (effect.kind == Kind::LowPass ? k * k : 1.0) / denominator;
                b1 = (effect.kind == Kind::LowPass ? 2.0 : -2.0) * b0;
                b2 = b0;
                a1 = 2.0 * (k * k - 1.0) / denominator;
                a2 = (1.0 - std::sqrt(2.0) * k + k * k) / denominator;
            }
            std::vector<double> next(samples.size());
            for (std::size_t channel = 0U; channel < channels; ++channel) {
                double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
                for (std::size_t frame = 0U; frame < frames; ++frame) {
                    const auto index = frame * channels + channel;
                    const double x = samples[index];
                    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                    next[index] = y;
                    x2 = x1; x1 = x; y2 = y1; y1 = y;
                }
            }
            samples = std::move(next);
            continue;
        }
        const auto delay = std::max<std::size_t>(1U, static_cast<std::size_t>(std::llround(effect.delayMs * rate / 1000.0)));
        std::vector<double> echo(samples.size());
        std::vector<double> next(samples.size());
        for (std::size_t frame = 0U; frame < frames; ++frame) {
            for (std::size_t channel = 0U; channel < channels; ++channel) {
                const auto index = frame * channels + channel;
                if (frame >= delay) {
                    const auto previous = (frame - delay) * channels + channel;
                    echo[index] = samples[previous] + effect.feedback * echo[previous];
                }
                next[index] = effect.dry * samples[index] + effect.wet * echo[index];
            }
        }
        samples = std::move(next);
    }
    return samples;
}

void Impulse(std::span<const Desc> effects, std::uint32_t blockSize, std::uint32_t rate = 48000U,
    std::uint32_t channels = 2U, std::size_t frames = 4096U) {
    std::vector<float> input(channels);
    for (std::size_t channel = 0U; channel < channels; ++channel) input[channel] = channel % 2U == 0U ? 0.25F : -0.125F;
    const auto active = static_cast<std::size_t>(std::count_if(effects.begin(), effects.end(), [](const auto& effect) { return !effect.bypass; }));
    if (active == 0U) input.resize(frames * channels);
    Graph graph;
    graph.Initialize(input, channels); // One frame only: the delay must render its tail after source EOF.
    graph.Configure(effects, rate);
    Require(graph.chain.ActiveNodeCount() == active, "Delay bypass or repeated stages were lost");
    const auto actual = graph.Read(frames, blockSize);
    const auto expected = Reference(input, frames, channels, rate, effects);
    double maximum = 0.0;
    for (std::size_t i = 0U; i < actual.size(); ++i) {
        maximum = std::max(maximum, std::abs(static_cast<double>(actual[i]) - expected[i]));
        Require(std::abs(static_cast<double>(actual[i]) - expected[i]) < 2e-6,
            "Delay PCM differs from independent time-offset recurrence");
    }
    std::cout << "Delay impulse stages=" << effects.size() << " frames=" << frames << " channels=" << channels
        << " rate=" << rate << " block=" << blockSize << " maximumDifference=" << maximum << '\n';
}

void Rounding() {
    for (const auto& [samples, expected] : std::array{
        std::pair{0.01, 1U}, std::pair{0.49, 1U}, std::pair{0.51, 1U},
        std::pair{1.49, 1U}, std::pair{1.51, 2U}, std::pair{7.0, 7U}}) {
        Graph graph;
        const std::array input{0.25F, -0.125F};
        graph.Initialize(input, 2U);
        const std::array effects{Desc{.kind = Kind::Delay, .delayMs = samples * 1000.0 / 48000.0, .wet = 1.0F, .dry = 0.0F}};
        graph.Configure(effects, 48000U);
        const auto output = graph.Read(16U, 1U);
        for (std::size_t frame = 0U; frame < 16U; ++frame) {
            Require(output[frame * 2U] == (frame == expected ? 0.25F : 0.0F)
                && output[frame * 2U + 1U] == (frame == expected ? -0.125F : 0.0F), "Delay sample rounding changed its first tap");
        }
        std::cout << "Delay rounding requestedFrames=" << samples << " actualFrames=" << expected << '\n';
    }
}

std::vector<float> Stream() {
    std::vector<float> input(8192U * 2U);
    for (std::size_t i = 0U; i < 8192U; ++i) {
        input[i * 2U] = static_cast<float>(0.02 * std::sin(6.283185307179586 * 1000.0 * static_cast<double>(i) / 48000.0));
        input[i * 2U + 1U] = static_cast<float>(-0.01 * std::cos(6.283185307179586 * 500.0 * static_cast<double>(i) / 48000.0));
    }
    return input;
}

void ContinuousSignals() {
    const Desc delay{.kind = Kind::Delay, .delayMs = 3.0, .feedback = 0.7F, .wet = 0.6F, .dry = 0.8F};
    const Desc peak{.kind = Kind::PeakingEqualizer, .cutoffHz = 1500.0, .gainDb = 6.0, .quality = 2.0};
    const Desc high{.kind = Kind::HighPass, .cutoffHz = 200.0};
    const std::array mixed{Desc{.cutoffHz = 4000.0}, delay, peak, high,
        Desc{.kind = Kind::Delay, .delayMs = 7.0, .feedback = 0.25F, .wet = 0.3F, .dry = 0.5F}};
    auto reverse = mixed;
    std::reverse(reverse.begin(), reverse.end());
    for (const bool step : {false, true}) {
        auto input = Stream();
        if (step) for (std::size_t i = 0U; i < input.size(); ++i) input[i] = i % 2U == 0U ? 0.02F : -0.01F;
        for (const auto& effects : std::array<std::vector<Desc>, 4U>{
                std::vector<Desc>{delay}, std::vector<Desc>{delay, delay},
                std::vector<Desc>{mixed.begin(), mixed.end()}, std::vector<Desc>{reverse.begin(), reverse.end()}}) {
            Graph graph;
            graph.Initialize(input, 2U);
            graph.Configure(effects, 48000U);
            const auto actual = graph.Read(input.size() / 2U, step ? 7U : 127U);
            const auto expected = Reference(input, input.size() / 2U, 2U, 48000U, effects);
            double maximum = 0.0;
            for (std::size_t i = 0U; i < actual.size(); ++i) {
                maximum = std::max(maximum, std::abs(static_cast<double>(actual[i]) - expected[i]));
                Require(std::abs(actual[i]) < 1.0F && std::abs(static_cast<double>(actual[i]) - expected[i]) < 2e-6,
                    "Continuous mixed delay signal differs from independent DSP reference");
            }
            std::cout << "Delay continuous step=" << step << " stages=" << effects.size() << " maximumDifference=" << maximum << '\n';
        }
    }
}

void Unchanged(Graph& actual, Graph& reference) {
    const auto output = actual.Read(64U, 7U);
    const auto expected = reference.Read(64U, 7U);
    for (std::size_t i = 0U; i < output.size(); ++i)
        Require(std::abs(output[i] - expected[i]) < 1e-6F, "Rejected delay edit changed the existing native signal");
}

void InvalidParametersAndBudgetPreserveSignal() {
    AllocationCounter counter;
    const ma_allocation_callbacks allocations{&counter, &AllocationCounter::Allocate, &AllocationCounter::Reallocate, &AllocationCounter::Free};
    Graph actual;
    Graph reference;
    const auto input = Stream();
    actual.Initialize(input, 2U);
    reference.Initialize(input, 2U);
    const Desc original{.kind = Kind::Delay, .delayMs = 5.0, .feedback = 0.5F};
    const std::array originalChain{original};
    actual.Configure(originalChain, 48000U, &allocations);
    reference.Configure(originalChain, 48000U);
    const auto live = counter.live;
    const auto bytes = counter.bytes;
    std::size_t rejected = 0U;
    const auto reject = [&](const Desc& effect) {
        const auto calls = counter.calls;
        const std::array effects{effect};
        Require(actual.chain.Initialize(actual.graph, 48000U, effects, &allocations) == MA_INVALID_ARGS
            && actual.chain.ActiveNodeCount() == 1U && counter.calls == calls
            && counter.live == live && counter.bytes == bytes, "Invalid delay parameters allocated or changed the graph");
        Unchanged(actual, reference);
        ++rejected;
    };
    for (const double time : {0.0, -1.0, 2000.1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto invalid = original;
        invalid.delayMs = time;
        reject(invalid);
    }
    for (const float value : {-1.0F, 0.951F, 1.0F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto invalid = original;
        invalid.feedback = value;
        reject(invalid);
    }
    for (const float value : {-0.01F, 1.01F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto invalid = original;
        invalid.wet = value;
        reject(invalid);
        invalid = original;
        invalid.dry = value;
        reject(invalid);
    }
    auto invalid = original;
    invalid.cutoffHz = 1001.0;
    reject(invalid);
    invalid = original;
    invalid.order = 1U;
    reject(invalid);
    invalid = original;
    invalid.gainDb = 6.0;
    reject(invalid);
    invalid = original;
    invalid.quality = 2.0;
    reject(invalid);
    invalid = original;
    invalid.bypass = true;
    invalid.delayMs = std::numeric_limits<double>::quiet_NaN();
    reject(invalid);
    invalid = original;
    invalid.kind = Kind::LowPass;
    reject(invalid);

    Graph wide;
    const std::array<float, 8U> wideInput{};
    wide.Initialize(wideInput, 8U);
    std::array large{Desc{.kind = Kind::Delay, .delayMs = 2000.0}, Desc{.kind = Kind::Delay, .delayMs = 2000.0},
        Desc{.kind = Kind::Delay, .delayMs = 2000.0}};
    const auto calls = counter.calls;
    Require(actual.chain.Initialize(wide.graph, 192000U, large, &allocations) == MA_INVALID_ARGS
        && counter.calls == calls && counter.live == live && counter.bytes == bytes, "Oversized delay line budget reached allocation");
    Unchanged(actual, reference);
    counter.failAt = counter.calls + 1U;
    Require(actual.chain.Initialize(wide.graph, 192000U, std::span{large}.first(2U), &allocations) == MA_OUT_OF_MEMORY
        && counter.live == live && counter.bytes == bytes, "Valid bounded delay failed to preserve the old graph after allocation refusal");
    counter.failAt = 0U;
    Unchanged(actual, reference);
    MiniaudioFilterChain bypass;
    for (auto& effect : large) effect.bypass = true;
    const auto beforeBypass = counter.calls;
    Require(bypass.Initialize(wide.graph, 192000U, large, &allocations) == MA_SUCCESS
        && bypass.ActiveNodeCount() == 0U && counter.calls == beforeBypass, "Bypassed delay reserved processing memory");
    actual.chain.Reset();
    Require(counter.live == 0U && counter.bytes == 0U, "Delay validation test retained owned allocations");
    std::cout << "Delay invalid rejected=" << rejected << " budgetBeforeAllocation=1 bypassAllocations=0 finalLive=" << counter.live << '\n';
}

void AllocationFailurePreservesSignal(bool delaySurvivor) {
    AllocationCounter counter;
    const ma_allocation_callbacks allocations{&counter, &AllocationCounter::Allocate, &AllocationCounter::Reallocate, &AllocationCounter::Free};
    Graph actual;
    Graph reference;
    const auto input = Stream();
    actual.Initialize(input, 2U);
    reference.Initialize(input, 2U);
    const Desc original = delaySurvivor ? Desc{.kind = Kind::Delay, .delayMs = 5.0, .feedback = 0.5F} : Desc{};
    const std::array originalChain{original};
    actual.Configure(originalChain, 48000U, &allocations);
    reference.Configure(originalChain, 48000U);
    const auto live = counter.live;
    const auto bytes = counter.bytes;
    const std::array replacement{Desc{.kind = Kind::Delay, .delayMs = 1.0, .feedback = 0.25F}, Desc{},
        Desc{.kind = Kind::Delay, .delayMs = 0.5, .feedback = 0.5F}};
    MiniaudioFilterChain probe;
    const auto calls = counter.calls;
    Require(probe.Initialize(actual.graph, 48000U, replacement, &allocations) == MA_SUCCESS, "Delay allocation probe did not initialize");
    const auto count = counter.calls - calls;
    probe.Reset();
    Require(count > 0U && counter.live == live && counter.bytes == bytes, "Delay allocation probe retained resources");
    for (std::size_t failure = 1U; failure <= count; ++failure) {
        counter.failAt = counter.calls + failure;
        Require(actual.chain.Initialize(actual.graph, 48000U, replacement, &allocations) == MA_OUT_OF_MEMORY
            && actual.chain.ActiveNodeCount() == 1U && counter.live == live && counter.bytes == bytes,
            "Partial delay graph allocation leaked or replaced the old graph");
        Unchanged(actual, reference);
        std::cout << "Delay allocation survivor=" << (delaySurvivor ? "delay" : "filter") << " fault=" << failure
            << " live=" << counter.live << " bytes=" << counter.bytes << '\n';
    }
    counter.failAt = 0U;
    actual.chain.Reset();
    Require(counter.live == 0U && counter.bytes == 0U, "Delay reset retained counted allocations");
}

} // namespace

int main() {
    try {
        for (const std::uint32_t block : {1U, 7U, 31U, 127U, 1024U}) {
            Impulse(std::array{Desc{.kind = Kind::Delay, .delayMs = 10.0}}, block);
            Impulse(std::array{Desc{.kind = Kind::Delay, .delayMs = 10.0, .feedback = 0.5F, .wet = 1.0F, .dry = 0.0F}}, block);
            Impulse(std::array{Desc{.kind = Kind::Delay, .delayMs = 7.0, .feedback = 0.25F},
                Desc{.kind = Kind::Delay, .delayMs = 13.0, .feedback = 0.5F, .wet = 0.75F, .dry = 0.5F}}, block);
        }
        Impulse(std::array{Desc{.kind = Kind::Delay, .delayMs = 10.0, .feedback = 0.95F, .wet = 1.0F}}, 127U);
        Impulse(std::array{Desc{.kind = Kind::Delay, .delayMs = 10.0, .wet = 0.0F}}, 31U);
        Impulse(std::array{Desc{.kind = Kind::Delay, .delayMs = 10.0, .wet = 0.0F, .dry = 0.0F}}, 127U);
        Impulse(std::array{Desc{.kind = Kind::Delay, .bypass = true, .delayMs = 10.0, .feedback = 0.95F}}, 31U);
        Impulse(std::array{Desc{.kind = Kind::Delay, .bypass = true}, Desc{.kind = Kind::Delay, .delayMs = 3.0}}, 7U);
        Impulse(std::array{Desc{.kind = Kind::Delay, .delayMs = 2000.0, .wet = 1.0F, .dry = 0.0F}}, 127U, 8000U, 1U, 16002U);
        Impulse(std::array{Desc{.kind = Kind::Delay, .delayMs = 125.0, .feedback = 0.5F}}, 127U, 44100U, 1U, 16000U);
        Impulse(std::array{Desc{.kind = Kind::Delay, .delayMs = 0.25, .feedback = 0.95F}}, 31U, 192000U, 8U, 512U);
        Impulse(std::array{Desc{.kind = Kind::Delay, .delayMs = 3.0, .feedback = 0.7F, .wet = 0.333F, .dry = 0.2F}}, 31U);
        const std::vector<Desc> maximum(16U, Desc{.kind = Kind::Delay, .delayMs = 1.0, .wet = 1.0F, .dry = 0.0F});
        Impulse(maximum, 7U, 8000U, 1U, 512U);
        Rounding();
        ContinuousSignals();
        InvalidParametersAndBudgetPreserveSignal();
        AllocationFailurePreservesSignal(false);
        AllocationFailurePreservesSignal(true);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
