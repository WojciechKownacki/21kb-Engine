#pragma once

#include "runtime/MiniaudioFilterChain.hpp"

namespace kb::audio_miniaudio {

// Linked-channel peak dynamics. Parameters are immutable during PCM reads;
// construction/reset and allocation callback lifetimes follow the filter chain.
class MiniaudioDynamicsNode {
public:
    MiniaudioDynamicsNode() = default;
    ~MiniaudioDynamicsNode();
    MiniaudioDynamicsNode(const MiniaudioDynamicsNode&) = delete;
    MiniaudioDynamicsNode& operator=(const MiniaudioDynamicsNode&) = delete;

    [[nodiscard]] ma_result Initialize(ma_node_graph& graph, std::uint32_t rate,
        const MiniaudioFilterDesc& desc, const ma_allocation_callbacks* allocations);
    void Reset() noexcept;
    [[nodiscard]] ma_node* Native() noexcept { return &state_.node; }

private:
    struct State {
        ma_node_base node{};
        std::uint32_t channels = 0U;
        bool limiter = false;
        double threshold = 1.0, exponent = 0.0, makeup = 1.0;
        double attack = 0.0, release = 0.0, gain = 1.0;
    };
    static void Process(ma_node* node, const float** input, ma_uint32* inputFrames,
        float** output, ma_uint32* outputFrames);
    [[nodiscard]] const ma_allocation_callbacks* Allocator() const noexcept;
    State state_;
    ma_allocation_callbacks allocations_{};
    bool customAllocator_ = false;
    bool ready_ = false;
};

} // namespace kb::audio_miniaudio
