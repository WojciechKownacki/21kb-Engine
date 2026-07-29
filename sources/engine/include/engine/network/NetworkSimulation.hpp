#pragma once

#include <cstdint>
#include <optional>

namespace kb::network {
inline constexpr std::uint32_t kMaximumNetworkSimulationLatencyMilliseconds = 60'000U;
struct NetworkSimulationConfig { std::uint32_t latencyMilliseconds = 0U; std::uint32_t jitterMilliseconds = 0U; std::uint16_t lossPermille = 0U; std::uint16_t reorderPermille = 0U; std::uint64_t disconnectAtTick = 0U; std::uint64_t seed = 1U; };
struct NetworkSimulationDecision { bool dropped = false; bool disconnected = false; bool reordered = false; std::uint32_t deliveryDelayMilliseconds = 0U; };
[[nodiscard]] constexpr bool IsValidNetworkSimulation(NetworkSimulationConfig config) noexcept { return config.latencyMilliseconds<=kMaximumNetworkSimulationLatencyMilliseconds&&config.jitterMilliseconds<=config.latencyMilliseconds&&config.lossPermille<=1000U&&config.reorderPermille<=1000U&&config.seed!=0U; }
[[nodiscard]] constexpr std::uint64_t NetworkSimulationRandom(NetworkSimulationConfig config, std::uint64_t tick, std::uint64_t sequence) noexcept { std::uint64_t value=config.seed^(tick*0x9E3779B97F4A7C15ULL)^(sequence*0xBF58476D1CE4E5B9ULL); value^=value>>30U; value*=0xBF58476D1CE4E5B9ULL; value^=value>>27U; return value^(value>>31U); }
[[nodiscard]] constexpr bool ShouldDisconnect(NetworkSimulationConfig config, std::uint64_t tick) noexcept { return config.disconnectAtTick!=0U&&tick>=config.disconnectAtTick; }
[[nodiscard]] constexpr bool ShouldDrop(NetworkSimulationConfig config, std::uint64_t tick, std::uint64_t sequence) noexcept { return IsValidNetworkSimulation(config)&&(NetworkSimulationRandom(config,tick,sequence)%1000U)<config.lossPermille; }
[[nodiscard]] constexpr bool ShouldReorder(NetworkSimulationConfig config, std::uint64_t tick, std::uint64_t sequence) noexcept { return IsValidNetworkSimulation(config)&&(NetworkSimulationRandom(config,tick,sequence^0xD1B54A32D192ED03ULL)%1000U)<config.reorderPermille; }
[[nodiscard]] constexpr std::optional<std::uint32_t> SimulatedDeliveryDelayMilliseconds(NetworkSimulationConfig config, std::uint64_t tick, std::uint64_t sequence) noexcept { if(!IsValidNetworkSimulation(config))return std::nullopt; const std::uint64_t range=static_cast<std::uint64_t>(config.jitterMilliseconds)*2U+1U; const std::int64_t jitter=static_cast<std::int64_t>(NetworkSimulationRandom(config,tick,sequence^0x94D049BB133111EBULL)%range)-static_cast<std::int64_t>(config.jitterMilliseconds); return static_cast<std::uint32_t>(static_cast<std::int64_t>(config.latencyMilliseconds)+jitter); }
[[nodiscard]] constexpr std::optional<NetworkSimulationDecision> SimulateNetworkPacket(NetworkSimulationConfig config, std::uint64_t tick, std::uint64_t sequence) noexcept { const std::optional<std::uint32_t> delay=SimulatedDeliveryDelayMilliseconds(config,tick,sequence); if(!delay.has_value())return std::nullopt; const bool disconnected=ShouldDisconnect(config,tick); const bool dropped=!disconnected&&ShouldDrop(config,tick,sequence); return NetworkSimulationDecision{.dropped=dropped,.disconnected=disconnected,.reordered=!dropped&&!disconnected&&ShouldReorder(config,tick,sequence),.deliveryDelayMilliseconds=dropped||disconnected?0U:*delay}; }
} // namespace kb::network
