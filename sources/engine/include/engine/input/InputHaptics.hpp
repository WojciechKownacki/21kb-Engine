#pragma once

#include "engine/input/InputLocalUser.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::input {

// LIB-153: one device's haptics capability - the honest "can this platform/device rumble,
// and within what limits" answer (mirror of RendererCapabilityReport's supported+limits+
// reason shape). `supported=false` carries `disabledReason` instead of a fake no-op
// actuator, exactly like LibraryModuleDesc::capability. XInput-class devices expose two
// motor magnitudes (low/high frequency) and nothing else - no per-trigger haptics, no
// LEDs - so the limits are deliberately this small.
struct InputHapticsCapability {
    bool supported = false;
    bool connected = false;
    // Dual-motor magnitude control (the only actuator model v1 exposes).
    bool dualMotor = false;
    std::uint32_t maxGamepads = 0U;
    std::string disabledReason;
};

// LIB-153: the host-registered haptics actuator. Backends address physical gamepad slots;
// InputHaptics owns the scene-local LocalUserId -> slot routing above this interface.
class IInputHapticsBackend {
public:
    virtual ~IInputHapticsBackend() = default;

    [[nodiscard]] virtual InputHapticsCapability Capability(std::uint32_t gamepadIndex) = 0;
    // Motor magnitudes in [0,1] (backends clamp). False when the index is out of the
    // platform's range or the device is not connected - never a silent success.
    [[nodiscard]] virtual bool SetVibration(std::uint32_t gamepadIndex, float lowFrequency, float highFrequency) = 0;
    virtual void StopAll() noexcept = 0;
};

// LIB-153: the scene-registered facade, mirroring kb::audio::AudioPlayback exactly - the
// host that owns the physical devices (the editor's Win32/XInput layer today) registers a
// backend per scene; without one every query returns the honest empty capability
// (supported=false, disabledReason="no haptics backend is registered") and every actuator
// call returns false.
class InputHaptics final {
public:
    InputHaptics() = delete;

    static void RegisterBackend(kb::scene::Scene& scene, IInputHapticsBackend& backend);
    static void UnregisterBackend(kb::scene::Scene& scene, IInputHapticsBackend& backend) noexcept;
    [[nodiscard]] static bool HasBackend(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static InputHapticsCapability Capability(kb::scene::Scene& scene, std::uint32_t gamepadIndex);
    [[nodiscard]] static bool SetVibration(kb::scene::Scene& scene, std::uint32_t gamepadIndex, float lowFrequency, float highFrequency);
    [[nodiscard]] static bool BindLocalUser(kb::scene::Scene& scene, LocalUserId localUser, std::uint32_t gamepadIndex);
    [[nodiscard]] static std::uint32_t GamepadForLocalUser(const kb::scene::Scene& scene, LocalUserId localUser) noexcept;
    [[nodiscard]] static InputHapticsCapability CapabilityForLocalUser(kb::scene::Scene& scene, LocalUserId localUser);
    [[nodiscard]] static bool SetVibrationForLocalUser(
        kb::scene::Scene& scene,
        LocalUserId localUser,
        float lowFrequency,
        float highFrequency);
    static void StopAll(kb::scene::Scene& scene) noexcept;
};

} // namespace kb::input
