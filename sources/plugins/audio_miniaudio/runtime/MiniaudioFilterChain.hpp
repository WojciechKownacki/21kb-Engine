#pragma once

#include <miniaudio.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace kb::audio_miniaudio {

enum class MiniaudioFilterKind : std::uint8_t { LowPass, HighPass, PeakingEqualizer, Delay };

struct MiniaudioFilterDesc {
    MiniaudioFilterKind kind = MiniaudioFilterKind::LowPass;
    double cutoffHz = 1000.0;
    std::uint32_t order = 2U;
    bool bypass = false;
    double gainDb = 0.0;
    double quality = 1.0;
    double delayMs = 250.0;
    float feedback = 0.0F;
    float wet = 0.5F;
    float dry = 1.0F;
};

// Private graph primitive. The graph and attached source/target must outlive this
// chain. Rebuilds are owner-thread operations, outside concurrent PCM reads.
class MiniaudioFilterChain {
public:
    MiniaudioFilterChain();
    ~MiniaudioFilterChain();
    MiniaudioFilterChain(const MiniaudioFilterChain&) = delete;
    MiniaudioFilterChain& operator=(const MiniaudioFilterChain&) = delete;

    // Builds unattached nodes. Invalid settings or failed construction leave the
    // previous chain intact. A successful rebuild requires Attach again.
    [[nodiscard]] ma_result Initialize(ma_node_graph& graph, std::uint32_t sampleRate,
        std::span<const MiniaudioFilterDesc> filters, const ma_allocation_callbacks* allocations = nullptr);
    [[nodiscard]] ma_result Attach(ma_node* source, ma_node* target);
    void Reset() noexcept;
    [[nodiscard]] std::size_t ActiveNodeCount() const noexcept;

private:
    struct Node;
    std::vector<std::unique_ptr<Node>> nodes_;
    ma_node_graph* graph_ = nullptr;
};

} // namespace kb::audio_miniaudio
