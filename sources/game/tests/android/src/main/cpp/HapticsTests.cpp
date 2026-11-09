#include "AndroidGamepadInput.hpp"

#include <limits>
#include <stdexcept>
#include <thread>

namespace {
Paddleboat_ControllerStatusCallback callback{};
void* callbackContext{};
bool testing{};
bool failRequest{};
bool receivedEnvironment{};
unsigned calls{};
uint32_t flags{};
Paddleboat_Vibration_Data last{};

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

extern "C" {
void __real_Paddleboat_setControllerStatusCallback(Paddleboat_ControllerStatusCallback, void*);
Paddleboat_ErrorCode __real_Paddleboat_getControllerInfo(int32_t, Paddleboat_Controller_Info*);
Paddleboat_ErrorCode __real_Paddleboat_getControllerData(int32_t, Paddleboat_Controller_Data*);
Paddleboat_ErrorCode __real_Paddleboat_setControllerVibrationData(int32_t, const Paddleboat_Vibration_Data*, JNIEnv*);

void __wrap_Paddleboat_setControllerStatusCallback(Paddleboat_ControllerStatusCallback value, void* context) {
    callback = value;
    callbackContext = context;
    __real_Paddleboat_setControllerStatusCallback(value, context);
}
Paddleboat_ErrorCode __wrap_Paddleboat_getControllerInfo(int32_t slot, Paddleboat_Controller_Info* info) {
    if (!testing || slot != 1) return __real_Paddleboat_getControllerInfo(slot, info);
    *info = {};
    info->controllerFlags = flags;
    return PADDLEBOAT_NO_ERROR;
}
Paddleboat_ErrorCode __wrap_Paddleboat_getControllerData(int32_t slot, Paddleboat_Controller_Data* data) {
    if (!testing || slot != 1) return __real_Paddleboat_getControllerData(slot, data);
    *data = {};
    return PADDLEBOAT_NO_ERROR;
}
Paddleboat_ErrorCode __wrap_Paddleboat_setControllerVibrationData(
    int32_t slot, const Paddleboat_Vibration_Data* data, JNIEnv* env) {
    if (!testing || slot != 1) return __real_Paddleboat_setControllerVibrationData(slot, data, env);
    ++calls;
    last = *data;
    receivedEnvironment = env != nullptr;
    return failRequest ? static_cast<Paddleboat_ErrorCode>(-1) : PADDLEBOAT_NO_ERROR;
}
}

// Exercise the production provider on its real Android/JNI host thread. Only
// controller samples and actuator calls are intercepted; no physical rumble is claimed.
void VerifyAndroidHaptics(kb::game::AndroidGamepadInput& provider) {
    Require(callback != nullptr, "Haptics connection callback missing");
    testing = true;
    failRequest = false;
    calls = 0;
    flags = PADDLEBOAT_CONTROLLER_FLAG_VIBRATION | PADDLEBOAT_CONTROLLER_FLAG_VIBRATION_DUAL_MOTOR;
    callback(1, PADDLEBOAT_CONTROLLER_ACTIVE, callbackContext);
    kb::input::InputDeviceState state;
    provider.Publish(state, true);
    const auto capability = provider.Capability(1);
    Require(capability.connected && capability.supported && capability.dualMotor, "Dual motor capability missing");
    Require(!provider.SetVibration(4, 1, 1) &&
        !provider.SetVibration(1, std::numeric_limits<float>::quiet_NaN(), 1), "Invalid haptics request accepted");
    Require(provider.SetVibration(1, 2, -1), "Haptics start failed");
    Require(calls == 1 && receivedEnvironment && last.intensityLeft == 1 && last.intensityRight == 0 &&
        last.durationLeft == 250 && last.durationRight == 250, "Haptics platform request mismatch");
    Require(provider.SetVibration(1, 1, 0) && calls == 1, "Unchanged request was resent");
    std::this_thread::sleep_for(std::chrono::milliseconds{170});
    provider.Update();
    Require(calls == 2 && last.intensityLeft == 1, "Active effect was not refreshed");
    provider.StopAll();
    Require(calls == 3 && last.intensityLeft == 0 && last.intensityRight == 0, "Stop did not clear motors");
    Require(provider.SetVibration(1, .2F, .7F), "Second start failed");
    provider.Publish(state, false);
    Require(last.intensityLeft == 0 && last.intensityRight == 0 && !provider.Capability(1).supported &&
        !provider.SetVibration(1, 1, 1), "Inactive host retained haptics");
    provider.Publish(state, true);
    const unsigned stoppedCalls = calls;
    std::this_thread::sleep_for(std::chrono::milliseconds{170});
    provider.Update();
    Require(calls == stoppedCalls, "Focus restoration resumed the old effect");
    flags = PADDLEBOAT_CONTROLLER_FLAG_VIBRATION;
    Require(!provider.Capability(1).dualMotor && provider.SetVibration(1, .2F, .7F) &&
        last.intensityLeft == .7F && last.intensityRight == 0, "Single motor mapping failed");
    Require(provider.SetVibration(1, 0, 0) && last.intensityLeft == 0 && last.intensityRight == 0,
        "Zero intensity did not stop the effect");
    failRequest = true;
    Require(!provider.SetVibration(1, 1, 1), "Platform failure reported success");
    failRequest = false;
    flags = 0;
    Require(!provider.Capability(1).supported && !provider.SetVibration(1, 1, 1), "Unsupported controller accepted haptics");
    flags = PADDLEBOAT_CONTROLLER_FLAG_VIBRATION;
    Require(provider.SetVibration(1, 1, 1), "Effect before disconnect failed");
    callback(1, PADDLEBOAT_CONTROLLER_JUST_DISCONNECTED, callbackContext);
    Require(!provider.Capability(1).connected && !provider.SetVibration(1, 1, 1), "Disconnected controller accepted haptics");
    const unsigned disconnectedCalls = calls;
    std::this_thread::sleep_for(std::chrono::milliseconds{170});
    provider.Update();
    Require(calls == disconnectedCalls, "Disconnected effect was refreshed");
    testing = false;
}
