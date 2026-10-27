#pragma once

#include "runtime/MiniaudioFilterChain.hpp"

namespace kb::audio_miniaudio {

// Heap-owned vendor state keeps ordinary filter nodes small. Control-thread
// construction/reset and callback user-data lifetimes follow the filter chain.
class MiniaudioReverbNode {
public:
    MiniaudioReverbNode() = default;
    ~MiniaudioReverbNode();
    MiniaudioReverbNode(const MiniaudioReverbNode&) = delete;
    MiniaudioReverbNode& operator=(const MiniaudioReverbNode&) = delete;

    [[nodiscard]] ma_result Initialize(ma_node_graph& graph, std::uint32_t rate,
        const MiniaudioFilterDesc& desc, const ma_allocation_callbacks* allocations);
    void Reset() noexcept;
    [[nodiscard]] ma_node* Native() noexcept;
    [[nodiscard]] static std::size_t StateBytes() noexcept;

private:
    struct State;
    [[nodiscard]] const ma_allocation_callbacks* Allocator() const noexcept;
    State* state_ = nullptr;
    ma_allocation_callbacks allocations_{};
    bool customAllocator_ = false;
    bool ready_ = false;
};

} // namespace kb::audio_miniaudio
