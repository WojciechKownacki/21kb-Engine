#pragma once

#include <cstdint>

namespace kb::network {
struct NetworkSimulationConfig { std::uint32_t latencyMilliseconds = 0U; std::uint32_t jitterMilliseconds = 0U; std::uint16_t lossPermille = 0U; std::uint16_t reorderPermille = 0U; std::uint64_t disconnectAtTick = 0U; std::uint64_t seed = 1U; };
[[nodiscard]] constexpr bool IsValidNetworkSimulation(NetworkSimulationConfig config) noexcept { return config.jitterMilliseconds<=config.latencyMilliseconds&&config.lossPermille<=1000U&&config.reorderPermille<=1000U&&config.seed!=0U; }
[[nodiscard]] constexpr std::uint64_t NetworkSimulationRandom(NetworkSimulationConfig config, std::uint64_t tick, std::uint64_t sequence) noexcept { std::uint64_t value=config.seed^(tick*0x9E3779B97F4A7C15ULL)^(sequence*0xBF58476D1CE4E5B9ULL); value^=value>>30U; value*=0xBF58476D1CE4E5B9ULL; value^=value>>27U; return value^(value>>31U); }
[[nodiscard]] constexpr bool ShouldDisconnect(NetworkSimulationConfig config, std::uint64_t tick) noexcept { return config.disconnectAtTick!=0U&&tick>=config.disconnectAtTick; }
[[nodiscard]] constexpr bool ShouldDrop(NetworkSimulationConfig config, std::uint64_t tick, std::uint64_t sequence) noexcept { return IsValidNetworkSimulation(config)&&(NetworkSimulationRandom(config,tick,sequence)%1000U)<config.lossPermille; }
} // namespace kb::network
