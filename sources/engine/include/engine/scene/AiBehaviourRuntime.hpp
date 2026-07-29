#pragma once

#include "engine/scene/AiBehaviourAsset.hpp"

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
    bool initialized = false;
};

class AiBehaviourRuntime final {
public:
    AiBehaviourRuntime() = delete;

    [[nodiscard]] static bool Initialize(const AiBehaviourAsset& asset, AiBehaviourRuntimeState& state);
    [[nodiscard]] static AiExecutionStatus Tick(const AiBehaviourAsset& asset, AiBehaviourRuntimeState& state, const AiBehaviourCallbacks& callbacks) noexcept;
    static void Reset(const AiBehaviourAsset& asset, AiBehaviourRuntimeState& state) noexcept;
};

} // namespace kb::scene
