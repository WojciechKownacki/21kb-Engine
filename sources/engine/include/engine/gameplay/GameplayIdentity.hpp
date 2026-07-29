#pragma once

#include <cstdint>
#include <string_view>

namespace kb::gameplay {

using GameplayTeamId = std::uint32_t;
using GameplayFactionId = std::uint32_t;
using GameplayTagId = std::uint64_t;
using GameplayLayerMask = std::uint32_t;
inline constexpr GameplayTeamId kNoTeam = 0U;
inline constexpr GameplayFactionId kNoFaction = 0U;

// FNV-1a gives every system a stable tag key without sharing strings in hot paths.
[[nodiscard]] constexpr GameplayTagId GameplayTag(std::string_view text) noexcept { GameplayTagId hash=14695981039346656037ULL; for(char c:text){hash^=static_cast<unsigned char>(c);hash*=1099511628211ULL;} return hash; }
struct GameplayIdentity { GameplayTeamId team=kNoTeam; GameplayFactionId faction=kNoFaction; GameplayLayerMask layers=1U; GameplayTagId tag=0U; };
[[nodiscard]] constexpr bool SharesLayer(GameplayIdentity a, GameplayIdentity b) noexcept { return (a.layers & b.layers)!=0U; }
[[nodiscard]] constexpr bool IsFriendly(GameplayIdentity a, GameplayIdentity b) noexcept { return a.team!=kNoTeam && a.team==b.team; }
} // namespace kb::gameplay
