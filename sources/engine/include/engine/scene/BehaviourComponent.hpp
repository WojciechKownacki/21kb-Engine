#pragma once

#include <cstdint>

namespace kb::scene {

enum class BehaviourBackend : std::uint8_t {
    Native,
    Lua,
    VisualGraph,
};

enum class BehaviourTickGroup : std::uint8_t {
    Input,
    Gameplay,
    Physics,
    Animation,
    Camera,
    Presentation,
};

struct BehaviourComponent {
    std::uint64_t behaviourAssetId = 0;
    BehaviourBackend backend = BehaviourBackend::VisualGraph;
    bool enabled = true;
    BehaviourTickGroup tickGroup = BehaviourTickGroup::Gameplay;
    std::int32_t executionOrder = 0;
};

} // namespace kb::scene
