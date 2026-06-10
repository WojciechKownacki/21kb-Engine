#pragma once

#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputMappingContextStack.hpp"
#include "engine/input/InputTriggers.hpp"
#include "engine/input/InputValue.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kb::input {

// The phase an action transitioned through this frame, emitted as script events.
enum class InputActionPhase : std::uint8_t {
    Started,   // First frame the action began evaluating (None -> Ongoing/Triggered).
    Triggered, // Fires this frame.
    Completed, // Was triggered and has now stopped.
    Canceled,  // Was ongoing but stopped before triggering.
};

// Per-frame resolved state of a single named action.
struct InputActionState {
    InputValue value{};
    bool started = false;
    bool triggered = false;
    bool completed = false;
    bool canceled = false;
    TriggerState combined = TriggerState::None;
};

struct InputActionEvent {
    std::string action;
    InputActionPhase phase = InputActionPhase::Triggered;
    InputValue value{};
};

// The runtime input subsystem. Platform code fills MutableDeviceState() each frame;
// the polling system calls Evaluate(dt); scripts query action states by name.
//
// This is the engine-side equivalent of Unreal's Enhanced Input Local Player
// Subsystem: it owns a prioritized stack of mapping contexts and resolves them
// into per-action values + trigger events every frame.
class InputSubsystem {
public:
    using ActionResolver = InputMappingContextStack::ActionResolver;
    using ContextResolver = InputMappingContextStack::ContextResolver;

    // Resolvers translate asset ids into asset data (typically backed by the
    // scene's AssetManager). Required before AddMappingContext can resolve names.
    void SetResolvers(ActionResolver actionResolver, ContextResolver contextResolver);

    [[nodiscard]] InputDeviceState& MutableDeviceState() noexcept {
        return deviceState_;
    }
    [[nodiscard]] const InputDeviceState& DeviceState() const noexcept {
        return deviceState_;
    }

    // Pushes a mapping context (resolved via the configured resolvers). Higher
    // priority wins for consume-gated keys. Re-adding an existing context updates
    // its priority. Returns false if the context could not be resolved.
    bool AddMappingContext(std::uint64_t contextId, std::int32_t priority);
    void RemoveMappingContext(std::uint64_t contextId);
    void ClearMappingContexts() noexcept;
    [[nodiscard]] bool HasMappingContext(std::uint64_t contextId) const noexcept;

    // Recomputes every action's value and trigger events for this frame.
    void Evaluate(float deltaSeconds);

    // --- Queries ---
    [[nodiscard]] bool IsActionPressed(std::string_view action) const;
    [[nodiscard]] InputValue GetActionValue(std::string_view action) const;
    [[nodiscard]] bool WasActionStarted(std::string_view action) const;
    [[nodiscard]] bool WasActionTriggered(std::string_view action) const;
    [[nodiscard]] bool WasActionCompleted(std::string_view action) const;

    [[nodiscard]] const std::vector<InputActionEvent>& FrameEvents() const noexcept {
        return frameEvents_;
    }

private:
    [[nodiscard]] const InputActionState* FindState(std::string_view action) const;

    InputDeviceState deviceState_;
    InputMappingContextStack stack_;
    std::unordered_map<std::string, InputActionState> actionStates_;
    std::unordered_map<std::string, TriggerState> previousCombined_;
    std::vector<InputActionEvent> frameEvents_;
};

} // namespace kb::input
