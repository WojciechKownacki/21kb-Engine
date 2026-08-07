#pragma once

#include "engine/input/InputLocalUser.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct AudioListenerComponent {
    static constexpr std::string_view StableId = "kb21.audio.ear";

    // The runtime owns one physical output mix. The enabled listener with the
    // highest priority drives it; primary and entity id make equal priorities
    // deterministic. localUser binds the authored ear to a local player.
    std::int32_t priority = 0;
    kb::input::LocalUserId localUser = kb::input::kPrimaryLocalUser;
    bool primary = true;
    bool enabled = true;
};

} // namespace kb::scene
