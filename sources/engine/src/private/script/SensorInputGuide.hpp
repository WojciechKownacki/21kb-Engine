#pragma once
#include <string_view>
namespace kb::script {
inline constexpr std::string_view kSensorInputGuide=R"guide(# Accelerometer, gyroscope and input replay

`InputDeviceState::Sensors()` carries both sensor samples in the existing raw
snapshot. `InputSubsystem::SensorState()` returns the last evaluated frame, including
explicit replay and modal gameplay blocking. Sensor data is separate from gamepad
axes and has explicit status, provenance, native error and sample timestamp.

Acceleration includes gravity and uses m/s^2; angular velocity uses rad/s. Axes are
the sensor's native device axes, without a scene/world or display-rotation remap.
Timestamps are nanoseconds on the provider's monotonic clock and describe the last
sample; an unchanged timestamp is not a fresh measurement. Active samples are finite
and timestamped; inactive samples clear values/time. `SetSensors` rejects malformed
frames without replacing the previous state. Reset clears sensor data.

```cpp
#include "engine/input/InputRecording.hpp"
kb::input::InputSensorFrame synthetic;
synthetic.source = kb::input::InputSensorSource::Replay;
synthetic.accelerometer.status = kb::input::InputSensorStatus::Active;
synthetic.accelerometer.value = {0, 9.80665F, 0};
synthetic.accelerometer.sampleTimeNanoseconds = 1000000000;
kb::input::InputDeviceState device;
if (!device.SetSensors(synthetic)) { /* report invalid input */ }
kb::input::InputRecording frames{kb::input::CaptureInputFrame(device, .016F)};
if (!kb::input::WriteInputRecording(outputPath, frames)) { /* report write error */ }
// Store under Assets; the normal discovery and cooker handle .21kbinputrecording.
std::string error;
if (!kb::input::ApplyInputRecordingFrame(scene,
        "/Game/Input/Motion.21kbinputrecording", 0, error)) { /* report error */ }
const auto& sample = scene.Input().SensorState().accelerometer;
```

The current recording schema is v6. Captures with default empty sensor state keep established v5 bytes;
v3/v4/v5 still decode with unsupported/empty sensor state. v5 cancellation semantics
remain frozen. Replay is explicitly marked and uses the shared ApplyInputFrame /
Evaluate path. The batch ReplayInputRecording leaves the live physical snapshot
unchanged; evaluated SensorState observes the replay. Applying an asset frame through
ApplyInputRecordingFrame updates primary input deliberately. Invalid assets/indices
leave input unchanged. `@asset` annotations retain recordings in source-free exports.

```lua
assert(CallFunction("Input.ReplayFrame", {
  asset="/Game/Input/Motion.21kbinputrecording", frame=0 }))
local reading=CallFunction("Input.Sensor", {kind="Accelerometer"})
if reading.available then
  assert(reading.unit=="m/s^2" and reading.replayed)
  -- reading.x/y/z and reading.timeNanoseconds describe this recorded sample.
end
```

Input.Sensor accepts Accelerometer or Gyroscope; raw=true reads the physical snapshot.
Its outputs include available, status, x/y/z, timeNanoseconds, unit, source, replayed,
and nativeError. Raw access is distinct from gameplay filtering. Unsupported,
Unavailable, Starting, Suspended, PermissionDenied, Error and Stale are not valid
zero measurements: available is false.

The shared Win32 collector owns a real Windows Sensors API provider. While foreground
it reads the first 3-axis accelerometer and gyrometer, converts g / degrees-per-second
to SI, and retains a stable monotonic timestamp for each reported sample. Reports older
than 500 ms are Stale. Missing devices are reprobed once per second. Losing activity
releases sensor handles; resume rediscovers them. No permission prompts are requested.
The standalone Win32MotionSensors provider supports explicit owner-thread polling.
Other hosts currently report Unsupported; no mobile sensor provider is claimed.
A headless replay or an unavailable Windows API/device does not qualify physical
measurement. Physical acceleration and gyro reports require a real supported sensor.
The larger raw snapshot requires native scripting SDK registration API version 7
and C++ engine-module ABI 26; older binaries are rejected before entering engine code.
The opaque C module SDK remains version 1.
)guide";
}
