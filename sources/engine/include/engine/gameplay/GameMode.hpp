#pragma once

#include <cstdint>

namespace kb::gameplay {

enum class GameAuthority : std::uint8_t { SinglePlayer, Server };

struct GameModeRules {
    GameAuthority authority = GameAuthority::SinglePlayer;
    std::uint32_t maxPlayers = 1U;
    bool allowJoinInProgress = false;
};

// GameMode is owned by GameInstance, never by a Scene entity. It provides the
// authoritative admission policy; player ownership/replication remain later
// gameplay-framework responsibilities.
class GameMode final {
public:
    explicit GameMode(GameModeRules rules = {}) noexcept;
    [[nodiscard]] const GameModeRules& Rules() const noexcept { return rules_; }
    [[nodiscard]] bool IsAuthoritative() const noexcept { return rules_.authority == GameAuthority::Server || rules_.authority == GameAuthority::SinglePlayer; }
    [[nodiscard]] bool CanAcceptPlayer(std::uint32_t currentPlayers, bool matchInProgress) const noexcept;

private:
    GameModeRules rules_;
};

} // namespace kb::gameplay
