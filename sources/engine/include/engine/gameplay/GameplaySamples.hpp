#pragma once

#include "engine/gameplay/CameraManager.hpp"

#include <array>
#include <string_view>

namespace kb::gameplay {

enum class GameplaySampleKind : std::uint8_t { ThirdPerson, TopDown, Platformer, SimpleShooter, AudioShooter };
enum class GameplaySampleMovement : std::uint8_t { CharacterRelative, ScreenPlane, SideScroll, ForwardFlight };

struct GameplaySampleProfile {
    GameplaySampleKind kind{};
    std::string_view name;
    GameplaySampleMovement movement{};
    CameraPolicy cameraPolicy{};
    bool usesJump = false;
    bool usesCombat = false;
    bool usesAudio = false;
};

[[nodiscard]] constexpr std::array<GameplaySampleProfile, 5U> GameplaySampleProfiles() noexcept {
    return {{
        { GameplaySampleKind::ThirdPerson, "third-person-controller", GameplaySampleMovement::CharacterRelative, CameraPolicy::Follow, true, false, false },
        { GameplaySampleKind::TopDown, "top-down-controller", GameplaySampleMovement::ScreenPlane, CameraPolicy::Follow, false, false, false },
        { GameplaySampleKind::Platformer, "platformer", GameplaySampleMovement::SideScroll, CameraPolicy::Follow, true, false, false },
        { GameplaySampleKind::SimpleShooter, "simple-shooter", GameplaySampleMovement::CharacterRelative, CameraPolicy::Possess, false, true, false },
        { GameplaySampleKind::AudioShooter, "audio-shooter", GameplaySampleMovement::ForwardFlight, CameraPolicy::Possess, false, true, true },
    }};
}

} // namespace kb::gameplay
