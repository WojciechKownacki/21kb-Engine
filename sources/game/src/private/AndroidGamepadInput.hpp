#pragma once

#include "engine/input/InputDeviceState.hpp"

#include <jni.h>
#include <paddleboat/paddleboat.h>

#include <cstdint>
#include <array>

struct GameActivity;
struct GameActivityKeyEvent;
struct GameActivityMotionEvent;

namespace kb::game {

// A null sample disconnects and clears only the selected controller slot.
void ApplyAndroidGamepadSample(input::InputDeviceState& state, std::uint8_t slot,
    const Paddleboat_Controller_Data* sample, const Paddleboat_Controller_Info* info) noexcept;

class AndroidGamepadInput {
public:
    AndroidGamepadInput() = default;
    ~AndroidGamepadInput();
    AndroidGamepadInput(const AndroidGamepadInput&) = delete;
    AndroidGamepadInput& operator=(const AndroidGamepadInput&) = delete;

    [[nodiscard]] bool Initialize(GameActivity& activity) noexcept;
    void Shutdown() noexcept;
    void OnStart() noexcept;
    void OnStop() noexcept;
    void Update() noexcept;
    [[nodiscard]] bool Process(const GameActivityKeyEvent& event) noexcept;
    [[nodiscard]] bool Process(const GameActivityMotionEvent& event) noexcept;
    void Publish(input::InputDeviceState& state, bool focused) const noexcept;

private:
    JavaVM* vm_ = nullptr;
    JNIEnv* env_ = nullptr;
    std::uint64_t enabledAxes_ = 0U;
    std::array<bool, input::InputDeviceState::kMaxGamepads> connected_{};
    bool attached_ = false;
    bool initialized_ = false;
};

} // namespace kb::game
