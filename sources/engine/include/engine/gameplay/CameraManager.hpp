#pragma once

#include "engine/gameplay/Players.hpp"

#include <unordered_map>

namespace kb::gameplay {

enum class CameraPolicy : std::uint8_t { Possess, Follow, Spectate };
struct PlayerCameraView { kb::scene::SceneEntity camera{}; kb::scene::SceneEntity target{}; CameraPolicy policy = CameraPolicy::Possess; };

class CameraManager final {
public:
    [[nodiscard]] bool SetView(PlayerId player, PlayerCameraView view) noexcept;
    [[nodiscard]] const PlayerCameraView* FindView(PlayerId player) const noexcept;
    [[nodiscard]] bool ClearView(PlayerId player) noexcept;
private:
    std::unordered_map<PlayerId, PlayerCameraView> views_;
};

} // namespace kb::gameplay
