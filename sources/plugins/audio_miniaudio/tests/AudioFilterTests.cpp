#include "runtime/MiniaudioFilterChain.hpp"
#include "AudioDspTestSupport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using kb::audio_miniaudio::MiniaudioFilterChain;
using kb::audio_miniaudio::tests::AllocationCounter;
using kb::audio_miniaudio::MiniaudioFilterDesc;
using Kind = kb::audio_miniaudio::MiniaudioFilterKind;
constexpr double kPi = 3.141592653589793;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

struct Graph {
    ma_node_graph graph{};
    ma_waveform wave{};
    ma_data_source_node source{};
    MiniaudioFilterChain chain;
    bool graphReady = false;
    bool waveReady = false;
    bool sourceReady = false;
    std::uint32_t channels = 0U;

    ~Graph() {
        chain.Reset();
        if (sourceReady) ma_data_source_node_uninit(&source, nullptr);
        if (waveReady) ma_waveform_uninit(&wave);
        if (graphReady) ma_node_graph_uninit(&graph, nullptr);
    }
    void Initialize(std::uint32_t count, std::uint32_t rate, double frequency, double amplitude = 0.1) {
        channels = count;
        const auto config = ma_node_graph_config_init(channels);
        graphReady = ma_node_graph_init(&config, nullptr, &graph) == MA_SUCCESS;
        Require(graphReady, "DSP test graph did not initialize");
        const auto tone = ma_waveform_config_init(ma_format_f32, channels, rate, ma_waveform_type_sine, amplitude, frequency);
        waveReady = ma_waveform_init(&tone, &wave) == MA_SUCCESS;
        Require(waveReady, "DSP test waveform did not initialize");
        const auto input = ma_data_source_node_config_init(&wave);
        sourceReady = ma_data_source_node_init(&graph, &input, nullptr, &source) == MA_SUCCESS;
        Require(sourceReady, "DSP test source did not initialize");
    }
    void Configure(std::span<const MiniaudioFilterDesc> filters, std::uint32_t rate = 48000U) {
        Require(chain.Initialize(graph, rate, filters) == MA_SUCCESS, "DSP chain did not initialize");
        Require(chain.Attach(&source, ma_node_graph_get_endpoint(&graph)) == MA_SUCCESS, "DSP chain did not attach");
    }
    double Render() {
        std::vector<float> output(4800U * channels);
        ma_uint64 frames = 0U;
        Require(ma_node_graph_read_pcm_frames(&graph, output.data(), 4800U, &frames) == MA_SUCCESS && frames == 4800U,
            "DSP test did not read actual complete PCM frames");
        double energy = 0.0;
        for (const float sample : output) {
            Require(std::isfinite(sample) && std::abs(sample) < 1.0F, "DSP filter produced non-finite or clipped PCM");
            energy += static_cast<double>(sample) * sample;
        }
        return std::sqrt(energy / static_cast<double>(output.size()));
    }
};

double ExpectedGain(const MiniaudioFilterDesc& filter, double rate, double frequency) {
    if (filter.bypass) return 1.0;
    if (filter.kind == Kind::PeakingEqualizer) {
        const double center = 2.0 * kPi * filter.cutoffHz / rate;
        const double amplitude = std::pow(10.0, filter.gainDb / 40.0);
        const double alpha = std::sin(center) / (2.0 * filter.quality);
        const auto z = std::polar(1.0, -2.0 * kPi * frequency / rate);
        const auto middle = -2.0 * std::cos(center) * z;
        const auto numerator = 1.0 + alpha * amplitude + middle + (1.0 - alpha * amplitude) * z * z;
        const auto denominator = 1.0 + alpha / amplitude + middle + (1.0 - alpha / amplitude) * z * z;
        return std::abs(numerator / denominator);
    }
    // Independent bilinear-transform response for a second-order Butterworth filter.
    const double ratio = std::tan(kPi * frequency / rate) / std::tan(kPi * filter.cutoffHz / rate);
    const double low = 1.0 / std::sqrt(1.0 + ratio * ratio * ratio * ratio);
    return filter.kind == Kind::LowPass ? low : low * ratio * ratio;
}

void Response(std::span<const MiniaudioFilterDesc> filters, double frequency, std::uint32_t channels = 2U,
    std::uint32_t rate = 48000U) {
    double gain = 1.0;
    for (const auto& filter : filters) gain *= ExpectedGain(filter, rate, frequency);
    // Reserve headroom for boost and repeated EQ stages; the primitive does not limit output.
    const double amplitude = std::min(0.1, 0.25 / std::max(1.0, gain));
    Graph graph;
    graph.Initialize(channels, rate, frequency, amplitude);
    graph.Configure(filters, rate);
    Require(graph.chain.ActiveNodeCount() == static_cast<std::size_t>(std::count_if(filters.begin(), filters.end(),
        [](const auto& filter) { return !filter.bypass; })), "DSP bypass or repeated stages were lost");
    const double expected = amplitude * gain / std::sqrt(2.0);
    static_cast<void>(graph.Render());
    double rms = 0.0;
    for (std::size_t block = 0U; block < 4U; ++block) {
        rms = graph.Render();
        Require(std::abs(rms / expected - 1.0) < 0.01, "DSP PCM differs from the independent filter response");
    }
    std::cout << "Filter PCM stages=" << filters.size() << " active=" << graph.chain.ActiveNodeCount()
        << " Hz=" << frequency << " rate=" << rate << " channels=" << channels
        << " rms=" << rms << " expected=" << expected << '\n';
}

void InvalidEditsPreserveLiveGraph() {
    Graph graph;
    graph.Initialize(2U, 48000U, 1000.0);
    const std::array original{MiniaudioFilterDesc{}};
    graph.Configure(original);
    static_cast<void>(graph.Render());
    const double before = graph.Render();
    for (const double cutoff : {0.0, -1.0, 24000.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto invalid = original;
        invalid[0].cutoffHz = cutoff;
        Require(graph.chain.Initialize(graph.graph, 48000U, invalid) == MA_INVALID_ARGS, "Invalid cutoff was accepted");
    }
    for (const std::uint32_t order : {0U, 9U, UINT32_MAX}) {
        auto invalid = original;
        invalid[0].order = order;
        Require(graph.chain.Initialize(graph.graph, 48000U, invalid) == MA_INVALID_ARGS, "Invalid filter order was accepted");
    }
    auto invalid = original;
    invalid[0].kind = static_cast<Kind>(255U);
    Require(graph.chain.Initialize(graph.graph, 48000U, invalid) == MA_INVALID_ARGS, "Unknown filter type was accepted");
    const std::vector<MiniaudioFilterDesc> tooMany(17U);
    Require(graph.chain.Initialize(graph.graph, 48000U, tooMany) == MA_INVALID_ARGS
        && graph.chain.Initialize(graph.graph, 0U, original) == MA_INVALID_ARGS
        && graph.chain.Initialize(graph.graph, 7999U, original) == MA_INVALID_ARGS
        && graph.chain.Initialize(graph.graph, 192001U, original) == MA_INVALID_ARGS,
        "Unbounded stage count or invalid sample rate was accepted");
    invalid = original;
    invalid[0].bypass = true;
    invalid[0].cutoffHz = -1.0;
    Require(graph.chain.Initialize(graph.graph, 48000U, invalid) == MA_INVALID_ARGS, "Bypass accepted invalid retained parameters");
    invalid = original;
    invalid[0].gainDb = 6.0;
    Require(graph.chain.Initialize(graph.graph, 48000U, invalid) == MA_INVALID_ARGS, "Low-pass silently ignored an EQ gain");
    invalid = original;
    invalid[0].quality = 2.0;
    Require(graph.chain.Initialize(graph.graph, 48000U, invalid) == MA_INVALID_ARGS, "Low-pass silently ignored an EQ Q value");
    Graph other;
    other.Initialize(1U, 48000U, 1000.0);
    Require(graph.chain.Attach(&graph.source, ma_node_graph_get_endpoint(&other.graph)) == MA_INVALID_ARGS
        && graph.chain.Attach(&graph.source, &graph.source) == MA_INVALID_ARGS
        && graph.chain.Attach(nullptr, nullptr) == MA_INVALID_ARGS, "Invalid graph attachment was accepted");
    Require(graph.chain.ActiveNodeCount() == 1U && std::abs(graph.Render() / before - 1.0) < 0.01,
        "Rejected DSP edit changed the playing graph or signal");
    Graph wide;
    wide.Initialize(9U, 48000U, 1000.0);
    Require(graph.chain.Initialize(wide.graph, 48000U, original) == MA_INVALID_ARGS
        && graph.chain.ActiveNodeCount() == 1U && std::abs(graph.Render() / before - 1.0) < 0.01,
        "An unsupported channel count replaced the playing graph");
    for (std::uint32_t order = 1U; order <= 8U; ++order) {
        const std::array filters{MiniaudioFilterDesc{.order = order}};
        graph.Configure(filters);
        for (std::size_t block = 0U; block < 4U; ++block) static_cast<void>(graph.Render());
    }
    graph.chain.Reset();
    graph.chain.Reset();
    Require(graph.chain.ActiveNodeCount() == 0U, "Reset retained DSP stages");
    graph.Configure({});
    Require(std::abs(graph.Render() / (0.1 / std::sqrt(2.0)) - 1.0) < 0.01, "Reset chain could not restore passthrough");
}

void AllocationFailurePreservesLiveGraph(bool equalizer = false) {
    AllocationCounter counter;
    const ma_allocation_callbacks allocator{&counter, &AllocationCounter::Allocate, &AllocationCounter::Reallocate, &AllocationCounter::Free};
    Graph graph;
    graph.Initialize(2U, 48000U, 1000.0);
    const std::array original{MiniaudioFilterDesc{
        .kind = equalizer ? Kind::PeakingEqualizer : Kind::LowPass, .gainDb = equalizer ? 6.0 : 0.0}};
    Require(graph.chain.Initialize(graph.graph, 48000U, original, &allocator) == MA_SUCCESS
        && graph.chain.Attach(&graph.source, ma_node_graph_get_endpoint(&graph.graph)) == MA_SUCCESS,
        "Counted DSP chain did not initialize");
    static_cast<void>(graph.Render());
    const double before = graph.Render();
    const auto live = counter.live;
    const auto bytes = counter.bytes;
    Require(live > 0U && bytes > 0U, "DSP allocation callbacks did not observe real vendor allocations");
    const std::array replacement{MiniaudioFilterDesc{}, MiniaudioFilterDesc{
        .kind = equalizer ? Kind::PeakingEqualizer : Kind::HighPass, .gainDb = equalizer ? 6.0 : 0.0}, MiniaudioFilterDesc{}};
    MiniaudioFilterChain probe;
    const auto calls = counter.calls;
    Require(probe.Initialize(graph.graph, 48000U, replacement, &allocator) == MA_SUCCESS, "DSP allocation probe failed");
    const auto allocationCount = counter.calls - calls;
    probe.Reset();
    Require(allocationCount > 0U && counter.live == live && counter.bytes == bytes, "DSP allocation probe retained resources");
    for (std::size_t failure = 1U; failure <= allocationCount; ++failure) {
        counter.failAt = counter.calls + failure;
        Require(graph.chain.Initialize(graph.graph, 48000U, replacement, &allocator) == MA_OUT_OF_MEMORY,
            "DSP allocation fault did not report out-of-memory");
        const double after = graph.Render();
        std::cout << "Filter allocation fault=" << failure << " live=" << counter.live << " expectedLive=" << live
            << " vendorBytes=" << counter.bytes << " expectedBytes=" << bytes << " rms=" << after << '\n';
        Require(counter.live == live && counter.bytes == bytes && graph.chain.ActiveNodeCount() == 1U
            && std::abs(after / before - 1.0) < 0.01,
            "Partial DSP allocation failure leaked resources or changed output");
    }
    counter.failAt = 0U;
    graph.chain.Reset();
    Require(counter.live == 0U && counter.bytes == 0U, "DSP reset leaked vendor allocations");
    std::cout << "Filter allocation rollback liveBefore=" << live << " vendorBytes=" << bytes << " peakVendorBytes=" << counter.peak << '\n';
}

void EqualizerResponses() {
    for (const double gain : {-24.0, -12.0, -6.0, 0.0, 6.0, 12.0, 24.0}) {
        const std::array filters{MiniaudioFilterDesc{.kind = Kind::PeakingEqualizer, .gainDb = gain}};
        for (const double frequency : {100.0, 1000.0, 10000.0}) Response(filters, frequency);
    }
    for (const double quality : {0.1, 0.25, 1.0, 4.0, 16.0}) {
        const std::array filters{MiniaudioFilterDesc{.kind = Kind::PeakingEqualizer, .gainDb = 12.0, .quality = quality}};
        for (const double frequency : {500.0, 1000.0, 2000.0}) Response(filters, frequency);
    }
    const MiniaudioFilterDesc boost{.kind = Kind::PeakingEqualizer, .gainDb = 12.0};
    const MiniaudioFilterDesc cut{.kind = Kind::PeakingEqualizer, .gainDb = -12.0};
    Response(std::array{boost, boost}, 1000.0);
    for (const double frequency : {100.0, 1000.0, 10000.0}) Response(std::array{boost, cut}, frequency);
    for (const double frequency : {500.0, 1000.0, 2000.0}) {
        Response(std::array{MiniaudioFilterDesc{}, boost}, frequency);
        Response(std::array{boost, MiniaudioFilterDesc{}}, frequency);
    }
    Response(std::array{MiniaudioFilterDesc{.kind = Kind::PeakingEqualizer, .bypass = true, .gainDb = 24.0}}, 1000.0);
    Response(std::array{cut}, 2000.0, 1U, 44100U);
    Response(std::array{boost}, 1000.0, 8U, 192000U);

    Graph graph;
    graph.Initialize(2U, 48000U, 1000.0);
    const std::array original{boost};
    graph.Configure(original);
    static_cast<void>(graph.Render());
    const auto before = graph.Render();
    const auto rejected = [&](const MiniaudioFilterDesc& invalid) {
        const std::array filters{invalid};
        Require(graph.chain.Initialize(graph.graph, 48000U, filters) == MA_INVALID_ARGS
            && graph.chain.ActiveNodeCount() == 1U && std::abs(graph.Render() / before - 1.0) < 0.01,
            "Invalid EQ edit changed the playing signal");
    };
    for (const double gain : {-24.1, 24.1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto invalid = boost;
        invalid.gainDb = gain;
        rejected(invalid);
    }
    for (const double quality : {-1.0, 0.0, 0.099, 16.1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto invalid = boost;
        invalid.quality = quality;
        rejected(invalid);
    }
    auto invalid = boost;
    invalid.order = 1U;
    rejected(invalid);
    invalid = boost;
    invalid.bypass = true;
    invalid.gainDb = std::numeric_limits<double>::quiet_NaN();
    rejected(invalid);
}

void BoundarySignalsRemainFinite() {
    std::size_t refusals = 0U;
    for (const std::uint32_t rate : {8000U, 192000U}) {
        for (const double frequency : {1.0, 0.49 * rate}) {
            for (auto filter : std::array{
                MiniaudioFilterDesc{.order = 8U}, MiniaudioFilterDesc{.kind = Kind::HighPass, .order = 8U},
                MiniaudioFilterDesc{.kind = Kind::PeakingEqualizer, .gainDb = 24.0, .quality = 0.1},
                MiniaudioFilterDesc{.kind = Kind::PeakingEqualizer, .gainDb = 24.0, .quality = 16.0},
                MiniaudioFilterDesc{.kind = Kind::PeakingEqualizer, .gainDb = -24.0, .quality = 0.1},
                MiniaudioFilterDesc{.kind = Kind::PeakingEqualizer, .gainDb = -24.0, .quality = 16.0}}) {
                Graph graph;
                graph.Initialize(2U, rate, 1000.0, 0.001);
                const std::array original{MiniaudioFilterDesc{}};
                graph.Configure(original, rate);
                static_cast<void>(graph.Render());
                const auto before = graph.Render();
                filter.cutoffHz = frequency;
                const std::array filters{filter};
                const auto result = graph.chain.Initialize(graph.graph, rate, filters);
                if (result == MA_INVALID_ARGS) {
                    ++refusals;
                    Require(frequency == 1.0 && rate == 192000U && graph.chain.ActiveNodeCount() == 1U
                        && std::abs(graph.Render() / before - 1.0) < 0.01,
                        "Numerically unstable filter was not refused without changing the old signal");
                    std::cout << "Filter boundary refused rate=" << rate << " Hz=" << frequency
                        << " kind=" << static_cast<int>(filter.kind) << " gainDb=" << filter.gainDb << " Q=" << filter.quality << '\n';
                    continue;
                }
                Require(result == MA_SUCCESS && graph.chain.Attach(&graph.source, ma_node_graph_get_endpoint(&graph.graph)) == MA_SUCCESS,
                    "Boundary DSP graph did not initialize");
                double rms = 0.0;
                for (std::size_t block = 0U; block < 32U; ++block) {
                    rms = graph.Render();
                    if (rms >= 0.03) std::cout << "Filter boundary failure rate=" << rate << " Hz=" << frequency
                        << " kind=" << static_cast<int>(filter.kind) << " block=" << block << " rms=" << rms << '\n';
                    Require(rms < 0.03, "Boundary DSP parameters produced an unbounded signal");
                }
                std::cout << "Filter boundary rate=" << rate << " Hz=" << frequency << " kind=" << static_cast<int>(filter.kind)
                    << " gainDb=" << filter.gainDb << " Q=" << filter.quality << " rms=" << rms << '\n';
            }
        }
    }
    Require(refusals > 0U, "Quantized unstable coefficients were never refused");
}

void UnstableConfigurationPreservesReferenceSignal() {
    AllocationCounter counter;
    const ma_allocation_callbacks allocator{&counter, &AllocationCounter::Allocate, &AllocationCounter::Reallocate, &AllocationCounter::Free};
    Graph actual;
    Graph reference;
    actual.Initialize(2U, 192000U, 1.0, 0.001);
    reference.Initialize(2U, 192000U, 1.0, 0.001);
    const std::array original{MiniaudioFilterDesc{}};
    actual.Configure(original, 192000U);
    reference.Configure(original, 192000U);
    const std::array unstable{MiniaudioFilterDesc{.cutoffHz = 1.0, .order = 8U}};
    Require(actual.chain.Initialize(actual.graph, 192000U, unstable, &allocator) == MA_INVALID_ARGS
        && actual.chain.ActiveNodeCount() == 1U && counter.live == 0U && counter.bytes == 0U,
        "Known unstable filter replaced the old graph or leaked its candidate");
    double maximumDifference = 0.0;
    for (std::size_t block = 0U; block < 32U; ++block) {
        const double difference = std::abs(actual.Render() - reference.Render());
        maximumDifference = std::max(maximumDifference, difference);
        Require(difference < 1e-9, "Refused unstable filter changed the original 1 Hz signal");
    }
    std::cout << "Filter stability refusal preserved reference maximumRmsDifference=" << maximumDifference
        << " candidateLive=" << counter.live << " candidateBytes=" << counter.bytes << '\n';
}

} // namespace

int main() {
    try {
        Response({}, 1000.0);
        for (const auto kind : {Kind::LowPass, Kind::HighPass}) {
            const std::array filters{MiniaudioFilterDesc{.kind = kind}};
            for (const double frequency : {100.0, 1000.0, 10000.0}) Response(filters, frequency);
        }
        Response(std::array{MiniaudioFilterDesc{}, MiniaudioFilterDesc{}}, 1000.0);
        Response(std::array{MiniaudioFilterDesc{}, MiniaudioFilterDesc{.cutoffHz = 2000.0}}, 1000.0);
        Response(std::array{MiniaudioFilterDesc{}, MiniaudioFilterDesc{.kind = Kind::HighPass}}, 1000.0);
        Response(std::array{MiniaudioFilterDesc{.kind = Kind::HighPass}, MiniaudioFilterDesc{}}, 1000.0);
        Response(std::array{MiniaudioFilterDesc{.bypass = true}}, 10000.0);
        Response(std::array{MiniaudioFilterDesc{.bypass = true}, MiniaudioFilterDesc{.kind = Kind::HighPass}}, 100.0);
        Response(std::array{MiniaudioFilterDesc{}}, 1000.0, 1U, 44100U);
        Response(std::array{MiniaudioFilterDesc{}}, 1000.0, 2U, 96000U);
        Response(std::array{MiniaudioFilterDesc{}}, 1000.0, 1U, 8000U);
        Response(std::array{MiniaudioFilterDesc{}}, 1000.0, 8U, 192000U);
        Response(std::vector<MiniaudioFilterDesc>(16U), 1000.0);
        InvalidEditsPreserveLiveGraph();
        AllocationFailurePreservesLiveGraph();
        AllocationFailurePreservesLiveGraph(true);
        EqualizerResponses();
        BoundarySignalsRemainFinite();
        UnstableConfigurationPreservesReferenceSignal();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
