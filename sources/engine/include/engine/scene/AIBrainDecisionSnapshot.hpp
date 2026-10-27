#pragma once

#include "engine/scene/AIBrainComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

enum class AIBrainDecisionState : std::uint8_t { Pending, Running, Disabled, Inactive, Dead };

// Runtime diagnostics published by the AI provider. Not part of authored scene
// serialization; a missing snapshot means the provider has not visited the brain.
struct AIBrainDecisionSnapshot {
    AIBrainDecisionState state = AIBrainDecisionState::Pending;
    int ruleIndex = -1;
    AICondition condition = AICondition::Never;
    float conditionValue = 0.0F;
    AIAction action = AIAction::Idle;
    float actionValue = 0.0F;
    SceneEntity enemy{};
    std::uint64_t frameIndex = 0U;
};

} // namespace kb::scene
