#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
namespace kb::input {
enum class InputSensorKind : std::uint8_t {Accelerometer,Gyroscope};
enum class InputSensorSource : std::uint8_t {None,WindowsSensors,Replay};
enum class InputSensorStatus : std::uint8_t {Unsupported,Unavailable,Starting,Active,Suspended,PermissionDenied,Error,Stale};
struct InputSensorSample {
    InputSensorStatus status=InputSensorStatus::Unsupported;
    // Native device axes: acceleration includes gravity in m/s^2; gyro in rad/s.
    std::array<float,3> value{};
    std::uint64_t sampleTimeNanoseconds=0;
    std::uint32_t nativeError=0;
    [[nodiscard]] bool Available() const noexcept {return status==InputSensorStatus::Active;}
    bool operator==(const InputSensorSample&) const = default;
};
struct InputSensorFrame {
    InputSensorSource source=InputSensorSource::None;
    bool replayed=false;
    InputSensorSample accelerometer{},gyroscope{};
    bool operator==(const InputSensorFrame&) const = default;
};
[[nodiscard]] inline bool IsInputSensorFrameValid(const InputSensorFrame& frame) noexcept {
    if(frame.source>InputSensorSource::Replay)return false;
    for(const auto* sample:{&frame.accelerometer,&frame.gyroscope}) {
        if(sample->status>InputSensorStatus::Stale||sample->sampleTimeNanoseconds>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))return false;
        for(float value:sample->value)if(!std::isfinite(value))return false;
        if(sample->Available()){if(frame.source==InputSensorSource::None||sample->sampleTimeNanoseconds==0||sample->nativeError!=0)return false;}
        else if(sample->sampleTimeNanoseconds!=0||sample->value!=std::array<float,3>{})return false;
    }
    return true;
}
[[nodiscard]] inline const char* ToString(InputSensorStatus status) noexcept {
    switch(status){case InputSensorStatus::Unsupported:return "Unsupported";case InputSensorStatus::Unavailable:return "Unavailable";case InputSensorStatus::Starting:return "Starting";case InputSensorStatus::Active:return "Active";case InputSensorStatus::Suspended:return "Suspended";case InputSensorStatus::PermissionDenied:return "PermissionDenied";case InputSensorStatus::Error:return "Error";case InputSensorStatus::Stale:return "Stale";}return "Invalid";
}
[[nodiscard]] inline const char* ToString(InputSensorSource source) noexcept {switch(source){case InputSensorSource::None:return "None";case InputSensorSource::WindowsSensors:return "WindowsSensors";case InputSensorSource::Replay:return "Replay";}return "Invalid";}
[[nodiscard]] inline bool ParseInputSensorKind(std::string_view value,InputSensorKind& out) noexcept {if(value=="Accelerometer"||value=="accelerometer"){out=InputSensorKind::Accelerometer;return true;}if(value=="Gyroscope"||value=="gyroscope"){out=InputSensorKind::Gyroscope;return true;}return false;}
[[nodiscard]] inline const InputSensorSample* FindInputSensorSample(const InputSensorFrame& frame,InputSensorKind kind) noexcept {switch(kind){case InputSensorKind::Accelerometer:return &frame.accelerometer;case InputSensorKind::Gyroscope:return &frame.gyroscope;}return nullptr;}
class IInputSensorProvider {
public:
    virtual ~IInputSensorProvider()=default;
    [[nodiscard]] virtual InputSensorFrame Poll(bool active) noexcept=0;
};
}
