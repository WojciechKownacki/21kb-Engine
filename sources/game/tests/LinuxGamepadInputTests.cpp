#include "LinuxGamepadInput.hpp"

#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using kb::input::InputKey;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Controller {
    int fd = -1;
    explicit Controller(int identity, int layout = 0) {
        fd = open("/dev/uinput", O_WRONLY | O_CLOEXEC);
        if (fd < 0) throw std::runtime_error("uinput unavailable");
        try {
            Require(ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0, "key capability");
            Require(ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0, "axis capability");
            for (int key : {BTN_SOUTH, BTN_EAST, BTN_TL2, BTN_TR2, BTN_DPAD_UP, BTN_DPAD_LEFT})
                Require(ioctl(fd, UI_SET_KEYBIT, key) == 0, "button capability");
            uinput_user_dev device{};
            std::snprintf(device.name, sizeof(device.name), "Engine Evdev Test %d", identity);
            device.id.bustype = BUS_USB;
            device.id.vendor = 1;
            device.id.product = static_cast<unsigned short>(identity);
            std::vector<int> axes{ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};
            if (layout == 1) axes = {ABS_X, ABS_Y, ABS_Z, ABS_RZ, ABS_BRAKE, ABS_GAS};
            if (layout == 2) axes = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_HAT1X, ABS_HAT1Y, ABS_HAT2X, ABS_HAT2Y};
            if (layout == 3) axes = {ABS_X, ABS_Y};
            for (int axis : axes) {
                Require(ioctl(fd, UI_SET_ABSBIT, axis) == 0, "axis registration");
                const bool hat = axis == ABS_HAT0X || axis == ABS_HAT0Y;
                const bool trigger = layout == 1 ? axis == ABS_BRAKE || axis == ABS_GAS :
                    axis == ABS_Z || axis == ABS_RZ || axis == ABS_HAT1X || axis == ABS_HAT1Y ||
                    axis == ABS_HAT2X || axis == ABS_HAT2Y;
                device.absmin[axis] = hat ? -1 : trigger ? 0 : -32768;
                device.absmax[axis] = hat ? 1 : trigger ? 255 : 32767;
                device.absflat[axis] = hat || trigger ? 0 : 4096;
            }
            Require(write(fd, &device, sizeof(device)) == static_cast<ssize_t>(sizeof(device)), "device setup");
            Require(ioctl(fd, UI_DEV_CREATE) == 0, "device creation");
        } catch (...) { close(fd); fd = -1; throw; }
    }
    ~Controller() { Disconnect(); }
    void Disconnect() {
        if (fd >= 0) { ioctl(fd, UI_DEV_DESTROY); close(fd); fd = -1; }
    }
    void Send(unsigned short type, unsigned short code, int value) {
        input_event event{};
        event.type = type; event.code = code; event.value = value;
        Require(write(fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event)), "event write");
        if (type != EV_SYN) Send(EV_SYN, SYN_REPORT, 0);
    }
};
template<class Predicate>
void Wait(kb::game::LinuxGamepadInput& collector, kb::input::InputDeviceState& state, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{4};
    do {
        collector.Poll(state);
        if (predicate()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("controller state deadline expired");
}
}

int main() {
    try {
        kb::game::LinuxGamepadInput collector;
        kb::input::InputDeviceState state;
        state.SetHasFocus(true);
        collector.Poll(state);
        for (std::uint8_t slot = 0; slot < 4; ++slot)
            Require(!state.IsGamepadConnected(slot), "test requires no existing gamepads");
        Controller first{101};
        Wait(collector, state, [&] { return state.IsGamepadConnected(0); });
        Controller second{102};
        Wait(collector, state, [&] { return state.IsGamepadConnected(1); });
        first.Send(EV_ABS, ABS_X, 16384);
        first.Send(EV_ABS, ABS_Y, -24576);
        first.Send(EV_ABS, ABS_RX, -8192);
        first.Send(EV_ABS, ABS_RY, 12288);
        first.Send(EV_ABS, ABS_Z, 153);
        first.Send(EV_ABS, ABS_RZ, 204);
        first.Send(EV_ABS, ABS_HAT0X, 1);
        first.Send(EV_KEY, BTN_SOUTH, 1);
        second.Send(EV_ABS, ABS_X, -16384);
        second.Send(EV_KEY, BTN_EAST, 1);
        auto near = [&](InputKey key, float value, std::uint8_t slot = 0) {
            return std::abs(state.GetValue(key, slot) - value) < 0.002F;
        };
        Wait(collector, state, [&] { return near(InputKey::GamepadLeftStickX, .5F) && near(InputKey::GamepadLeftStickX, -.5F, 1); });
        Require(near(InputKey::GamepadLeftStickY, .75F) && near(InputKey::GamepadRightStickX, -.25F) &&
            near(InputKey::GamepadRightStickY, -.375F) && near(InputKey::GamepadLeftTrigger, .6F) &&
            near(InputKey::GamepadRightTrigger, .8F), "six independent axes");
        Require(state.IsKeyDown(InputKey::GamepadFaceBottom) && state.IsKeyDown(InputKey::GamepadDPadRight) &&
            state.IsKeyDown(InputKey::GamepadFaceRight, 1), "independent buttons and hat");
        first.Send(EV_ABS, ABS_X, 1000);
        Wait(collector, state, [&] { return near(InputKey::GamepadLeftStickX, 0); });
        state.SetHasFocus(false); collector.Poll(state);
        Require(near(InputKey::GamepadLeftStickX, 0, 1) && !state.IsKeyDown(InputKey::GamepadFaceBottom), "focus release");
        state.SetHasFocus(true); collector.Poll(state);
        Require(near(InputKey::GamepadLeftStickX, -.5F, 1), "focus snapshot recovery");
        first.Disconnect();
        Wait(collector, state, [&] { return !state.IsGamepadConnected(0); });
        Require(!state.IsKeyDown(InputKey::GamepadFaceBottom) && near(InputKey::GamepadLeftStickX, -.5F, 1), "disconnect isolation");
        Controller reconnected{103};
        Wait(collector, state, [&] { return state.IsGamepadConnected(0); });
        Require(!state.IsKeyDown(InputKey::GamepadFaceBottom) && near(InputKey::GamepadLeftStickX, -.5F, 1), "reconnect isolation");
        reconnected.Send(EV_KEY, BTN_SOUTH, 1);
        Wait(collector, state, [&] { return state.IsKeyDown(InputKey::GamepadFaceBottom); });
        reconnected.Disconnect(); second.Disconnect();
        Wait(collector, state, [&] { return !state.IsGamepadConnected(0) && !state.IsGamepadConnected(1); });
        for (int layout = 1; layout <= 3; ++layout) {
            Controller alternate{110 + layout, layout};
            Wait(collector, state, [&] { return state.IsGamepadConnected(0); });
            alternate.Send(EV_KEY, BTN_DPAD_UP, 1);
            alternate.Send(EV_KEY, BTN_DPAD_LEFT, 1);
            if (layout == 1) {
                alternate.Send(EV_ABS, ABS_Z, -16384);
                alternate.Send(EV_ABS, ABS_RZ, 16384);
                alternate.Send(EV_ABS, ABS_BRAKE, 153);
                alternate.Send(EV_ABS, ABS_GAS, 204);
            } else if (layout == 2) {
                alternate.Send(EV_ABS, ABS_HAT1X, 128);
                alternate.Send(EV_ABS, ABS_HAT1Y, 128);
                alternate.Send(EV_ABS, ABS_HAT2Y, 153);
                alternate.Send(EV_ABS, ABS_HAT2X, 204);
            } else {
                alternate.Send(EV_KEY, BTN_TL2, 1);
                alternate.Send(EV_KEY, BTN_TR2, 1);
            }
            Wait(collector, state, [&] { return near(InputKey::GamepadLeftTrigger, layout == 3 ? 1.0F : .6F); });
            Require(near(InputKey::GamepadRightTrigger, layout == 3 ? 1.0F : .8F), "alternate triggers");
            Require(state.IsKeyDown(InputKey::GamepadDPadUp) && state.IsKeyDown(InputKey::GamepadDPadLeft), "digital directional pad");
            if (layout == 1) Require(near(InputKey::GamepadRightStickX, -.5F) && near(InputKey::GamepadRightStickY, -.5F), "legacy axes");
            if (layout == 2) Require(state.IsKeyDown(InputKey::GamepadLeftShoulder) && state.IsKeyDown(InputKey::GamepadRightShoulder), "analog shoulders");
            alternate.Disconnect();
            Wait(collector, state, [&] { return !state.IsGamepadConnected(0); });
            Require(near(InputKey::GamepadLeftTrigger, 0) && near(InputKey::GamepadRightTrigger, 0) &&
                !state.IsKeyDown(InputKey::GamepadDPadUp) && !state.IsKeyDown(InputKey::GamepadLeftShoulder), "alternate release");
        }
        std::cout << "PASS: evdev axes, buttons, dead zones, focus, hotplug, stable slots and four mappings\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
