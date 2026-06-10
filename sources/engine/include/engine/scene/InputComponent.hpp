#pragma once

#include <cstdint>

namespace kb::scene {

// Declares that an entity contributes an Input Mapping Context to the runtime
// input subsystem while the scene is playing. Mirrors the data-only style of
// BehaviourComponent (asset referenced by raw stable id).
struct InputComponent {
    std::uint64_t mappingContextAssetId = 0;
    std::int32_t priority = 0;
    bool enabled = true;
};

} // namespace kb::scene
