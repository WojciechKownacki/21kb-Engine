#include "AndroidGamepadInput.hpp"
#include "AndroidTouchInput.hpp"
#include "AndroidWindowInsets.hpp"
#include "AndroidTextInput.hpp"

#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

void VerifyAndroidTouchInput();

namespace {
using kb::input::InputDeviceState;
using kb::input::InputKey;
struct Observation {
    kb::scene::UIEdges safeArea{};
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
    std::u32string text;
    unsigned submits = 0U;
    std::string imeDiagnostic;
    std::array<unsigned, PADDLEBOAT_MAX_CONTROLLERS> statuses{};
};
std::mutex observationMutex;
Observation observation;
std::atomic<bool> focused{true};
std::atomic<unsigned> starts{0U}, stops{0U};
std::atomic<unsigned> surfaces{0U}, lostSurfaces{0U}, resizedSurfaces{0U};
std::atomic<bool> holdMotion{false}, finishTouchFrame{false};
std::atomic<unsigned> touchEvents{0U};
std::atomic<bool> touchBoundaries{false};
std::atomic<int> imeRequest{-1};
std::atomic<unsigned> imeDismissals{0U};
struct PlatformInput {
    kb::game::AndroidGamepadInput controllers;
    kb::game::AndroidTouchInput touches;
    kb::game::DigitalInputFrame digital;
    InputDeviceState textState;
    bool imeRequested = false;
    bool imeWasVisible = false;
    bool imeInitialized = false;
};

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

extern "C" JNIEXPORT void JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_requestIme(JNIEnv*, jclass, jboolean shown) {
    imeRequest.store(shown ? 1 : 0);
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_imeState(JNIEnv* env, jclass) {
    const std::lock_guard lock{observationMutex};
    std::vector<jint> values{static_cast<jint>(observation.submits)};
    for (const auto value : observation.text) values.push_back(static_cast<jint>(value));
    auto result = env->NewIntArray(static_cast<jsize>(values.size()));
    if (result != nullptr) env->SetIntArrayRegion(result, 0, static_cast<jsize>(values.size()), values.data());
    return result;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_imeDismissalCount(JNIEnv*, jclass) {
    return static_cast<jint>(imeDismissals.load());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_check(JNIEnv*, jclass, jint phase, jint first, jint second) {
    const std::lock_guard lock{observationMutex};
    if (phase == 7) return observation.stopped && observation.error.empty();
    if (!observation.ready || !observation.error.empty()) return false;
    if (phase == 9) return true; // Touch-only hosts do not need OS virtual controllers.
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

extern "C" JNIEXPORT void JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_holdMotion(JNIEnv*, jclass, jboolean value) {
    holdMotion.store(value);
}

extern "C" JNIEXPORT void JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_consumeTouchFrame(JNIEnv*, jclass) {
    finishTouchFrame.store(true);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_touchMatches(
    JNIEnv* env, jclass, jintArray ids, jintArray phases, jfloatArray coordinates, jint width, jint height) {
    if (ids == nullptr || phases == nullptr || coordinates == nullptr) return false;
    const auto count = env->GetArrayLength(ids);
    if (count > static_cast<jsize>(InputDeviceState::kMaxTouchPoints) || env->GetArrayLength(phases) != count ||
        env->GetArrayLength(coordinates) != count * 2) return false;
    std::array<jint, InputDeviceState::kMaxTouchPoints> expectedIds{}, expectedPhases{};
    std::array<jfloat, InputDeviceState::kMaxTouchPoints * 2U> expectedCoordinates{};
    env->GetIntArrayRegion(ids, 0, count, expectedIds.data());
    env->GetIntArrayRegion(phases, 0, count, expectedPhases.data());
    env->GetFloatArrayRegion(coordinates, 0, count * 2, expectedCoordinates.data());
    const std::lock_guard lock{observationMutex};
    const auto points = observation.state.TouchPoints();
    if (!observation.ready || !observation.error.empty() || points.size() != static_cast<std::size_t>(count)) return false;
    if (width > 0 && (observation.state.PointerViewportWidth() != static_cast<unsigned>(width) ||
        observation.state.PointerViewportHeight() != static_cast<unsigned>(height))) return false;
    bool active = false;
    std::size_t primary = 0U;
    for (std::size_t index = 0U; index < points.size(); ++index) {
        if (points[index].id != static_cast<unsigned>(expectedIds[index]) ||
            static_cast<int>(points[index].phase) != expectedPhases[index] ||
            !Near(points[index].x, expectedCoordinates[index * 2U]) ||
            !Near(points[index].y, expectedCoordinates[index * 2U + 1U])) return false;
        if (!active && kb::input::IsTouchActive(points[index].phase)) { primary = index; active = true; }
    }
    return observation.state.IsKeyDown(InputKey::TouchDown) == active &&
        (points.empty() || (Near(observation.state.PointerX(), points[primary].x) &&
            Near(observation.state.PointerY(), points[primary].y)));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_surfaceState(JNIEnv*, jclass, jint phase) {
    const std::lock_guard lock{observationMutex};
    const bool available = observation.state.PointerViewportWidth() > 0U && observation.state.PointerViewportHeight() > 0U;
    if (phase == 0) return surfaces.load() > 0U && available;
    if (phase == 1) return resizedSurfaces.load() > 0U;
    if (phase == 2) return lostSurfaces.load() > 0U && !available;
    if (phase == 3) return surfaces.load() >= 2U && available;
    return false;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_diagnostics(JNIEnv* env, jclass) {
    const std::lock_guard lock{observationMutex};
    std::ostringstream text;
    text << "ready=" << observation.ready << " stopped=" << observation.stopped << " motions=" << observation.motions
         << '/' << observation.motionsSeen
         << " keys=" << observation.keys << " starts=" << starts.load() << " stops=" << stops.load()
         << " surfaces=" << surfaces.load() << " lost-surfaces=" << lostSurfaces.load()
         << " resized-surfaces=" << resizedSurfaces.load() << " viewport=" << observation.state.PointerViewportWidth()
         << 'x' << observation.state.PointerViewportHeight() << " touches=" << observation.state.TouchPoints().size()
         << " touch-events=" << touchEvents.load() << " touch-boundaries=" << touchBoundaries.load()
         << " ime=" << observation.imeDiagnostic
         << " safe-area=" << observation.safeArea.left << ',' << observation.safeArea.top << ','
         << observation.safeArea.right << ',' << observation.safeArea.bottom
         << " error=" << observation.error << " statuses=";
    for (const auto& point : observation.state.TouchPoints()) {
        text << " touch=" << point.id << ',' << static_cast<unsigned>(point.phase) << ',' << point.x << ',' << point.y;
    }
    for (auto status : observation.statuses) text << status << ',';
    for (std::uint8_t slot = 0; slot < InputDeviceState::kMaxGamepads; ++slot) {
        text << " slot=" << static_cast<unsigned>(slot) << " device=" << observation.devices[slot];
        for (auto key : {InputKey::GamepadLeftStickX, InputKey::GamepadLeftStickY,
                 InputKey::GamepadRightStickX, InputKey::GamepadRightStickY,
                 InputKey::GamepadLeftTrigger, InputKey::GamepadRightTrigger}) text << ',' << observation.state.GetValue(key, slot);
    }
    return env->NewStringUTF(text.str().c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kbengine_controllertests_ControllerInstrumentation_safeAreaMatches(
    JNIEnv*, jclass, jint left, jint top, jint right, jint bottom) {
    const std::lock_guard lock{observationMutex};
    const auto& insets = observation.safeArea;
    return Near(insets.left, static_cast<float>(left)) && Near(insets.top, static_cast<float>(top)) &&
        Near(insets.right, static_cast<float>(right)) && Near(insets.bottom, static_cast<float>(bottom));
}

extern "C" void android_main(android_app* app) {
    try {
        Require(app != nullptr && app->activity != nullptr, "GameActivity context required");
        VerifySampleBoundaries();
        VerifyAndroidTouchInput();
        touchBoundaries.store(true);
        PlatformInput platform;
        auto& controllers = platform.controllers;
        Require(controllers.Initialize(*app->activity), "Controller library initialization failed");
        app->userData = &platform;
        app->onAppCmd = [](android_app* host, int32_t command) {
            auto& inputs = *static_cast<PlatformInput*>(host->userData);
            inputs.touches.HandleAppCommand(command);
            if (command == APP_CMD_SOFTWARE_KB_VIS_CHANGED &&
                kb::game::QueueAndroidImeDismissal(host->softwareKeyboardVisible, inputs.imeRequested,
                    inputs.imeWasVisible, inputs.textState, inputs.digital)) {
                ++imeDismissals;
                inputs.imeRequested = false;
            }
            if (command == APP_CMD_EDITOR_ACTION) {
                static_cast<void>(kb::game::QueueAndroidEditorAction(host->editorAction, inputs.textState, inputs.digital));
            }
            auto& input = inputs.controllers;
            if (command == APP_CMD_START) { ++starts; input.OnStart(); }
            if (command == APP_CMD_STOP) { ++stops; input.OnStop(); }
            if (command == APP_CMD_INIT_WINDOW) ++surfaces;
            if (command == APP_CMD_TERM_WINDOW) ++lostSurfaces;
            if (command == APP_CMD_WINDOW_RESIZED) ++resizedSurfaces;
        };
        android_app_set_key_event_filter(app, nullptr);
        android_app_set_motion_event_filter(app, nullptr);
        unsigned motions = 0, motionsSeen = 0, keys = 0;
        bool submittedBefore = false;
        while (app->destroyRequested == 0) {
            android_poll_source* source = nullptr;
            static_cast<void>(ALooper_pollOnce(10, nullptr, nullptr, reinterpret_cast<void**>(&source)));
            if (source != nullptr) source->process(app, source);
            if (app->destroyRequested != 0) break;
            const int requested = imeRequest.exchange(-1);
            if (requested >= 0) {
                platform.imeRequested = requested != 0;
                if (requested != 0 && !platform.imeInitialized) {
                    constexpr std::u32string_view initial = U"Start \U0001F600 ";
                    kb::game::SetAndroidTextInputState(app->activity, initial, initial.size());
                    platform.imeInitialized = true;
                }
                if (requested != 0) GameActivity_showSoftInput(app->activity, GAMEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT);
                else GameActivity_hideSoftInput(app->activity, GAMEACTIVITY_HIDE_SOFT_INPUT_NOT_ALWAYS);
            }
            platform.textState.ClearTextInput();
            if (app->textInputState != 0) {
                kb::game::AndroidCommittedTextContext context;
                GameActivity_getTextInputState(app->activity, [](void* raw, const GameTextInputState* text) {
                    kb::game::ReadAndroidCommittedText(raw, text);
                    if (text != nullptr && text->text_length > 0) {
                        std::ostringstream detail;
                        detail << "bytes=" << text->text_length << ",composition=" << text->composingRegion.start << ':' << text->composingRegion.end;
                        for (int i = 0; i < text->text_length && i < 40; ++i)
                            detail << ',' << static_cast<unsigned>(static_cast<unsigned char>(text->text_UTF8[i]));
                        const std::lock_guard lock{observationMutex};
                        observation.imeDiagnostic = detail.str();
                    }
                }, &context);
                app->textInputState = 0;
                if (context.valid) {
                    const std::lock_guard lock{observationMutex};
                    observation.text.assign(context.text.data(), context.count);
                }
            }
            platform.digital.Publish(platform.textState);
            controllers.Update();
            const auto width = app->window == nullptr ? 0U : static_cast<unsigned>(ANativeWindow_getWidth(app->window));
            const auto height = app->window == nullptr ? 0U : static_cast<unsigned>(ANativeWindow_getHeight(app->window));
            platform.touches.SetViewport(width, height);
            if (!focused.load()) platform.touches.Cancel();
            if (finishTouchFrame.exchange(false)) platform.touches.FinishFrame();
            auto* events = holdMotion.load() ? nullptr : android_app_swap_input_buffers(app);
            if (events != nullptr) {
                for (std::uint64_t index = 0; index < events->keyEventsCount; ++index) {
                    if (controllers.Process(events->keyEvents[index])) ++keys;
                }
                for (std::uint64_t index = 0; index < events->motionEventsCount; ++index) {
                    const auto& event = events->motionEvents[index];
                    if ((event.source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK) ++motionsSeen;
                    if (controllers.Process(event)) ++motions;
                    else if (focused.load()) {
                        if (platform.touches.Process(event)) ++touchEvents;
                    }
                }
                android_app_clear_motion_events(events);
                android_app_clear_key_events(events);
            }
            const std::lock_guard lock{observationMutex};
            const bool submitted = platform.textState.IsKeyDown(InputKey::Enter);
            if (submitted && !submittedBefore) ++observation.submits;
            submittedBefore = submitted;
            observation.safeArea = kb::game::ReadAndroidWindowInsets(*app->activity);
            controllers.Publish(observation.state, focused.load());
            platform.touches.Publish(observation.state);
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
        {
            const std::lock_guard lock{observationMutex};
            observation.error = error.what();
            observation.stopped = true;
        }
        // Complete native lifecycle handshakes so instrumentation can report the
        // failed assertion instead of waiting forever for activity startup.
        if (app == nullptr) return;
        app->onAppCmd = nullptr;
        app->userData = nullptr;
        while (app->destroyRequested == 0) {
            android_poll_source* source = nullptr;
            static_cast<void>(ALooper_pollOnce(10, nullptr, nullptr, reinterpret_cast<void**>(&source)));
            if (source != nullptr) source->process(app, source);
        }
    }
}
