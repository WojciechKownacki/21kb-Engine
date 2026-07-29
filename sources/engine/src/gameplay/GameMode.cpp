#include "engine/gameplay/GameMode.hpp"

namespace kb::gameplay {

GameMode::GameMode(GameModeRules rules) noexcept : rules_(rules) {
    if (rules_.maxPlayers == 0U) rules_.maxPlayers = 1U;
    if (rules_.authority == GameAuthority::SinglePlayer) {
        rules_.maxPlayers = 1U;
        rules_.allowJoinInProgress = false;
    }
}

bool GameMode::CanAcceptPlayer(std::uint32_t currentPlayers, bool matchInProgress) const noexcept {
    return currentPlayers < rules_.maxPlayers && (!matchInProgress || rules_.allowJoinInProgress);
}

} // namespace kb::gameplay
