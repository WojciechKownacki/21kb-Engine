#pragma once

#include "engine/audio/AudioDspEffect.hpp"

#include <miniaudio.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace kb::audio_miniaudio {

using MiniaudioFilterKind = kb::audio::AudioDspKind;
using MiniaudioFilterDesc = kb::audio::AudioDspParameters;

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
    // Commit graph routing and fader changes while the host has excluded PCM reads.
    // Empty chains use the supplied source as their direct output.
    [[nodiscard]] ma_result DetachOutput(ma_node* source);
    [[nodiscard]] ma_result SetOutputVolume(float volume, ma_node* source);
    void Reset() noexcept;
    [[nodiscard]] std::size_t ActiveNodeCount() const noexcept;

private:
    struct Node;
    std::vector<std::unique_ptr<Node>> nodes_;
    ma_node_graph* graph_ = nullptr;
};

} // namespace kb::audio_miniaudio
