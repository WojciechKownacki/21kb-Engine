#pragma once

#include "engine/input/InputDeviceState.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>

namespace kb::game {

// evdev snapshots preserve device slots across enumeration and focus changes.
class LinuxGamepadInput final {
public:
    explicit LinuxGamepadInput(std::filesystem::path directory = "/dev/input");
    ~LinuxGamepadInput();
    LinuxGamepadInput(const LinuxGamepadInput&) = delete;
    LinuxGamepadInput& operator=(const LinuxGamepadInput&) = delete;
    void Poll(input::InputDeviceState& state);

private:
    struct Device;
    std::filesystem::path directory_;
    std::array<std::unique_ptr<Device>, input::InputDeviceState::kMaxGamepads> devices_;
    std::chrono::steady_clock::time_point nextScan_{};
};

} // namespace kb::game
