#include "engine/input/InputHaptics.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

namespace kb::input {
namespace {

[[nodiscard]] IInputHapticsBackend* FindBackend(kb::scene::Scene& scene) noexcept {
    return kb::scene::SceneAccess::State(scene).inputHapticsBackend;
}

} // namespace

void InputHaptics::RegisterBackend(kb::scene::Scene& scene, IInputHapticsBackend& backend) {
    kb::scene::SceneAccess::State(scene).inputHapticsBackend = &backend;
}

void InputHaptics::UnregisterBackend(kb::scene::Scene& scene, IInputHapticsBackend& backend) noexcept {
    kb::scene::SceneState& state = kb::scene::SceneAccess::State(scene);
    if (state.inputHapticsBackend == &backend) {
        state.inputHapticsBackend = nullptr;
    }
}

bool InputHaptics::HasBackend(kb::scene::Scene& scene) noexcept {
    return FindBackend(scene) != nullptr;
}

InputHapticsCapability InputHaptics::Capability(kb::scene::Scene& scene, std::uint32_t gamepadIndex) {
    IInputHapticsBackend* backend = FindBackend(scene);
    if (backend == nullptr) {
        return InputHapticsCapability{ .disabledReason = "no haptics backend is registered" };
    }
    return backend->Capability(gamepadIndex);
}

bool InputHaptics::SetVibration(kb::scene::Scene& scene, std::uint32_t gamepadIndex, float lowFrequency, float highFrequency) {
    IInputHapticsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->SetVibration(gamepadIndex, lowFrequency, highFrequency);
}

void InputHaptics::StopAll(kb::scene::Scene& scene) noexcept {
    IInputHapticsBackend* backend = FindBackend(scene);
    if (backend != nullptr) {
        backend->StopAll();
    }
}

} // namespace kb::input
