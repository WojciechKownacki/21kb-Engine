#include "engine/platform/win32/Win32GamepadInput.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/platform/win32/Win32InputKeyMap.hpp"

#include "Win32GamepadInputInternal.hpp"
#include <Xinput.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <cwchar>
#include <limits>
#include <mutex>
#include <vector>

namespace kb::input {
namespace {
using Microsoft::WRL::ComPtr;
constexpr auto noUser = std::numeric_limits<std::uint32_t>::max();
constexpr std::array analogKeys{InputKey::GamepadLeftStickX, InputKey::GamepadLeftStickY,
    InputKey::GamepadRightStickX, InputKey::GamepadRightStickY,
    InputKey::GamepadLeftTrigger, InputKey::GamepadRightTrigger};

struct Slot {
    std::uint32_t user = noUser;
    GUID identity{};
    ComPtr<IDirectInputDevice8W> device;
    Win32GamepadMapping mapping;
    std::array<bool, 8> axes{};
    bool seen = false;
    bool Empty() const { return user == noUser && !device; }
};
struct ProductMapping { GUID product; Win32GamepadMapping mapping; };

struct Registry {
    std::mutex mutex;
    ComPtr<IDirectInput8W> input;
    std::array<Slot, InputDeviceState::kMaxGamepads> slots;
    std::vector<ProductMapping> mappings;
    ULONGLONG nextScan = 0;
    HWND window = nullptr;

    static BOOL CALLBACK Axis(const DIDEVICEOBJECTINSTANCEW* object, void* context) {
        auto& slot = *static_cast<Slot*>(context);
        const std::array<DWORD, 8> offsets{DIJOFS_X, DIJOFS_Y, DIJOFS_Z,
            DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ, DIJOFS_SLIDER(0), DIJOFS_SLIDER(1)};
        for (std::size_t index = 0; index < offsets.size(); ++index) {
            if (object->dwOfs != offsets[index]) continue;
            DIPROPRANGE range{};
            range.diph = {sizeof(range), sizeof(DIPROPHEADER), object->dwType, DIPH_BYID};
            range.lMin = 0; range.lMax = 65535;
            slot.axes[index] = SUCCEEDED(slot.device->SetProperty(DIPROP_RANGE, &range.diph));
        }
        return DIENUM_CONTINUE;
    }

    static BOOL CALLBACK Device(const DIDEVICEINSTANCEW* description, void* context) {
        auto& registry = *static_cast<Registry*>(context);
        for (auto& slot : registry.slots) {
            if (slot.device && slot.identity == description->guidInstance) {
                slot.seen = true;
                return DIENUM_CONTINUE;
            }
        }
        auto free = std::find_if(registry.slots.begin(), registry.slots.end(), [](const Slot& slot) { return slot.Empty(); });
        if (free == registry.slots.end()) return DIENUM_CONTINUE;
        Slot candidate;
        if (FAILED(registry.input->CreateDevice(description->guidInstance, &candidate.device, nullptr))) return DIENUM_CONTINUE;
        DIPROPGUIDANDPATH path{};
        path.diph = {sizeof(path), sizeof(DIPROPHEADER), 0, DIPH_DEVICE};
        if (SUCCEEDED(candidate.device->GetProperty(DIPROP_GUIDANDPATH, &path.diph))) {
            for (auto& character : path.wszPath) character = static_cast<WCHAR>(std::towupper(character));
            // XInput controllers also enumerate through DirectInput.
            if (std::wcsstr(path.wszPath, L"IG_") != nullptr) return DIENUM_CONTINUE;
        }
        if (FAILED(candidate.device->SetDataFormat(&c_dfDIJoystick2)) ||
            FAILED(candidate.device->SetCooperativeLevel(registry.window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND)))
            return DIENUM_CONTINUE;
        candidate.identity = description->guidInstance;
        candidate.seen = true;
        if (FAILED(candidate.device->EnumObjects(&Axis, &candidate, DIDFT_AXIS))) return DIENUM_CONTINUE;
        if (!candidate.axes[3] && !candidate.axes[4]) candidate.mapping.axes = {0, 1, 2, 5, 6, 7};
        for (const auto& entry : registry.mappings)
            if (entry.product == description->guidProduct) candidate.mapping = entry.mapping;
        candidate.device->Acquire();
        *free = std::move(candidate);
        return DIENUM_CONTINUE;
    }

    void Scan(HWND owner) {
        const auto now = GetTickCount64();
        if (owner != window) {
            for (auto& slot : slots) if (slot.device) {
                slot.device->Unacquire();
                slot.device->SetCooperativeLevel(owner, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
            }
            window = owner;
            nextScan = 0;
        }
        if (now < nextScan || !IsWindow(owner)) return;
        nextScan = now + 1000;
        if (!input && FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                IID_IDirectInput8W, reinterpret_cast<void**>(input.GetAddressOf()), nullptr))) return;
        for (auto& slot : slots) slot.seen = false;
        if (SUCCEEDED(input->EnumDevices(DI8DEVCLASS_GAMECTRL, &Device, this, DIEDFL_ATTACHEDONLY)))
            for (auto& slot : slots) if (slot.device && !slot.seen) slot = {};
    }
};
Registry& Devices() { static Registry registry; return registry; }

float Stick(LONG value, float deadZone) {
    const float normalized = std::clamp((static_cast<float>(value) - 32767.5F) / 32767.5F, -1.0F, 1.0F);
    return std::abs(normalized) <= deadZone ? 0.0F : normalized;
}

void PublishDirectInput(Slot& slot, InputDeviceState& state, std::uint8_t index) {
    DIJOYSTATE2 sample{};
    slot.device->Poll();
    HRESULT result = slot.device->GetDeviceState(sizeof(sample), &sample);
    if (result == DIERR_INPUTLOST || result == DIERR_NOTACQUIRED) {
        if (SUCCEEDED(slot.device->Acquire())) {
            slot.device->Poll();
            result = slot.device->GetDeviceState(sizeof(sample), &sample);
        }
    }
    if (FAILED(result) || !state.HasFocus()) return;
    detail::PublishDirectInputSnapshot(slot.mapping, slot.axes, sample, state, index);
}

void PublishXInput(const XINPUT_GAMEPAD& pad, InputDeviceState& state, std::uint8_t index) {
    for (const auto& binding : Win32InputKeyMap::GamepadButtons())
        state.SetKeyDown(binding.key, (pad.wButtons & binding.mask) != 0, index);
    const auto stick = [&](SHORT x, SHORT y, SHORT dead, std::size_t axis) {
        const float fx = static_cast<float>(x), fy = static_cast<float>(y);
        const float magnitude = std::sqrt(fx * fx + fy * fy);
        if (magnitude <= dead) return;
        const float scale = (std::min(magnitude, 32767.0F) - dead) / (32767.0F - dead);
        state.SetAnalog(analogKeys[axis], fx / magnitude * scale, index);
        state.SetAnalog(analogKeys[axis + 1], fy / magnitude * scale, index);
    };
    stick(pad.sThumbLX, pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, 0);
    stick(pad.sThumbRX, pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, 2);
    const auto trigger = [](BYTE value) { return value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 0.0F :
        static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / (255.0F - XINPUT_GAMEPAD_TRIGGER_THRESHOLD); };
    state.SetAnalog(InputKey::GamepadLeftTrigger, trigger(pad.bLeftTrigger), index);
    state.SetAnalog(InputKey::GamepadRightTrigger, trigger(pad.bRightTrigger), index);
}
} // namespace

void detail::PublishDirectInputSnapshot(const Win32GamepadMapping& mapping,
    const std::array<bool, 8>& availableAxes, const DIJOYSTATE2& sample,
    InputDeviceState& state, std::uint8_t index) {
    for (std::size_t button = 0; button < mapping.buttons.size(); ++button) {
        const int mapped = mapping.buttons[button];
        if (mapped >= 0 && mapped < 128)
            state.SetKeyDown(static_cast<InputKey>(300U + button), (sample.rgbButtons[mapped] & 0x80U) != 0, index);
    }
    if (mapping.pov >= 0 && mapping.pov < 4) {
        const DWORD angle = sample.rgdwPOV[mapping.pov];
        if (LOWORD(angle) != 0xffffU && angle < 36000) {
            const auto direction = ((angle + 2250U) / 4500U) % 8U;
            const auto press = [&](InputKey key, bool down) {
                state.SetKeyDown(key, state.IsKeyDown(key, index) || down, index);
            };
            press(InputKey::GamepadDPadUp, direction == 7 || direction <= 1);
            press(InputKey::GamepadDPadRight, direction >= 1 && direction <= 3);
            press(InputKey::GamepadDPadDown, direction >= 3 && direction <= 5);
            press(InputKey::GamepadDPadLeft, direction >= 5 && direction <= 7);
        }
    }
    const std::array axes{sample.lX, sample.lY, sample.lZ, sample.lRx, sample.lRy, sample.lRz,
        sample.rglSlider[0], sample.rglSlider[1]};
    for (std::size_t axis = 0; axis < analogKeys.size(); ++axis) {
        const int mapped = mapping.axes[axis];
        if (mapped < 0 || mapped >= 8 || !availableAxes[mapped]) continue;
        float value = axis >= 4 ? std::clamp(static_cast<float>(axes[mapped]) / 65535.0F, 0.0F, 1.0F) : Stick(axes[mapped], mapping.deadZone);
        if (axis == 1 || axis == 3) value = -value;
        state.SetAnalog(analogKeys[axis], value, index);
    }
}

void ConfigureWin32GamepadMapping(const GUID& product, const Win32GamepadMapping& mapping) {
    auto& registry = Devices();
    std::lock_guard lock{registry.mutex};
    auto sanitized = mapping;
    sanitized.deadZone = std::isfinite(mapping.deadZone) ? std::clamp(mapping.deadZone, 0.0F, 1.0F) : 0.15F;
    for (auto& entry : registry.mappings) if (entry.product == product) { entry.mapping = sanitized; return; }
    registry.mappings.push_back({product, sanitized});
}

void CollectWin32Gamepads(InputDeviceState& state, HWND window) noexcept {
    for (std::uint8_t slot = 0; slot < InputDeviceState::kMaxGamepads; ++slot) {
        state.SetGamepadConnected(slot, false);
        for (const auto& binding : Win32InputKeyMap::GamepadButtons()) state.SetKeyDown(binding.key, false, slot);
        for (auto key : analogKeys) state.SetAnalog(key, 0.0F, slot);
    }
    try {
        auto& registry = Devices();
        std::lock_guard lock{registry.mutex};
        std::array<XINPUT_STATE, XUSER_MAX_COUNT> readings{};
        std::array<bool, XUSER_MAX_COUNT> connected{};
        for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) connected[user] = XInputGetState(user, &readings[user]) == ERROR_SUCCESS;
        for (auto& slot : registry.slots) if (slot.user != noUser && !connected[slot.user]) slot = {};
        registry.Scan(window);
        for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) {
            if (!connected[user] || std::any_of(registry.slots.begin(), registry.slots.end(),
                [&](const Slot& slot) { return slot.user == user; })) continue;
            auto free = std::find_if(registry.slots.begin(), registry.slots.end(), [](const Slot& slot) { return slot.Empty(); });
            if (free != registry.slots.end()) free->user = user;
        }
        for (std::uint8_t index = 0; index < registry.slots.size(); ++index) {
            auto& slot = registry.slots[index];
            state.SetGamepadConnected(index, !slot.Empty());
            if (slot.device) PublishDirectInput(slot, state, index);
            else if (slot.user != noUser && state.HasFocus()) PublishXInput(readings[slot.user].Gamepad, state, index);
        }
    } catch (...) { /* Failed discovery leaves released controls and can retry next frame. */ }
}

std::uint32_t Win32GamepadXInputIndex(std::uint32_t slot) noexcept {
    try {
        auto& registry = Devices();
        std::lock_guard lock{registry.mutex};
        return slot < registry.slots.size() ? registry.slots[slot].user : noUser;
    } catch (...) { return noUser; }
}
} // namespace kb::input
