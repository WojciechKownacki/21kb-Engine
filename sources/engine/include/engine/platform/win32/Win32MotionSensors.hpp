#pragma once
#if defined(_WIN32)
#include "engine/input/InputSensors.hpp"
#include <memory>
namespace kb::input {
// Owner-thread-only Windows Sensors API provider. It never requests permissions.
class Win32MotionSensors final : public IInputSensorProvider {
public:
    Win32MotionSensors();
    ~Win32MotionSensors() override;
    Win32MotionSensors(const Win32MotionSensors&)=delete;
    Win32MotionSensors& operator=(const Win32MotionSensors&)=delete;
    [[nodiscard]] InputSensorFrame Poll(bool active) noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
