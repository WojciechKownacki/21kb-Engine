#pragma once

#include <cstdint>

namespace kb::scene {

enum class BehaviourBackend : std::uint8_t {
    Native,
    Lua,
    VisualGraph,
};

struct BehaviourComponent {
    std::uint64_t behaviourAssetId = 0;
    BehaviourBackend backend = BehaviourBackend::VisualGraph;
    bool enabled = true;
};

} // namespace kb::scene
