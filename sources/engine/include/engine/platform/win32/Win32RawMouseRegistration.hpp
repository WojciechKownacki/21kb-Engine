#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <vector>

namespace kb::input {
// Registration belongs to the host: only one window can receive a device class
// in a process. Never replace another host's existing registration.
class Win32RawMouseRegistration final {
public:
    Win32RawMouseRegistration() = default;
    ~Win32RawMouseRegistration() { Reset(); }
    Win32RawMouseRegistration(const Win32RawMouseRegistration&) = delete;
    Win32RawMouseRegistration& operator=(const Win32RawMouseRegistration&) = delete;

    bool Register(HWND window) noexcept {
        if (window == nullptr || !IsWindow(window)) return false;
        if (window == window_) return true;
        Reset();
        try {
            for (const auto& device : Registrations())
                if (device.usUsagePage == 1 && device.usUsage == 2) return false;
            const RAWINPUTDEVICE mouse{1, 2, 0, window};
            if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse))) return false;
            window_ = window;
            return true;
        } catch (...) { return false; }
    }

    void Reset() noexcept {
        if (window_ == nullptr) return;
        try {
            for (const auto& device : Registrations()) {
                if (device.usUsagePage != 1 || device.usUsage != 2 ||
                    device.hwndTarget != window_ || device.dwFlags != 0) continue;
                const RAWINPUTDEVICE remove{1, 2, RIDEV_REMOVE, nullptr};
                RegisterRawInputDevices(&remove, 1, sizeof(remove));
                break;
            }
        } catch (...) {}
        window_ = nullptr;
    }

private:
    static std::vector<RAWINPUTDEVICE> Registrations() {
        UINT count = 0;
        if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) == UINT(-1))
            throw GetLastError();
        std::vector<RAWINPUTDEVICE> devices(count);
        if (count != 0) {
            const UINT read = GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE));
            if (read == UINT(-1)) throw GetLastError();
            devices.resize(read);
        }
        return devices;
    }
    HWND window_ = nullptr;
};
}
#endif
