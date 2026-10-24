#include "AndroidGamepadInput.hpp"

#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using kb::input::InputDeviceState;
using kb::input::InputKey;
struct Observation {
    InputDeviceState state;
    std::array<int, InputDeviceState::kMaxGamepads> devices{-1, -1, -1, -1};
    std::string error;
    int firstSlot = -1;
    int secondSlot = -1;
    unsigned motions = 0U;
    unsigned motionsSeen = 0U;
    unsigned keys = 0U;
    bool ready = false;
    bool stopped = false;
    std::array<unsigned, PADDLEBOAT_MAX_CONTROLLERS> statuses{};
};
std::mutex observationMutex;
Observation observation;
std::atomic<bool> focused{true};
std::atomic<unsigned> starts{0U}, stops{0U};

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool Near(float actual, float expected) { return std::abs(actual - expected) < 0.001F; }

void VerifySampleBoundaries() {
    InputDeviceState state;
    state.SetKeyDown(InputKey::A, true);
    Paddleboat_Controller_Info info{};
    info.leftStickPrecision.stickFlatX = 0.1F;
    Paddleboat_Controller_Data data{};
    data.leftStick.stickX = 0.05F;
    data.leftStick.stickY = -2.0F;
    data.rightStick.stickX = std::numeric_limits<float>::quiet_NaN();
    data.rightStick.stickY = std::numeric_limits<float>::infinity();
    data.triggerL2 = -1.0F;
    data.triggerR2 = 2.0F;
    data.buttonsDown = PADDLEBOAT_BUTTON_A | PADDLEBOAT_BUTTON_DPAD_RIGHT;
    kb::game::ApplyAndroidGamepadSample(state, 1U, &data, &info);
    Require(state.IsGamepadConnected(1U) && !state.IsGamepadConnected(0U), "Sample slot isolation");
    Require(Near(state.GetValue(InputKey::GamepadLeftStickX, 1U), 0.0F) &&
        Near(state.GetValue(InputKey::GamepadLeftStickY, 1U), 1.0F) &&
        Near(state.GetValue(InputKey::GamepadRightStickX, 1U), 0.0F) &&
        Near(state.GetValue(InputKey::GamepadRightStickY, 1U), 0.0F) &&
        Near(state.GetValue(InputKey::GamepadLeftTrigger, 1U), 0.0F) &&
        Near(state.GetValue(InputKey::GamepadRightTrigger, 1U), 1.0F), "Sample finite/range/deadzone handling");
    Require(state.IsKeyDown(InputKey::GamepadFaceBottom, 1U) &&
        state.IsKeyDown(InputKey::GamepadDPadRight, 1U) && state.IsKeyDown(InputKey::A), "Sample buttons and keyboard isolation");
    kb::game::ApplyAndroidGamepadSample(state, InputDeviceState::kMaxGamepads, &data, &info);
    for (std::uint8_t slot = 0; slot < InputDeviceState::kMaxGamepads; ++slot) {
        Require(state.IsGamepadConnected(slot) == (slot == 1U), "Excessive slot must not alias a controller");
    }
    kb::game::ApplyAndroidGamepadSample(state, 1U, nullptr, nullptr);
    Require(!state.IsGamepadConnected(1U) && !state.IsKeyDown(InputKey::GamepadFaceBottom, 1U) &&
        Near(state.GetValue(InputKey::GamepadLeftStickY, 1U), 0.0F) && state.IsKeyDown(InputKey::A), "Disconnected sample must clear only its slot");
}

int Slot(int id) {
    for (std::size_t slot = 0; slot < observation.devices.size(); ++slot) {
        if (observation.devices[slot] == id) return static_cast<int>(slot);
    }
    return -1;
}

bool Axes(int slot, float x, float y, float z, float rz, float left, float right) {
    if (slot < 0) return false;
    const auto index = static_cast<std::uint8_t>(slot);
    return Near(observation.state.GetValue(InputKey::GamepadLeftStickX, index), x) &&
        Near(observation.state.GetValue(InputKey::GamepadLeftStickY, index), y) &&
        Near(observation.state.GetValue(InputKey::GamepadRightStickX, index), z) &&
        Near(observation.state.GetValue(InputKey::GamepadRightStickY, index), rz) &&
        Near(observation.state.GetValue(InputKey::GamepadLeftTrigger, index), left) &&
        Near(observation.state.GetValue(InputKey::GamepadRightTrigger, index), right);
}

bool Button(int slot, InputKey key) {
    return slot >= 0 && observation.state.IsKeyDown(key, static_cast<std::uint8_t>(slot));
}
bool Connected(int slot) {
    return slot >= 0 && observation.state.IsGamepadConnected(static_cast<std::uint8_t>(slot));
}
} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_check(JNIEnv*, jclass, jint phase, jint first, jint second) {
    const std::lock_guard lock{observationMutex};
    if (phase == 7) return observation.stopped && observation.error.empty();
    if (!observation.ready || !observation.error.empty()) return false;
    const int a = Slot(first), b = Slot(second);
    if (phase == 0) {
        if (a < 0 || b < 0 || a == b) return false;
        observation.firstSlot = a;
        observation.secondSlot = b;
        return Connected(a) && Connected(b) && Axes(a, 0, 0, 0, 0, 0, 0) && Axes(b, 0, 0, 0, 0, 0, 0);
    }
    const bool secondHeld = b == observation.secondSlot && Button(b, InputKey::GamepadFaceRight) &&
        Button(b, InputKey::GamepadDPadLeft) && Button(b, InputKey::GamepadDPadUp) &&
        Axes(b, -0.8F, -0.6F, 0.5F, 0.7F, 0.7F, 0.2F);
    if (phase == 1) return Axes(a, 0.75F, 0.5F, -0.6F, -0.8F, 0.4F, 0.9F) &&
        Button(a, InputKey::GamepadFaceBottom) && Button(a, InputKey::GamepadDPadRight) &&
        !Button(b, InputKey::GamepadFaceBottom) && Axes(b, 0, 0, 0, 0, 0, 0);
    if (phase == 2) return Axes(a, 0.75F, 0.5F, -0.6F, -0.8F, 0.4F, 0.9F) &&
        Button(a, InputKey::GamepadFaceBottom) && !Button(a, InputKey::GamepadFaceRight) && secondHeld;
    if (phase == 3) return Axes(a, 0, 0, 0, 0, 0, 0) && !Button(a, InputKey::GamepadFaceBottom) &&
        !Button(a, InputKey::GamepadDPadRight) && secondHeld;
    if (phase == 4) return Connected(a) && Connected(b) && Axes(a, 0, 0, 0, 0, 0, 0) && Axes(b, 0, 0, 0, 0, 0, 0) &&
        !Button(a, InputKey::GamepadFaceBottom) && !Button(b, InputKey::GamepadFaceRight);
    if (phase == 5) return a < 0 && observation.firstSlot >= 0 &&
        !observation.state.IsGamepadConnected(static_cast<std::uint8_t>(observation.firstSlot)) &&
        Axes(observation.firstSlot, 0, 0, 0, 0, 0, 0) && !Button(observation.firstSlot, InputKey::GamepadFaceBottom) &&
        !Button(observation.firstSlot, InputKey::GamepadDPadDown) && secondHeld;
    if (phase == 6) return a == observation.firstSlot && secondHeld && Axes(a, 0, 0, 0, 0, 0, 0) &&
        !Button(a, InputKey::GamepadFaceBottom) && !Button(a, InputKey::GamepadDPadDown) &&
        observation.motions >= 4U && observation.keys >= 4U;
    if (phase == 8) return Axes(a, 0.7F, 0.4F, -0.3F, -0.9F, 0.5F, 0.8F) &&
        Button(a, InputKey::GamepadFaceBottom) && Button(a, InputKey::GamepadDPadDown) && secondHeld;
    return false;
}

extern "C" JNIEXPORT void JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_setFocused(JNIEnv*, jclass, jboolean value) {
    focused.store(value);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_diagnostics(JNIEnv* env, jclass) {
    const std::lock_guard lock{observationMutex};
    std::ostringstream text;
    text << "ready=" << observation.ready << " stopped=" << observation.stopped << " motions=" << observation.motions
         << '/' << observation.motionsSeen
         << " keys=" << observation.keys << " starts=" << starts.load() << " stops=" << stops.load()
         << " error=" << observation.error << " statuses=";
    for (auto status : observation.statuses) text << status << ',';
    for (std::uint8_t slot = 0; slot < InputDeviceState::kMaxGamepads; ++slot) {
        text << " slot=" << static_cast<unsigned>(slot) << " device=" << observation.devices[slot];
        for (auto key : {InputKey::GamepadLeftStickX, InputKey::GamepadLeftStickY,
                 InputKey::GamepadRightStickX, InputKey::GamepadRightStickY,
                 InputKey::GamepadLeftTrigger, InputKey::GamepadRightTrigger}) text << ',' << observation.state.GetValue(key, slot);
    }
    return env->NewStringUTF(text.str().c_str());
}

extern "C" void android_main(android_app* app) {
    try {
        Require(app != nullptr && app->activity != nullptr, "GameActivity context required");
        VerifySampleBoundaries();
        kb::game::AndroidGamepadInput controllers;
        Require(controllers.Initialize(*app->activity), "Controller library initialization failed");
        app->userData = &controllers;
        app->onAppCmd = [](android_app* host, int32_t command) {
            auto& input = *static_cast<kb::game::AndroidGamepadInput*>(host->userData);
            if (command == APP_CMD_START) { ++starts; input.OnStart(); }
            if (command == APP_CMD_STOP) { ++stops; input.OnStop(); }
        };
        android_app_set_key_event_filter(app, nullptr);
        android_app_set_motion_event_filter(app, nullptr);
        unsigned motions = 0, motionsSeen = 0, keys = 0;
        while (app->destroyRequested == 0) {
            android_poll_source* source = nullptr;
            static_cast<void>(ALooper_pollOnce(10, nullptr, nullptr, reinterpret_cast<void**>(&source)));
            if (source != nullptr) source->process(app, source);
            if (app->destroyRequested != 0) break;
            controllers.Update();
            auto* events = android_app_swap_input_buffers(app);
            if (events != nullptr) {
                for (std::uint64_t index = 0; index < events->keyEventsCount; ++index) {
                    if (controllers.Process(events->keyEvents[index])) ++keys;
                }
                for (std::uint64_t index = 0; index < events->motionEventsCount; ++index) {
                    ++motionsSeen;
                    if (controllers.Process(events->motionEvents[index])) ++motions;
                }
                android_app_clear_motion_events(events);
                android_app_clear_key_events(events);
            }
            const std::lock_guard lock{observationMutex};
            controllers.Publish(observation.state, focused.load());
            for (int slot = 0; slot < PADDLEBOAT_MAX_CONTROLLERS; ++slot) {
                observation.statuses[static_cast<std::size_t>(slot)] = Paddleboat_getControllerStatus(slot);
            }
            observation.devices.fill(-1);
            for (std::uint8_t slot = 0; slot < InputDeviceState::kMaxGamepads; ++slot) {
                Paddleboat_Controller_Info info{};
                if (Paddleboat_getControllerStatus(slot) == PADDLEBOAT_CONTROLLER_ACTIVE &&
                    Paddleboat_getControllerInfo(slot, &info) == PADDLEBOAT_NO_ERROR) observation.devices[slot] = info.deviceId;
            }
            observation.motions = motions;
            observation.motionsSeen = motionsSeen;
            observation.keys = keys;
            observation.ready = true;
        }
        controllers.Shutdown();
        const std::lock_guard lock{observationMutex};
        Require(!Paddleboat_isInitialized(), "Controller library shutdown failed");
        observation.stopped = true;
    } catch (const std::exception& error) {
        const std::lock_guard lock{observationMutex};
        observation.error = error.what();
        observation.stopped = true;
    }
}
