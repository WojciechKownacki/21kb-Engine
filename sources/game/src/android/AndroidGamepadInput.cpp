#include "AndroidGamepadInput.hpp"

#include <android/input.h>
#include <game-activity/GameActivity.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace kb::game {
namespace {

using input::InputKey;
constexpr std::array kButtons{
    std::pair{InputKey::GamepadFaceBottom, PADDLEBOAT_BUTTON_A},
    std::pair{InputKey::GamepadFaceRight, PADDLEBOAT_BUTTON_B},
    std::pair{InputKey::GamepadFaceLeft, PADDLEBOAT_BUTTON_X},
    std::pair{InputKey::GamepadFaceTop, PADDLEBOAT_BUTTON_Y},
    std::pair{InputKey::GamepadDPadUp, PADDLEBOAT_BUTTON_DPAD_UP},
    std::pair{InputKey::GamepadDPadDown, PADDLEBOAT_BUTTON_DPAD_DOWN},
    std::pair{InputKey::GamepadDPadLeft, PADDLEBOAT_BUTTON_DPAD_LEFT},
    std::pair{InputKey::GamepadDPadRight, PADDLEBOAT_BUTTON_DPAD_RIGHT},
    std::pair{InputKey::GamepadLeftShoulder, PADDLEBOAT_BUTTON_L1},
    std::pair{InputKey::GamepadRightShoulder, PADDLEBOAT_BUTTON_R1},
    std::pair{InputKey::GamepadLeftThumb, PADDLEBOAT_BUTTON_L3},
    std::pair{InputKey::GamepadRightThumb, PADDLEBOAT_BUTTON_R3},
    std::pair{InputKey::GamepadStart, PADDLEBOAT_BUTTON_START},
    std::pair{InputKey::GamepadBack, PADDLEBOAT_BUTTON_SELECT},
};

[[nodiscard]] float CenteredAxis(float value, float flat) noexcept {
    if (!std::isfinite(value)) return 0.0F;
    flat = std::isfinite(flat) ? std::clamp(flat, 0.0F, 1.0F) : 0.0F;
    return std::abs(value) <= flat ? 0.0F : std::clamp(value, -1.0F, 1.0F);
}

[[nodiscard]] float Trigger(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
}

} // namespace

void ApplyAndroidGamepadSample(input::InputDeviceState& state, std::uint8_t slot,
    const Paddleboat_Controller_Data* sample, const Paddleboat_Controller_Info* info) noexcept {
    if (slot >= input::InputDeviceState::kMaxGamepads) return;
    const bool connected = sample != nullptr && info != nullptr;
    state.SetGamepadConnected(slot, connected);
    const Paddleboat_Controller_Data empty{};
    const Paddleboat_Controller_Info noInfo{};
    const auto& data = connected ? *sample : empty;
    const auto& device = connected ? *info : noInfo;
    for (const auto& [key, mask] : kButtons) {
        state.SetKeyDown(key, (data.buttonsDown & static_cast<std::uint32_t>(mask)) != 0U, slot);
    }
    state.SetAnalog(InputKey::GamepadLeftStickX,
        CenteredAxis(data.leftStick.stickX, device.leftStickPrecision.stickFlatX), slot);
    state.SetAnalog(InputKey::GamepadLeftStickY,
        -CenteredAxis(data.leftStick.stickY, device.leftStickPrecision.stickFlatY), slot);
    state.SetAnalog(InputKey::GamepadRightStickX,
        CenteredAxis(data.rightStick.stickX, device.rightStickPrecision.stickFlatX), slot);
    state.SetAnalog(InputKey::GamepadRightStickY,
        -CenteredAxis(data.rightStick.stickY, device.rightStickPrecision.stickFlatY), slot);
    state.SetAnalog(InputKey::GamepadLeftTrigger, Trigger(data.triggerL2), slot);
    state.SetAnalog(InputKey::GamepadRightTrigger, Trigger(data.triggerR2), slot);
}

AndroidGamepadInput::~AndroidGamepadInput() { Shutdown(); }

bool AndroidGamepadInput::Initialize(GameActivity& activity) noexcept {
    Shutdown();
    vm_ = activity.vm;
    if (vm_ == nullptr) return false;
    const auto status = vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (vm_->AttachCurrentThread(&env_, nullptr) != JNI_OK) {
            env_ = nullptr;
            return false;
        }
        attached_ = true;
    } else if (status != JNI_OK) {
        env_ = nullptr;
        return false;
    }
    initialized_ = Paddleboat_init(env_, activity.javaGameActivity) == PADDLEBOAT_NO_ERROR;
    if (!initialized_) {
        Shutdown();
        return false;
    }
    // Connection callbacks also let the library finish pending device setup.
    Paddleboat_setControllerStatusCallback([](int32_t slot, Paddleboat_ControllerStatus status, void* context) {
        auto& self = *static_cast<AndroidGamepadInput*>(context);
        if (slot >= 0 && static_cast<std::size_t>(slot) < self.connected_.size()) {
            self.connected_[static_cast<std::size_t>(slot)] = status == PADDLEBOAT_CONTROLLER_JUST_CONNECTED ||
                status == PADDLEBOAT_CONTROLLER_ACTIVE;
            if (!self.connected_[static_cast<std::size_t>(slot)]) self.vibration_[static_cast<std::size_t>(slot)] = {};
        }
    }, this);
    // Enable standard axes before the first queued event; device-specific mappings
    // discovered by the controller library add their axes during Update.
    for (const auto axis : {AMOTION_EVENT_AXIS_X, AMOTION_EVENT_AXIS_Y,
             AMOTION_EVENT_AXIS_Z, AMOTION_EVENT_AXIS_RZ, AMOTION_EVENT_AXIS_RX,
             AMOTION_EVENT_AXIS_RY, AMOTION_EVENT_AXIS_HAT_X, AMOTION_EVENT_AXIS_HAT_Y,
             AMOTION_EVENT_AXIS_LTRIGGER, AMOTION_EVENT_AXIS_RTRIGGER,
             AMOTION_EVENT_AXIS_BRAKE, AMOTION_EVENT_AXIS_GAS}) {
        GameActivityPointerAxes_enableAxis(axis);
        enabledAxes_ |= std::uint64_t{1U} << axis;
    }
    Update();
    return true;
}

void AndroidGamepadInput::Shutdown() noexcept {
    StopAll();
    focused_ = false;
    if (initialized_) {
        Paddleboat_setControllerStatusCallback(nullptr, nullptr);
        Paddleboat_destroy(env_);
    }
    initialized_ = false;
    if (attached_ && vm_ != nullptr) static_cast<void>(vm_->DetachCurrentThread());
    attached_ = false;
    env_ = nullptr;
    vm_ = nullptr;
    enabledAxes_ = 0U;
    connected_.fill(false);
}

void AndroidGamepadInput::OnStart() noexcept {
    if (initialized_) Paddleboat_onStart(env_);
}

void AndroidGamepadInput::OnStop() noexcept {
    StopAll();
    focused_ = false;
    if (initialized_) Paddleboat_onStop(env_);
}

void AndroidGamepadInput::Update() noexcept {
    if (!initialized_) return;
    Paddleboat_update(env_);
    const auto active = Paddleboat_getActiveAxisMask();
    const auto added = active & ~enabledAxes_;
    for (int axis = 0; axis < 64; ++axis) {
        if ((added & (std::uint64_t{1U} << axis)) != 0U) GameActivityPointerAxes_enableAxis(axis);
    }
    enabledAxes_ |= active;
    const auto now = std::chrono::steady_clock::now();
    for (std::uint32_t slot = 0; slot < vibration_.size(); ++slot) {
        auto& effect = vibration_[slot];
        if ((effect.low > 0.0F || effect.high > 0.0F) && now >= effect.next) {
            if (!focused_ || !connected_[slot] || !SendVibration(slot, effect.low, effect.high)) effect = {};
        }
    }
}

bool AndroidGamepadInput::Process(const GameActivityKeyEvent& event) noexcept {
    return initialized_ && Paddleboat_processGameActivityKeyInputEvent(&event, sizeof(event)) != 0;
}

bool AndroidGamepadInput::Process(const GameActivityMotionEvent& event) noexcept {
    return initialized_ && Paddleboat_processGameActivityMotionInputEvent(&event, sizeof(event)) != 0;
}

void AndroidGamepadInput::Publish(input::InputDeviceState& state, bool focused) noexcept {
    focused_ = focused;
    if (!focused) StopAll();
    static_assert(input::InputDeviceState::kMaxGamepads <= PADDLEBOAT_MAX_CONTROLLERS);
    for (std::uint8_t slot = 0U; slot < input::InputDeviceState::kMaxGamepads; ++slot) {
        Paddleboat_Controller_Data data{};
        Paddleboat_Controller_Info info{};
        const bool connected = initialized_ && connected_[slot] &&
            Paddleboat_getControllerData(slot, &data) == PADDLEBOAT_NO_ERROR &&
            Paddleboat_getControllerInfo(slot, &info) == PADDLEBOAT_NO_ERROR;
        if (!focused) data = {};
        ApplyAndroidGamepadSample(state, slot, connected ? &data : nullptr, connected ? &info : nullptr);
    }
}

input::InputHapticsCapability AndroidGamepadInput::Capability(std::uint32_t slot) {
    Paddleboat_Controller_Info info{};
    const bool connected = slot < connected_.size() && initialized_ && connected_[slot] &&
        Paddleboat_getControllerInfo(static_cast<int32_t>(slot), &info) == PADDLEBOAT_NO_ERROR;
    const bool supported = connected && focused_ && (info.controllerFlags & PADDLEBOAT_CONTROLLER_FLAG_VIBRATION) != 0;
    return { .supported = supported, .connected = connected,
        .dualMotor = supported && (info.controllerFlags & PADDLEBOAT_CONTROLLER_FLAG_VIBRATION_DUAL_MOTOR) != 0,
        .maxGamepads = input::InputDeviceState::kMaxGamepads,
        .disabledReason = supported ? "" : !connected ? "Android controller is not connected" :
            !focused_ ? "Android controller host is inactive" : "Android controller does not support vibration" };
}

bool AndroidGamepadInput::SendVibration(std::uint32_t slot, float low, float high) noexcept {
    if (!initialized_ || env_ == nullptr || slot >= connected_.size() || !connected_[slot]) return false;
    Paddleboat_Controller_Info info{};
    if (Paddleboat_getControllerInfo(static_cast<int32_t>(slot), &info) != PADDLEBOAT_NO_ERROR ||
        !(info.controllerFlags & PADDLEBOAT_CONTROLLER_FLAG_VIBRATION)) return false;
    const bool dual = (info.controllerFlags & PADDLEBOAT_CONTROLLER_FLAG_VIBRATION_DUAL_MOTOR) != 0;
    const Paddleboat_Vibration_Data effect{ .durationLeft = 250, .durationRight = 250,
        .intensityLeft = dual ? low : std::max(low, high), .intensityRight = dual ? high : 0.0F };
    const auto result = Paddleboat_setControllerVibrationData(static_cast<int32_t>(slot), &effect, env_);
    if (env_->ExceptionCheck()) { env_->ExceptionClear(); return false; }
    vibration_[slot].next = std::chrono::steady_clock::now() + std::chrono::milliseconds{ 150 };
    return result == PADDLEBOAT_NO_ERROR;
}

bool AndroidGamepadInput::SetVibration(std::uint32_t slot, float low, float high) {
    if (!std::isfinite(low) || !std::isfinite(high) || !Capability(slot).supported) return false;
    low = std::clamp(low, 0.0F, 1.0F); high = std::clamp(high, 0.0F, 1.0F);
    auto& effect = vibration_[slot];
    if (effect.low == low && effect.high == high) return true;
    if (!SendVibration(slot, low, high)) { effect = {}; return false; }
    effect.low = low; effect.high = high;
    return true;
}

void AndroidGamepadInput::StopAll() noexcept {
    for (std::uint32_t slot = 0; slot < vibration_.size(); ++slot) {
        if (vibration_[slot].low > 0.0F || vibration_[slot].high > 0.0F)
            static_cast<void>(SendVibration(slot, 0.0F, 0.0F));
        vibration_[slot] = {};
    }
}

} // namespace kb::game
