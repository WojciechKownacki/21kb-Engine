#include "runtime/MiniaudioFilterChain.hpp"
#include "runtime/MiniaudioDelayNode.hpp"
#include "runtime/MiniaudioReverbNode.hpp"
#include "runtime/MiniaudioDynamicsNode.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>
#include <variant>

namespace kb::audio_miniaudio {
namespace {

bool StableBiquad(const ma_biquad& filter) noexcept {
    const double a1 = filter.a1.f32;
    const double a2 = filter.a2.f32;
    // Apply the strict Jury conditions to the actual quantized denominator,
    // not the double-precision coefficients used before f32 conversion.
    return std::isfinite(filter.b0.f32) && std::isfinite(filter.b1.f32) && std::isfinite(filter.b2.f32)
        && std::isfinite(a1) && std::isfinite(a2)
        && 1.0 + a1 + a2 > 0.0 && 1.0 - a1 + a2 > 0.0 && 1.0 - a2 > 0.0;
}

bool StableFirstOrder(float coefficient) noexcept {
    return std::isfinite(coefficient) && coefficient > 0.0F && coefficient < 1.0F;
}

std::uint32_t DelayFrames(double milliseconds, std::uint32_t rate) noexcept {
    return std::max(1U, static_cast<std::uint32_t>(std::llround(milliseconds * rate / 1000.0)));
}

} // namespace

struct MiniaudioFilterChain::Node {
    std::variant<ma_lpf_node, ma_hpf_node, ma_peak_node, MiniaudioDelayNode, MiniaudioReverbNode, MiniaudioDynamicsNode> value;
    ma_allocation_callbacks allocations{};
    bool customAllocator = false;
    bool initialized = false;

    const ma_allocation_callbacks* Allocator() const noexcept { return customAllocator ? &allocations : nullptr; }

    ~Node() {
        if (!initialized) return;
        if (auto* low = std::get_if<ma_lpf_node>(&value)) ma_lpf_node_uninit(low, Allocator());
        else if (auto* high = std::get_if<ma_hpf_node>(&value)) ma_hpf_node_uninit(high, Allocator());
        else if (auto* peak = std::get_if<ma_peak_node>(&value)) ma_peak_node_uninit(peak, Allocator());
        // Other stages own independent RAII state, including partial construction.
    }
    ma_node* Native() noexcept {
        if (auto* low = std::get_if<ma_lpf_node>(&value)) return low;
        if (auto* high = std::get_if<ma_hpf_node>(&value)) return high;
        if (auto* peak = std::get_if<ma_peak_node>(&value)) return peak;
        if (auto* reverb = std::get_if<MiniaudioReverbNode>(&value)) return reverb->Native();
        if (auto* dynamics = std::get_if<MiniaudioDynamicsNode>(&value)) return dynamics->Native();
        return std::get<MiniaudioDelayNode>(value).Input();
    }
    ma_node* Output() noexcept {
        if (auto* delay = std::get_if<MiniaudioDelayNode>(&value)) return delay->Output();
        return Native();
    }
    bool Stable() const noexcept {
        if (const auto* low = std::get_if<ma_lpf_node>(&value)) {
            for (ma_uint32 i = 0U; i < low->lpf.lpf1Count; ++i)
                if (!StableFirstOrder(low->lpf.pLPF1[i].a.f32)) return false;
            for (ma_uint32 i = 0U; i < low->lpf.lpf2Count; ++i)
                if (!StableBiquad(low->lpf.pLPF2[i].bq)) return false;
        } else if (const auto* high = std::get_if<ma_hpf_node>(&value)) {
            for (ma_uint32 i = 0U; i < high->hpf.hpf1Count; ++i)
                if (!StableFirstOrder(high->hpf.pHPF1[i].a.f32)) return false;
            for (ma_uint32 i = 0U; i < high->hpf.hpf2Count; ++i)
                if (!StableBiquad(high->hpf.pHPF2[i].bq)) return false;
        } else if (const auto* peak = std::get_if<ma_peak_node>(&value)) {
            return StableBiquad(peak->peak.bq);
        }
        return true;
    }
    ma_result Initialize(ma_node_graph& graph, std::uint32_t rate, const MiniaudioFilterDesc& desc,
        const ma_allocation_callbacks* callbacks) {
        if (callbacks != nullptr) { allocations = *callbacks; customAllocator = true; }
        const auto channels = ma_node_graph_get_channels(&graph);
        ma_result result;
        if (desc.kind == MiniaudioFilterKind::LowPass) {
            const auto config = ma_lpf_node_config_init(channels, rate, desc.cutoffHz, desc.order);
            result = ma_lpf_node_init(&graph, &config, Allocator(), &value.emplace<ma_lpf_node>());
        } else if (desc.kind == MiniaudioFilterKind::HighPass) {
            const auto config = ma_hpf_node_config_init(channels, rate, desc.cutoffHz, desc.order);
            result = ma_hpf_node_init(&graph, &config, Allocator(), &value.emplace<ma_hpf_node>());
        } else if (desc.kind == MiniaudioFilterKind::PeakingEqualizer) {
            const auto config = ma_peak_node_config_init(channels, rate, desc.gainDb, desc.quality, desc.cutoffHz);
            result = ma_peak_node_init(&graph, &config, Allocator(), &value.emplace<ma_peak_node>());
        } else if (desc.kind == MiniaudioFilterKind::Delay) {
            result = value.emplace<MiniaudioDelayNode>().Initialize(graph, rate, DelayFrames(desc.delayMs, rate), desc, Allocator());
        } else if (desc.kind == MiniaudioFilterKind::Reverb) {
            result = value.emplace<MiniaudioReverbNode>().Initialize(graph, rate, desc, Allocator());
        } else {
            result = value.emplace<MiniaudioDynamicsNode>().Initialize(graph, rate, desc, Allocator());
        }
        initialized = result == MA_SUCCESS;
        if (!initialized) {
            // The vendored node initializer can finish the filter allocation before
            // its graph allocation fails. Release only a successfully owned filter;
            // a failed filter initialization has already released its own heap.
            if (auto* low = std::get_if<ma_lpf_node>(&value)) {
                if (low->lpf._ownsHeap) ma_lpf_uninit(&low->lpf, Allocator());
            } else if (auto* high = std::get_if<ma_hpf_node>(&value)) {
                if (high->hpf._ownsHeap) ma_hpf_uninit(&high->hpf, Allocator());
            } else if (auto* peak = std::get_if<ma_peak_node>(&value)) {
                if (peak->peak.bq._ownsHeap) ma_peak2_uninit(&peak->peak, Allocator());
            }
        }
        return result;
    }
};

MiniaudioFilterChain::MiniaudioFilterChain() = default;
MiniaudioFilterChain::~MiniaudioFilterChain() { Reset(); }

ma_result MiniaudioFilterChain::Initialize(ma_node_graph& graph, std::uint32_t sampleRate,
    std::span<const MiniaudioFilterDesc> filters, const ma_allocation_callbacks* allocations) {
    const auto channels = ma_node_graph_get_channels(&graph);
    if (channels == 0U || channels > 8U || sampleRate < 8000U || sampleRate > 192000U || filters.size() > 16U)
        return MA_INVALID_ARGS;
    std::uint64_t stateBudget = 32ULL * 1024ULL * 1024ULL;
    for (const auto& filter : filters) {
        if (!kb::audio::AudioDspParameterError(filter).empty() || filter.cutoffHz > 0.49 * sampleRate
            || filter.order > MA_MAX_FILTER_ORDER) return MA_INVALID_ARGS;
        std::uint64_t bytes = 0U;
        if (!filter.bypass && filter.kind == MiniaudioFilterKind::Delay) {
            bytes = static_cast<std::uint64_t>(DelayFrames(filter.delayMs, sampleRate)) * channels * sizeof(float);
        } else if (!filter.bypass && filter.kind == MiniaudioFilterKind::Reverb) {
            if (channels > 2U || sampleRate < 22050U) return MA_INVALID_ARGS;
            bytes = MiniaudioReverbNode::StateBytes();
        }
        if (bytes > stateBudget) return MA_INVALID_ARGS;
        stateBudget -= bytes;
    }
    std::vector<std::unique_ptr<Node>> candidate;
    try {
        candidate.reserve(filters.size());
        for (const auto& filter : filters) {
            if (filter.bypass) continue;
            auto node = std::make_unique<Node>();
            const auto result = node->Initialize(graph, sampleRate, filter, allocations);
            if (result != MA_SUCCESS) return result;
            if (!node->Stable()) return MA_INVALID_ARGS;
            if (!candidate.empty()) {
                const auto attached = ma_node_attach_output_bus(candidate.back()->Output(), 0U, node->Native(), 0U);
                if (attached != MA_SUCCESS) return attached;
            }
            candidate.push_back(std::move(node));
        }
    } catch (const std::bad_alloc&) {
        return MA_OUT_OF_MEMORY;
    }
    Reset();
    nodes_ = std::move(candidate);
    graph_ = &graph;
    return MA_SUCCESS;
}

ma_result MiniaudioFilterChain::Attach(ma_node* source, ma_node* target) {
    if (graph_ == nullptr || source == nullptr || target == nullptr || source == target
        || ma_node_get_node_graph(source) != graph_ || ma_node_get_node_graph(target) != graph_
        || ma_node_get_output_bus_count(source) == 0U || ma_node_get_input_bus_count(target) == 0U)
        return MA_INVALID_ARGS;
    const auto channels = ma_node_graph_get_channels(graph_);
    if (ma_node_get_output_channels(source, 0U) != channels || ma_node_get_input_channels(target, 0U) != channels)
        return MA_INVALID_ARGS;
    for (const auto& node : nodes_)
        if (node->Native() == source || node->Native() == target || node->Output() == source || node->Output() == target) return MA_INVALID_ARGS;
    if (nodes_.empty()) return ma_node_attach_output_bus(source, 0U, target, 0U);
    const auto result = ma_node_attach_output_bus(nodes_.back()->Output(), 0U, target, 0U);
    return result == MA_SUCCESS ? ma_node_attach_output_bus(source, 0U, nodes_.front()->Native(), 0U) : result;
}

void MiniaudioFilterChain::Reset() noexcept {
    while (!nodes_.empty()) nodes_.pop_back();
    graph_ = nullptr;
}

ma_result MiniaudioFilterChain::DetachOutput(ma_node* source) {
    ma_node* output = nodes_.empty() ? source : nodes_.back()->Output();
    if (graph_ == nullptr || output == nullptr || ma_node_get_node_graph(output) != graph_) return MA_INVALID_ARGS;
    return ma_node_detach_output_bus(output, 0U);
}

ma_result MiniaudioFilterChain::SetOutputVolume(float volume, ma_node* source) {
    ma_node* output = nodes_.empty() ? source : nodes_.back()->Output();
    if (graph_ == nullptr || output == nullptr || ma_node_get_node_graph(output) != graph_
        || !std::isfinite(volume) || volume < 0.0F) return MA_INVALID_ARGS;
    return ma_node_set_output_bus_volume(output, 0U, volume);
}

std::size_t MiniaudioFilterChain::ActiveNodeCount() const noexcept { return nodes_.size(); }

} // namespace kb::audio_miniaudio
