#include "runtime/MiniaudioFilterChain.hpp"
#include "runtime/MiniaudioReverbNode.hpp"
#include "AudioDspTestSupport.hpp"

#include <extras/nodes/ma_reverb_node/verblib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
using kb::audio_miniaudio::MiniaudioFilterChain;
using kb::audio_miniaudio::MiniaudioReverbNode;
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
        Require(graphReady, "Reverb graph initialization failed");
        bufferReady = ma_audio_buffer_ref_init(ma_format_f32, count, input.data(), input.size() / count, &buffer) == MA_SUCCESS;
        Require(bufferReady, "Reverb input initialization failed");
        const auto node = ma_data_source_node_config_init(&buffer);
        sourceReady = ma_data_source_node_init(&graph, &node, nullptr, &source) == MA_SUCCESS;
        Require(sourceReady, "Reverb source initialization failed");
    }
    void Configure(std::span<const Desc> effects, std::uint32_t rate, const ma_allocation_callbacks* allocations = nullptr) {
        Require(chain.Initialize(graph, rate, effects, allocations) == MA_SUCCESS
            && chain.Attach(&source, ma_node_graph_get_endpoint(&graph)) == MA_SUCCESS, "Reverb chain configuration failed");
    }
    std::vector<float> Read(std::size_t frames, std::size_t block) {
        std::vector<float> output(frames * channels);
        for (std::size_t offset = 0U; offset < frames;) {
            const auto count = std::min(block, frames - offset);
            ma_uint64 read = 0U;
            Require(ma_node_graph_read_pcm_frames(&graph, output.data() + offset * channels, count, &read) == MA_SUCCESS
                && read == count, "Reverb did not produce actual complete PCM");
            offset += count;
        }
        for (float sample : output) Require(std::isfinite(sample) && std::abs(sample) < 1.0F, "Reverb signal is non-finite or clipped");
        return output;
    }
};

// Direct DSP control checks graph wiring, parameter propagation and block-size
// independence. It uses the same vendor kernel; acoustic properties are checked
// separately below, rather than claiming a second independent reverb algorithm.
std::vector<float> Direct(std::span<const float> input, std::size_t frames, std::uint32_t channels,
    std::uint32_t rate, std::span<const Desc> effects) {
    std::vector<float> samples(frames * channels);
    std::copy(input.begin(), input.end(), samples.begin());
    for (const auto& effect : effects) {
        if (effect.bypass) continue;
        Require(effect.kind == Kind::Reverb || effect.kind == Kind::Delay, "Unsupported reverb control stage");
        std::vector<float> next(samples.size());
        if (effect.kind == Kind::Reverb) {
            auto state = std::make_unique<verblib>();
            Require(verblib_initialize(state.get(), rate, channels) != 0, "Direct reverb control initialization failed");
            verblib_set_room_size(state.get(), effect.roomSize);
            verblib_set_damping(state.get(), effect.damping);
            verblib_set_width(state.get(), effect.width);
            verblib_set_wet(state.get(), effect.wet / 3.0F);
            verblib_set_dry(state.get(), effect.dry / 2.0F);
            verblib_process(state.get(), samples.data(), next.data(), static_cast<unsigned long>(frames));
        } else {
            // Independent time-offset delay recurrence, not the vendor delay kernel.
            const auto delay = std::max<std::size_t>(1U, static_cast<std::size_t>(std::llround(effect.delayMs * rate / 1000.0)));
            std::vector<double> echo(samples.size());
            for (std::size_t frame = 0U; frame < frames; ++frame) {
                for (std::size_t channel = 0U; channel < channels; ++channel) {
                    const auto index = frame * channels + channel;
                    if (frame >= delay) {
                        const auto previous = (frame - delay) * channels + channel;
                        echo[index] = samples[previous] + effect.feedback * echo[previous];
                    }
                    next[index] = static_cast<float>(effect.dry * samples[index] + effect.wet * echo[index]);
                }
            }
        }
        samples = std::move(next);
    }
    return samples;
}

double Energy(std::span<const float> samples) {
    double result = 0.0;
    for (float sample : samples) result += static_cast<double>(sample) * sample;
    return result;
}

std::vector<float> Impulse(std::span<const Desc> effects, std::uint32_t rate = 48000U,
    std::uint32_t channels = 2U, std::size_t block = 127U) {
    std::vector<float> input(channels);
    input[0] = 0.25F; // Left-only input also exercises the stereo wet path.
    const auto frames = static_cast<std::size_t>(rate) * 2U;
    if (std::all_of(effects.begin(), effects.end(), [](const auto& e) { return e.bypass; })) input.resize(frames * channels);
    Graph graph;
    graph.Initialize(input, channels);
    graph.Configure(effects, rate);
    const auto output = graph.Read(frames, block);
    const auto expected = Direct(input, frames, channels, rate, effects);
    double maximum = 0.0;
    for (std::size_t i = 0U; i < output.size(); ++i) maximum = std::max(maximum, std::abs(static_cast<double>(output[i]) - expected[i]));
    Require(maximum < 2e-6, "Reverb graph lost parameters, stages or sample timing");
    std::cout << "Reverb impulse stages=" << effects.size() << " rate=" << rate << " channels=" << channels
        << " block=" << block << " maximumDifference=" << maximum << " tailEnergy=" << Energy(std::span{output}.subspan(channels)) << '\n';
    return output;
}

void Properties() {
    const Desc wet{.kind = Kind::Reverb, .wet = 1.0F, .dry = 0.0F};
    const auto normal = Impulse(std::array{wet});
    Require(normal[0] == 0.0F && normal[1] == 0.0F && Energy(std::span{normal}.subspan(960U * 2U)) > 1e-5,
        "Wet reverb has no delayed tail after the one-frame source ended");
    double right = 0.0, difference = 0.0;
    for (std::size_t i = 0U; i < normal.size(); i += 2U) {
        right += static_cast<double>(normal[i + 1U]) * normal[i + 1U];
        difference += std::abs(normal[i] - normal[i + 1U]);
    }
    Require(right > 1e-6 && difference > 0.01, "Reverb stereo spread disappeared");
    auto narrow = wet; narrow.width = 0.0F;
    const auto monoWet = Impulse(std::array{narrow});
    for (std::size_t i = 0U; i < monoWet.size(); i += 2U) Require(monoWet[i] == monoWet[i + 1U], "Zero-width wet reverb is not centered");
    auto small = wet; small.roomSize = 0.0F;
    auto large = wet; large.roomSize = 1.0F;
    const auto smallOutput = Impulse(std::array{small});
    const auto largeOutput = Impulse(std::array{large});
    const auto offset = 48000U * 2U;
    Require(Energy(std::span{largeOutput}.subspan(offset)) > 5.0 * Energy(std::span{smallOutput}.subspan(offset)),
        "Room size did not extend the late tail");
    auto dry = wet; dry.wet = 0.0F; dry.dry = 0.5F;
    const auto dryOutput = Impulse(std::array{dry, dry});
    Require(dryOutput[0] == 0.0625F && Energy(std::span{dryOutput}.subspan(1U)) == 0.0, "Reverb dry gain was internally rescaled");
    static_cast<void>(Impulse(std::vector<Desc>(16U, dry), 22050U, 1U, 1024U));
    for (const float damping : {0.0F, 1.0F}) {
        auto effect = wet; effect.damping = damping;
        static_cast<void>(Impulse(std::array{effect}, 22050U, 1U, 31U));
    }
    static_cast<void>(Impulse(std::array{wet}, 192000U, 2U, 1024U));
    static_cast<void>(Impulse(std::array{Desc{.kind = Kind::Reverb, .bypass = true}}));
    static_cast<void>(Impulse(std::array{wet, Desc{.kind = Kind::Reverb, .wet = 0.7F, .dry = 0.4F, .roomSize = 0.8F, .damping = 0.6F, .width = 0.3F}}, 44100U, 2U, 7U));
    const Desc delay{.kind = Kind::Delay, .delayMs = 7.0, .feedback = 0.25F};
    static_cast<void>(Impulse(std::array{delay, wet}));
    static_cast<void>(Impulse(std::array{wet, delay}));
    // Wet-only silence must remain silent, including its continuously processed tail.
    Graph silence;
    const std::array<float, 2U> zero{};
    silence.Initialize(zero, 2U);
    silence.Configure(std::array{wet}, 48000U);
    Require(Energy(silence.Read(48000U, 31U)) == 0.0, "Silent reverb generated a signal");
}

void InvalidAndAllocationFailures() {
    AllocationCounter counter;
    const ma_allocation_callbacks allocations{&counter, &AllocationCounter::Allocate, &AllocationCounter::Reallocate, &AllocationCounter::Free};
    Graph actual, reference;
    std::vector<float> input(16384U * 2U);
    for (std::size_t i = 0U; i < input.size(); ++i) input[i] = static_cast<float>(0.01 * std::sin(static_cast<double>(i) * 0.1));
    actual.Initialize(input, 2U); reference.Initialize(input, 2U);
    const Desc original{.kind = Kind::Reverb};
    actual.Configure(std::array{original}, 48000U, &allocations); reference.Configure(std::array{original}, 48000U);
    const auto live = counter.live, bytes = counter.bytes;
    const auto unchanged = [&] {
        const auto a = actual.Read(128U, 7U), b = reference.Read(128U, 7U);
        Require(a == b && counter.live == live && counter.bytes == bytes && actual.chain.ActiveNodeCount() == 1U,
            "Rejected reverb edit changed the old signal or retained allocations");
    };
    std::size_t rejected = 0U;
    const auto reject = [&](const Desc& effect) {
        const auto calls = counter.calls;
        Require(actual.chain.Initialize(actual.graph, 48000U, std::array{effect}, &allocations) == MA_INVALID_ARGS
            && calls == counter.calls, "Invalid reverb parameters reached allocation");
        unchanged(); ++rejected;
    };
    for (float value : {-0.01F, 1.01F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        for (float Desc::* member : {&Desc::roomSize, &Desc::damping, &Desc::width, &Desc::wet, &Desc::dry}) {
            auto invalid = original; invalid.*member = value; reject(invalid);
        }
    }
    auto invalid = original; invalid.delayMs = 1.0; reject(invalid);
    invalid = original; invalid.feedback = 0.5F; reject(invalid);
    invalid = original; invalid.kind = Kind::Delay; invalid.roomSize = 0.6F; reject(invalid);
    invalid = original; invalid.kind = Kind::LowPass; invalid.damping = 0.6F; reject(invalid);
    for (std::uint32_t rate : {8000U, 22049U}) {
        const auto calls = counter.calls;
        Require(actual.chain.Initialize(actual.graph, rate, std::array{original}, &allocations) == MA_INVALID_ARGS
            && counter.calls == calls, "Unsupported reverb rate reached allocation");
        unchanged();
    }
    Graph wide; const std::array<float, 8U> zero{}; wide.Initialize(zero, 8U);
    const auto beforeWide = counter.calls;
    Require(actual.chain.Initialize(wide.graph, 48000U, std::array{original}, &allocations) == MA_INVALID_ARGS
        && counter.calls == beforeWide, "Unsupported reverb channels reached allocation");
    unchanged();
    auto bypassed = original; bypassed.bypass = true;
    MiniaudioFilterChain bypass;
    Require(bypass.Initialize(wide.graph, 8000U, std::array{bypassed}, &allocations) == MA_SUCCESS
        && bypass.ActiveNodeCount() == 0U && counter.calls == beforeWide, "Bypassed reverb allocated device-specific state");
    std::vector<Desc> overBudget(10U, Desc{.kind = Kind::Delay, .delayMs = 2000.0});
    overBudget.insert(overBudget.end(), 6U, original);
    const auto beforeBudget = counter.calls;
    Require(actual.chain.Initialize(actual.graph, 192000U, overBudget, &allocations) == MA_INVALID_ARGS
        && counter.calls == beforeBudget, "Combined reverb and delay state exceeded the budget before validation");
    unchanged();
    const std::vector<Desc> allBypassed(16U, bypassed);
    Require(bypass.Initialize(wide.graph, 8000U, allBypassed, &allocations) == MA_SUCCESS
        && bypass.ActiveNodeCount() == 0U && counter.calls == beforeBudget, "Bypassed reverb chain allocated state");
    const std::array replacement{original, Desc{.kind = Kind::Delay, .delayMs = 3.0}, original};
    MiniaudioFilterChain probe;
    const auto start = counter.calls;
    Require(probe.Initialize(actual.graph, 48000U, replacement, &allocations) == MA_SUCCESS, "Reverb allocation probe failed");
    const auto count = counter.calls - start;
    probe.Reset(); unchanged();
    for (std::size_t fault = 1U; fault <= count; ++fault) {
        counter.failAt = counter.calls + fault;
        Require(actual.chain.Initialize(actual.graph, 48000U, replacement, &allocations) == MA_OUT_OF_MEMORY,
            "Reverb allocation fault was not reported");
        unchanged();
        std::cout << "Reverb allocation fault=" << fault << " live=" << live << " bytes=" << bytes << '\n';
    }
    counter.failAt = 0U;
    actual.chain.Reset();
    Require(counter.live == 0U && counter.bytes == 0U, "Reverb reset retained owned memory");
    std::cout << "Reverb invalid rejected=" << rejected << " stateBytes=" << MiniaudioReverbNode::StateBytes() << " finalLive=" << counter.live << '\n';
}
} // namespace

int main() {
    try { Properties(); InvalidAndAllocationFailures(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
