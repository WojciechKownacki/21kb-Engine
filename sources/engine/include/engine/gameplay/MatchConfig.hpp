#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>
#include <vector>

namespace kb::gameplay {
enum class MatchPhase : std::uint8_t { Waiting, Playing, Finished };
struct SpawnPoint { kb::math::Vec3 position{}; std::uint32_t team = 0U; bool enabled = true; };
struct RespawnPolicy { float delaySeconds = 3.0F; bool reuseTeamSpawn = true; };
struct MatchConfig { std::vector<SpawnPoint> spawnPoints; RespawnPolicy respawn{}; std::uint32_t teamCount = 2U; };
class MatchRuntime final { public: explicit MatchRuntime(MatchConfig config = {}) : config_(std::move(config)) {} [[nodiscard]] MatchPhase Phase() const noexcept{return phase_;} [[nodiscard]] bool SetPhase(MatchPhase value) noexcept {if(phase_==value)return false;phase_=value;return true;} [[nodiscard]] const SpawnPoint* SelectSpawn(std::uint32_t team) const noexcept {for(const auto& point:config_.spawnPoints)if(point.enabled&&point.team==team)return &point;return nullptr;} private: MatchConfig config_; MatchPhase phase_=MatchPhase::Waiting;};
} // namespace kb::gameplay
