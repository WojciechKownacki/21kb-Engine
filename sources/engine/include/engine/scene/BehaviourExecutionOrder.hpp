#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::scene {

// The one guaranteed ordering for behaviour dispatch (lifecycle events and
// per-frame ticks alike): BehaviourTickGroup ascending, then
// BehaviourComponent::executionOrder ascending, then SceneEntity::Id()
// ascending as a deterministic tie-breaker. Every caller that iterates
// behaviours in an order a script can observe (ScriptRuntime's per-tick
// dispatch, ScriptRuntimeSceneSystem's Created/Activated/Ready/Deactivated/
// Destroyed transitions) must sort with this comparator instead of keeping a
// private copy, so the guarantee cannot silently diverge between them. Only
// a well-defined ordering for distinct entities: kb::scene::
// SceneBehaviourComponents stores at most one BehaviourComponent per
// entity, so the entity id tie-breaker is always unique within a batch.
//
// Defined inline: this runs as the comparator of a std::ranges::sort over
// every behaviour once per lifecycle phase, every frame, and the engine
// build has no LTO/IPO enabled, so an out-of-line definition would turn
// every pairwise comparison into a real cross-TU call instead of a few
// inlined ALU ops.
[[nodiscard]] inline bool BehaviourExecutionOrderLess(
    SceneEntity lhsEntity,
    const BehaviourComponent& lhsBehaviour,
    SceneEntity rhsEntity,
    const BehaviourComponent& rhsBehaviour) noexcept {
    const auto lhsGroup = static_cast<std::uint8_t>(lhsBehaviour.tickGroup);
    const auto rhsGroup = static_cast<std::uint8_t>(rhsBehaviour.tickGroup);
    if (lhsGroup != rhsGroup) {
        return lhsGroup < rhsGroup;
    }
    if (lhsBehaviour.executionOrder != rhsBehaviour.executionOrder) {
        return lhsBehaviour.executionOrder < rhsBehaviour.executionOrder;
    }
    return lhsEntity.Id() < rhsEntity.Id();
}

} // namespace kb::scene
