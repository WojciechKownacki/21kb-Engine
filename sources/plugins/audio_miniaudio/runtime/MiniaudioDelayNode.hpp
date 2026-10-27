#pragma once

#include "runtime/MiniaudioFilterChain.hpp"

namespace kb::audio_miniaudio {

// Standard parallel dry/wet delay assembled from existing graph nodes. The
// source, graph and callback user data outlive this object; resets are owned
// by the control thread outside concurrent PCM reads.
class MiniaudioDelayNode {
public:
    MiniaudioDelayNode() = default;
    ~MiniaudioDelayNode();
    MiniaudioDelayNode(const MiniaudioDelayNode&) = delete;
    MiniaudioDelayNode& operator=(const MiniaudioDelayNode&) = delete;

    [[nodiscard]] ma_result Initialize(ma_node_graph& graph, std::uint32_t rate,
        std::uint32_t frames, const MiniaudioFilterDesc& desc, const ma_allocation_callbacks* allocations);
    void Reset() noexcept;
    [[nodiscard]] ma_node* Input() noexcept { return &input_; }
    [[nodiscard]] ma_node* Output() noexcept { return &output_; }

private:
    [[nodiscard]] const ma_allocation_callbacks* Allocator() const noexcept;
    ma_splitter_node input_{};
    ma_delay_node delay_{};
    ma_splitter_node output_{};
    ma_allocation_callbacks allocations_{};
    bool customAllocator_ = false;
    bool inputReady_ = false;
    bool delayReady_ = false;
    bool outputReady_ = false;
};

} // namespace kb::audio_miniaudio
