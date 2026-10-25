#include "runtime/MiniaudioFilterChain.hpp"

#include <cmath>
#include <new>
#include <utility>
#include <variant>

namespace kb::audio_miniaudio {

struct MiniaudioFilterChain::Node {
    std::variant<ma_lpf_node, ma_hpf_node> value;
    ma_allocation_callbacks allocations{};
    bool customAllocator = false;
    bool initialized = false;

    const ma_allocation_callbacks* Allocator() const noexcept { return customAllocator ? &allocations : nullptr; }

    ~Node() {
        if (!initialized) return;
        if (auto* low = std::get_if<ma_lpf_node>(&value)) ma_lpf_node_uninit(low, Allocator());
        else ma_hpf_node_uninit(&std::get<ma_hpf_node>(value), Allocator());
    }
    ma_node* Native() noexcept {
        if (auto* low = std::get_if<ma_lpf_node>(&value)) return low;
        return &std::get<ma_hpf_node>(value);
    }
    ma_result Initialize(ma_node_graph& graph, std::uint32_t rate, const MiniaudioFilterDesc& desc,
        const ma_allocation_callbacks* callbacks) {
        if (callbacks != nullptr) { allocations = *callbacks; customAllocator = true; }
        const auto channels = ma_node_graph_get_channels(&graph);
        ma_result result;
        if (desc.kind == MiniaudioFilterKind::LowPass) {
            const auto config = ma_lpf_node_config_init(channels, rate, desc.cutoffHz, desc.order);
            result = ma_lpf_node_init(&graph, &config, Allocator(), &value.emplace<ma_lpf_node>());
        } else {
            const auto config = ma_hpf_node_config_init(channels, rate, desc.cutoffHz, desc.order);
            result = ma_hpf_node_init(&graph, &config, Allocator(), &value.emplace<ma_hpf_node>());
        }
        initialized = result == MA_SUCCESS;
        if (!initialized) {
            // The vendored node initializer can finish the filter allocation before
            // its graph allocation fails. Release only a successfully owned filter;
            // a failed filter initialization has already released its own heap.
            if (auto* low = std::get_if<ma_lpf_node>(&value)) {
                if (low->lpf._ownsHeap) ma_lpf_uninit(&low->lpf, Allocator());
            } else {
                auto& high = std::get<ma_hpf_node>(value);
                if (high.hpf._ownsHeap) ma_hpf_uninit(&high.hpf, Allocator());
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
    for (const auto& filter : filters) {
        if ((filter.kind != MiniaudioFilterKind::LowPass && filter.kind != MiniaudioFilterKind::HighPass)
            || !std::isfinite(filter.cutoffHz) || filter.cutoffHz < 1.0
            || filter.cutoffHz > 0.49 * sampleRate || filter.order == 0U || filter.order > MA_MAX_FILTER_ORDER)
            return MA_INVALID_ARGS;
    }
    std::vector<std::unique_ptr<Node>> candidate;
    try {
        candidate.reserve(filters.size());
        for (const auto& filter : filters) {
            if (filter.bypass) continue;
            auto node = std::make_unique<Node>();
            const auto result = node->Initialize(graph, sampleRate, filter, allocations);
            if (result != MA_SUCCESS) return result;
            if (!candidate.empty()) {
                const auto attached = ma_node_attach_output_bus(candidate.back()->Native(), 0U, node->Native(), 0U);
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
    for (const auto& node : nodes_) if (node->Native() == source || node->Native() == target) return MA_INVALID_ARGS;
    if (nodes_.empty()) return ma_node_attach_output_bus(source, 0U, target, 0U);
    const auto result = ma_node_attach_output_bus(nodes_.back()->Native(), 0U, target, 0U);
    return result == MA_SUCCESS ? ma_node_attach_output_bus(source, 0U, nodes_.front()->Native(), 0U) : result;
}

void MiniaudioFilterChain::Reset() noexcept {
    while (!nodes_.empty()) nodes_.pop_back();
    graph_ = nullptr;
}

std::size_t MiniaudioFilterChain::ActiveNodeCount() const noexcept { return nodes_.size(); }

} // namespace kb::audio_miniaudio
