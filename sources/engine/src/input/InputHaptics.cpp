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

bool InputHaptics::BindLocalUser(kb::scene::Scene& scene, LocalUserId localUser, std::uint32_t gamepadIndex) {
    IInputHapticsBackend* backend = FindBackend(scene);
    if (backend == nullptr) {
        return false;
    }
    const InputHapticsCapability capability = backend->Capability(gamepadIndex);
    if (capability.maxGamepads == 0U || gamepadIndex >= capability.maxGamepads) {
        return false;
    }
    kb::scene::SceneAccess::State(scene).hapticsGamepadByLocalUser[localUser.value] = gamepadIndex;
    return true;
}

std::uint32_t InputHaptics::GamepadForLocalUser(const kb::scene::Scene& scene, LocalUserId localUser) noexcept {
    const kb::scene::SceneState& state = kb::scene::SceneAccess::State(scene);
    const auto iterator = state.hapticsGamepadByLocalUser.find(localUser.value);
    return iterator == state.hapticsGamepadByLocalUser.end() ? localUser.value : iterator->second;
}

InputHapticsCapability InputHaptics::CapabilityForLocalUser(kb::scene::Scene& scene, LocalUserId localUser) {
    return Capability(scene, GamepadForLocalUser(scene, localUser));
}

bool InputHaptics::SetVibrationForLocalUser(
    kb::scene::Scene& scene,
    LocalUserId localUser,
    float lowFrequency,
    float highFrequency) {
    return SetVibration(scene, GamepadForLocalUser(scene, localUser), lowFrequency, highFrequency);
}

void InputHaptics::StopAll(kb::scene::Scene& scene) noexcept {
    IInputHapticsBackend* backend = FindBackend(scene);
    if (backend != nullptr) {
        backend->StopAll();
    }
}

} // namespace kb::input
