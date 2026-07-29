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
inline constexpr GameplayLayerMask kAllGameplayLayers = 0x7FFFFFFFU;

// FNV-1a gives every system a stable tag key without sharing strings in hot paths.
[[nodiscard]] constexpr GameplayTagId GameplayTag(std::string_view text) noexcept { GameplayTagId hash=14695981039346656037ULL; for(char c:text){hash^=static_cast<unsigned char>(c);hash*=1099511628211ULL;} return hash; }
struct GameplayIdentity { GameplayTeamId team=kNoTeam; GameplayFactionId faction=kNoFaction; GameplayLayerMask layers=1U; GameplayTagId tag=0U; };
[[nodiscard]] constexpr bool SharesLayer(GameplayIdentity a, GameplayIdentity b) noexcept { return (a.layers & b.layers)!=0U; }
[[nodiscard]] constexpr bool IsFriendly(GameplayIdentity a, GameplayIdentity b) noexcept { return a.team!=kNoTeam && a.team==b.team; }

enum class GameplayRelation : std::uint8_t { Any, Friendly, Hostile };

// One value filter used at gameplay boundaries. Physics and rendering consume
// LayerMask(); AI consumes Accepts(); damage uses the same relation check.
// Team/faction/tag zero means "do not require this attribute".
struct GameplayIdentityFilter {
    GameplayTeamId requiredTeam = kNoTeam;
    GameplayFactionId requiredFaction = kNoFaction;
    GameplayLayerMask layerMask = kAllGameplayLayers;
    GameplayTagId requiredTag = 0U;
    GameplayRelation relation = GameplayRelation::Any;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return layerMask != 0U; }
    [[nodiscard]] constexpr bool MatchesLayer(GameplayLayerMask candidateLayers) const noexcept {
        return (layerMask & candidateLayers) != 0U;
    }
    [[nodiscard]] constexpr bool Accepts(GameplayIdentity observer, GameplayIdentity target) const noexcept {
        if (!IsValid() || !MatchesLayer(target.layers) ||
            (requiredTeam != kNoTeam && target.team != requiredTeam) ||
            (requiredFaction != kNoFaction && target.faction != requiredFaction) ||
            (requiredTag != 0U && target.tag != requiredTag)) return false;
        return relation == GameplayRelation::Any ||
            (relation == GameplayRelation::Friendly ? IsFriendly(observer, target) : !IsFriendly(observer, target));
    }
    // These adapters deliberately return the shared bit mask unchanged; they
    // prevent AI/physics/render callers from inventing separate layer rules.
    [[nodiscard]] constexpr GameplayLayerMask PhysicsQueryMask() const noexcept { return layerMask; }
    [[nodiscard]] constexpr GameplayLayerMask RenderCullingMask() const noexcept { return layerMask; }
};
} // namespace kb::gameplay
