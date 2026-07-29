#pragma once

#include "engine/scene/AiBehaviourAsset.hpp"
#include "engine/math/EngineMath.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

enum class AiExecutionStatus : std::uint8_t {
    Success,
    Failure,
    Running,
    Invalid,
};

// Callbacks are invoked synchronously from Tick. They intentionally receive
// only authored ids: gameplay owns all mutable world state and can decide
// whether an action calls native code, emits an existing ScriptEvent, or does
// nothing. The runtime itself never schedules a script.
using AiConditionCallback = bool (*)(void* context, AiNodeId id) noexcept;
using AiActionCallback = AiExecutionStatus (*)(void* context, AiNodeId id) noexcept;
using AiUtilityCallback = float (*)(void* context, AiNodeId id) noexcept;

struct AiBehaviourCallbacks {
    void* context = nullptr;
    AiConditionCallback condition = nullptr;
    AiActionCallback action = nullptr;
    AiUtilityCallback utility = nullptr;
};

struct AiBehaviourRuntimeState {
    std::uint32_t activeState = 0U;
    // Allocated/rebuilt only by Initialize. A tick merely updates these
    // cursors to resume sequence/selector children that returned Running.
    std::vector<std::uint32_t> childCursors;
    kb::math::RandomStream random{};
    std::uint64_t decisionTick = 0U;
    AiExecutionStatus lastStatus = AiExecutionStatus::Invalid;
    AiNodeId lastRoot = 0U;
    bool initialized = false;
};

struct AiDecisionSnapshot {
    std::uint64_t tick = 0U;
    std::uint32_t activeState = 0U;
    AiNodeId root = 0U;
    AiExecutionStatus status = AiExecutionStatus::Invalid;
    std::uint32_t randomSeed = 0U;
    std::uint32_t randomCounter = 0U;
};

class AiBehaviourRuntime final {
public:
    AiBehaviourRuntime() = delete;

    [[nodiscard]] static bool Initialize(const AiBehaviourAsset& asset, AiBehaviourRuntimeState& state, std::uint32_t seed = 0U);
    [[nodiscard]] static AiExecutionStatus Tick(const AiBehaviourAsset& asset, AiBehaviourRuntimeState& state, const AiBehaviourCallbacks& callbacks) noexcept;
    [[nodiscard]] static kb::math::RandomStreamUInt32Result NextRandom(AiBehaviourRuntimeState& state) noexcept;
    [[nodiscard]] static AiDecisionSnapshot Snapshot(const AiBehaviourRuntimeState& state) noexcept;
    static void Reset(const AiBehaviourAsset& asset, AiBehaviourRuntimeState& state) noexcept;
};

} // namespace kb::scene
