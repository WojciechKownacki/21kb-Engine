#include "engine/gameplay/GameState.hpp"

#include <cmath>

namespace kb::gameplay {

bool GameState::SetMatchInProgress(bool value) noexcept { if (snapshot_.matchInProgress == value) return false; snapshot_.matchInProgress = value; Touch(); return true; }
bool GameState::Advance(float deltaSeconds) noexcept { if (!snapshot_.matchInProgress || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) return false; snapshot_.elapsedSeconds += deltaSeconds; Touch(); return true; }
bool GameState::SetScore(std::uint32_t team, std::int32_t score) noexcept { if (team >= snapshot_.scores.size() || snapshot_.scores[team] == score) return false; snapshot_.scores[team] = score; Touch(); return true; }
bool GameState::ApplyReplication(const GameStateSnapshot& snapshot) noexcept { if (snapshot.revision < snapshot_.revision || !std::isfinite(snapshot.elapsedSeconds) || snapshot.elapsedSeconds < 0.0F) return false; snapshot_ = snapshot; return true; }

} // namespace kb::gameplay
