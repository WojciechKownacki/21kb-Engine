#include "LinuxGamepadInput.hpp"

#include <linux/input.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace kb::game {
namespace {
using input::InputKey;
template<std::size_t Count> using Bits = std::array<unsigned char, (Count + 7U) / 8U>;
template<std::size_t Size>
bool Has(const std::array<unsigned char, Size>& bits, unsigned int code) noexcept {
    return code / 8U < Size && (bits[code / 8U] & (1U << (code % 8U))) != 0U;
}
constexpr std::array buttons{
    std::pair{BTN_SOUTH, InputKey::GamepadFaceBottom},
    std::pair{BTN_EAST, InputKey::GamepadFaceRight},
    std::pair{BTN_WEST, InputKey::GamepadFaceLeft},
    std::pair{BTN_NORTH, InputKey::GamepadFaceTop},
    std::pair{BTN_TL, InputKey::GamepadLeftShoulder},
    std::pair{BTN_TR, InputKey::GamepadRightShoulder},
    std::pair{BTN_THUMBL, InputKey::GamepadLeftThumb},
    std::pair{BTN_THUMBR, InputKey::GamepadRightThumb},
    std::pair{BTN_START, InputKey::GamepadStart},
    std::pair{BTN_SELECT, InputKey::GamepadBack},
    std::pair{BTN_DPAD_UP, InputKey::GamepadDPadUp},
    std::pair{BTN_DPAD_DOWN, InputKey::GamepadDPadDown},
    std::pair{BTN_DPAD_LEFT, InputKey::GamepadDPadLeft},
    std::pair{BTN_DPAD_RIGHT, InputKey::GamepadDPadRight},
};
constexpr std::array analogKeys{InputKey::GamepadLeftStickX, InputKey::GamepadLeftStickY,
    InputKey::GamepadRightStickX, InputKey::GamepadRightStickY,
    InputKey::GamepadLeftTrigger, InputKey::GamepadRightTrigger};

float Normalize(const input_absinfo& axis, bool trigger) noexcept {
    const double low = axis.minimum, high = axis.maximum;
    if (high <= low) return 0.0F;
    const double value = std::clamp(static_cast<double>(axis.value), low, high);
    const double center = trigger ? low : (low + high) * 0.5;
    const double delta = value - center;
    if (std::abs(delta) <= std::max(0, axis.flat)) return 0.0F;
    const double extent = trigger || delta >= 0.0 ? high - center : center - low;
    return static_cast<float>(std::clamp(delta / extent, trigger ? 0.0 : -1.0, 1.0));
}
} // namespace

struct LinuxGamepadInput::Device {
    int fd = -1;
    dev_t deviceId{};
    ino_t inode{};
    Bits<ABS_CNT> axes{};
    std::array<int, 6> mapping{ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ};
    ~Device() { if (fd >= 0) close(fd); }

    bool Sample(input::InputDeviceState& state, std::uint8_t slot) const {
        Bits<KEY_CNT> keys{};
        if (ioctl(fd, EVIOCGKEY(sizeof(keys)), keys.data()) < 0) return false;
        std::array<float, 6> values{};
        for (std::size_t index = 0; index < mapping.size(); ++index) {
            const auto code = static_cast<unsigned int>(mapping[index]);
            if (!Has(axes, code)) continue;
            input_absinfo axis{};
            if (ioctl(fd, EVIOCGABS(code), &axis) < 0) return false;
            values[index] = Normalize(axis, index >= 4U);
        }
        std::array<int, 2> hat{};
        for (unsigned int index = 0; index < hat.size(); ++index) {
            const unsigned int code = ABS_HAT0X + index;
            if (!Has(axes, code)) continue;
            input_absinfo axis{};
            if (ioctl(fd, EVIOCGABS(code), &axis) < 0) return false;
            hat[index] = axis.value;
        }
        std::array<bool, 2> shoulders{};
        for (unsigned int index = 0; index < shoulders.size(); ++index) {
            const unsigned int code = index == 0 ? ABS_HAT1Y : ABS_HAT1X;
            if (!Has(axes, code)) continue;
            input_absinfo axis{};
            if (ioctl(fd, EVIOCGABS(code), &axis) < 0) return false;
            shoulders[index] = Normalize(axis, true) > 0.0F;
        }
        if (!state.HasFocus()) return true;
        for (const auto& [code, key] : buttons)
            state.SetKeyDown(key, Has(keys, static_cast<unsigned int>(code)), slot);
        state.SetKeyDown(InputKey::GamepadLeftShoulder, Has(keys, BTN_TL) || shoulders[0], slot);
        state.SetKeyDown(InputKey::GamepadRightShoulder, Has(keys, BTN_TR) || shoulders[1], slot);
        state.SetKeyDown(InputKey::GamepadDPadLeft, Has(keys, BTN_DPAD_LEFT) || hat[0] < 0, slot);
        state.SetKeyDown(InputKey::GamepadDPadRight, Has(keys, BTN_DPAD_RIGHT) || hat[0] > 0, slot);
        state.SetKeyDown(InputKey::GamepadDPadUp, Has(keys, BTN_DPAD_UP) || hat[1] < 0, slot);
        state.SetKeyDown(InputKey::GamepadDPadDown, Has(keys, BTN_DPAD_DOWN) || hat[1] > 0, slot);
        if (!Has(axes, static_cast<unsigned int>(mapping[4]))) values[4] = Has(keys, BTN_TL2) ? 1.0F : 0.0F;
        if (!Has(axes, static_cast<unsigned int>(mapping[5]))) values[5] = Has(keys, BTN_TR2) ? 1.0F : 0.0F;
        values[1] = -values[1];
        values[3] = -values[3];
        for (std::size_t index = 0; index < values.size(); ++index)
            state.SetAnalog(analogKeys[index], values[index], slot);
        return true;
    }
};

LinuxGamepadInput::LinuxGamepadInput(std::filesystem::path directory) : directory_{std::move(directory)} {}
LinuxGamepadInput::~LinuxGamepadInput() = default;

void LinuxGamepadInput::Poll(input::InputDeviceState& state) {
    // Query the kernel's current snapshot rather than retaining an event queue.
    // This also recovers held controls after focus changes without stale events.
    for (std::uint8_t slot = 0; slot < devices_.size(); ++slot) {
        for (const auto& binding : buttons) state.SetKeyDown(binding.second, false, slot);
        for (auto key : analogKeys) state.SetAnalog(key, 0.0F, slot);
        if (devices_[slot] && !devices_[slot]->Sample(state, slot)) {
            devices_[slot].reset();
            nextScan_ = {};
        }
        state.SetGamepadConnected(slot, devices_[slot] != nullptr);
    }
    const auto now = std::chrono::steady_clock::now();
    if (now < nextScan_) return;
    nextScan_ = now + std::chrono::milliseconds{500};
    std::error_code error;
    std::vector<std::filesystem::path> paths;
    for (std::filesystem::directory_iterator it{directory_, error}, end;
         !error && it != end; it.increment(error)) {
        if (it->path().filename().string().starts_with("event")) paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
        auto free = std::find_if(devices_.begin(), devices_.end(), [](const auto& device) { return !device; });
        if (free == devices_.end()) break;
        struct stat info{};
        if (stat(path.c_str(), &info) != 0 || !S_ISCHR(info.st_mode)) continue;
        if (std::any_of(devices_.begin(), devices_.end(), [&](const auto& device) {
            return device && device->deviceId == info.st_rdev && device->inode == info.st_ino;
        })) continue;
        auto device = std::make_unique<Device>();
        device->fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (device->fd < 0) continue;
        if (fstat(device->fd, &info) != 0) continue;
        Bits<KEY_CNT> keys{};
        if (ioctl(device->fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys.data()) < 0 || !Has(keys, BTN_GAMEPAD)) continue;
        if (ioctl(device->fd, EVIOCGBIT(EV_ABS, sizeof(device->axes)), device->axes.data()) < 0) continue;
        device->deviceId = info.st_rdev;
        device->inode = info.st_ino;
        if (!Has(device->axes, ABS_RX) && !Has(device->axes, ABS_RY) &&
            Has(device->axes, ABS_Z) && Has(device->axes, ABS_RZ) &&
            (Has(device->axes, ABS_GAS) || Has(device->axes, ABS_BRAKE))) {
            device->mapping = {ABS_X, ABS_Y, ABS_Z, ABS_RZ, ABS_BRAKE, ABS_GAS};
        } else {
            if (Has(device->axes, ABS_HAT2Y)) device->mapping[4] = ABS_HAT2Y;
            if (Has(device->axes, ABS_HAT2X)) device->mapping[5] = ABS_HAT2X;
        }
        *free = std::move(device);
    }
}
} // namespace kb::game
