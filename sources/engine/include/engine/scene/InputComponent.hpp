#pragma once

#include "engine/input/InputLocalUser.hpp"

#include <cstdint>

namespace kb::scene {

// Declares that an entity contributes an Input Mapping Context to the runtime
// input subsystem while the scene is playing. Mirrors the data-only style of
// BehaviourComponent (asset referenced by raw stable id).
struct InputComponent {
    std::uint64_t mappingContextAssetId = 0;
    std::int32_t priority = 0;
    bool enabled = true;
    // Which local user's independent InputSubsystem this mapping context is
    // pushed onto. Defaults to the primary user, so every InputComponent authored
    // before LIB-115 keeps behaving exactly as before.
    kb::input::LocalUserId localUser = kb::input::kPrimaryLocalUser;
};

} // namespace kb::scene
