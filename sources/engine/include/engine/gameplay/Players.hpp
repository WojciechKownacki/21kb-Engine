#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace kb::gameplay {

using PlayerId = std::uint64_t;

struct PlayerState { std::string displayName; std::int32_t score = 0; std::uint32_t team = 0U; };
struct PlayerController { kb::scene::SceneEntity pawn{}; };
struct Player { PlayerId id = 0U; PlayerState state{}; PlayerController controller{}; };

class Players final {
public:
    [[nodiscard]] bool Join(Player player);
    [[nodiscard]] bool Leave(PlayerId id) noexcept;
    [[nodiscard]] Player* Find(PlayerId id) noexcept;
    [[nodiscard]] const Player* Find(PlayerId id) const noexcept;
    [[nodiscard]] bool Possess(PlayerId id, kb::scene::SceneEntity pawn) noexcept;
    [[nodiscard]] std::uint32_t Count() const noexcept;

private:
    std::unordered_map<PlayerId, Player> players_;
};

} // namespace kb::gameplay
