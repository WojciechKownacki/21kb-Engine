#include "engine/input/InputRecording.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <array>
#include <cstddef>

namespace kb::input {
namespace {

// Reuse the engine's byte-level (de)serialization helpers; they are generic and
// not scene-specific despite their namespace.
namespace io = kb::scene::SceneAssetBinaryIO;

[[nodiscard]] bool ReadMagic(io::ByteReader& reader, const std::array<std::uint8_t, 8U>& expected) {
    std::array<std::uint8_t, 8U> magic{};
    if (!reader.ReadRaw(magic.data(), magic.size())) {
        return false;
    }
    return magic == expected;
}

void WriteTouchPoint(std::vector<std::uint8_t>& output, const InputTouchPoint& point) {
    io::WriteUInt32(output, point.id);
    io::WriteFloat(output, point.x);
    io::WriteFloat(output, point.y);
    io::WriteUInt8(output, static_cast<std::uint8_t>(point.phase));
}

[[nodiscard]] bool ReadTouchPoint(io::ByteReader& reader, InputTouchPoint& point) {
    std::uint8_t phase = 0U;
    if (!reader.ReadUInt32(point.id) || !reader.ReadFloat(point.x) || !reader.ReadFloat(point.y) ||
        !reader.ReadUInt8(phase) || phase > static_cast<std::uint8_t>(InputTouchPhase::Ended)) {
        return false;
    }
    point.phase = static_cast<InputTouchPhase>(phase);
    return true;
}

void WriteFrame(std::vector<std::uint8_t>& output, const InputFrameSnapshot& frame) {
    io::WriteFloat(output, frame.deltaSeconds);

    io::WriteUInt32(output, static_cast<std::uint32_t>(frame.digitalDown.size()));
    for (const InputRecordedDigitalKey& entry : frame.digitalDown) {
        io::WriteUInt32(output, static_cast<std::uint32_t>(entry.key));
        io::WriteUInt8(output, entry.gamepadIndex);
    }

    io::WriteUInt32(output, static_cast<std::uint32_t>(frame.analogValues.size()));
    for (const InputRecordedAnalogKey& entry : frame.analogValues) {
        io::WriteUInt32(output, static_cast<std::uint32_t>(entry.key));
        io::WriteUInt8(output, entry.gamepadIndex);
        io::WriteFloat(output, entry.value);
    }

    io::WriteUInt32(output, static_cast<std::uint32_t>(frame.touchPoints.size()));
    for (const InputTouchPoint& point : frame.touchPoints) {
        WriteTouchPoint(output, point);
    }

    io::WriteFloat(output, frame.pointerX);
    io::WriteFloat(output, frame.pointerY);
    io::WriteBool(output, frame.hasFocus);
    for (const bool connected : frame.gamepadConnected) {
        io::WriteBool(output, connected);
    }
}

[[nodiscard]] bool ReadFrame(io::ByteReader& reader, InputFrameSnapshot& frame) {
    if (!reader.ReadFloat(frame.deltaSeconds)) {
        return false;
    }

    std::uint32_t digitalCount = 0U;
    if (!reader.ReadUInt32(digitalCount) || digitalCount > InputAssetFormat::MaxRecordingKeysPerFrame) {
        return false;
    }
    frame.digitalDown.resize(digitalCount);
    for (InputRecordedDigitalKey& entry : frame.digitalDown) {
        std::uint32_t key = 0U;
        if (!reader.ReadUInt32(key) || !reader.ReadUInt8(entry.gamepadIndex)) {
            return false;
        }
        entry.key = static_cast<InputKey>(static_cast<std::uint16_t>(key));
    }

    std::uint32_t analogCount = 0U;
    if (!reader.ReadUInt32(analogCount) || analogCount > InputAssetFormat::MaxRecordingKeysPerFrame) {
        return false;
    }
    frame.analogValues.resize(analogCount);
    for (InputRecordedAnalogKey& entry : frame.analogValues) {
        std::uint32_t key = 0U;
        if (!reader.ReadUInt32(key) || !reader.ReadUInt8(entry.gamepadIndex) || !reader.ReadFloat(entry.value)) {
            return false;
        }
        entry.key = static_cast<InputKey>(static_cast<std::uint16_t>(key));
    }

    std::uint32_t touchCount = 0U;
    if (!reader.ReadUInt32(touchCount) || touchCount > InputAssetFormat::MaxRecordingKeysPerFrame) {
        return false;
    }
    frame.touchPoints.resize(touchCount);
    for (InputTouchPoint& point : frame.touchPoints) {
        if (!ReadTouchPoint(reader, point)) {
            return false;
        }
    }

    if (!reader.ReadFloat(frame.pointerX) || !reader.ReadFloat(frame.pointerY) || !reader.ReadBool(frame.hasFocus)) {
        return false;
    }
    for (bool& connected : frame.gamepadConnected) {
        if (!reader.ReadBool(connected)) {
            return false;
        }
    }
    return true;
}

} // namespace

InputFrameSnapshot CaptureInputFrame(const InputDeviceState& device, float deltaSeconds) {
    InputFrameSnapshot frame;
    frame.deltaSeconds = deltaSeconds;

    for (std::uint32_t raw = 0U; raw < static_cast<std::uint32_t>(InputKey::Count); ++raw) {
        const auto key = static_cast<InputKey>(raw);
        if (key == InputKey::None || key == InputKey::TouchDown) {
            continue; // TouchDown is derived from touchPoints below, not an independent signal.
        }
        const std::uint8_t gamepadSlots =
            DeviceKindOf(key) == InputDeviceKind::Gamepad ? InputDeviceState::kMaxGamepads : 1U;
        for (std::uint8_t gamepadIndex = 0U; gamepadIndex < gamepadSlots; ++gamepadIndex) {
            if (IsAnalogKey(key)) {
                const float value = device.GetValue(key, gamepadIndex);
                if (value != 0.0F) {
                    frame.analogValues.push_back(InputRecordedAnalogKey{.key = key, .gamepadIndex = gamepadIndex, .value = value});
                }
            } else if (device.IsKeyDown(key, gamepadIndex)) {
                frame.digitalDown.push_back(InputRecordedDigitalKey{.key = key, .gamepadIndex = gamepadIndex});
            }
        }
    }

    const std::span<const InputTouchPoint> touchSpan = device.TouchPoints();
    frame.touchPoints.assign(touchSpan.begin(), touchSpan.end());
    frame.pointerX = device.PointerX();
    frame.pointerY = device.PointerY();
    frame.hasFocus = device.HasFocus();
    for (std::uint8_t index = 0U; index < InputDeviceState::kMaxGamepads; ++index) {
        frame.gamepadConnected[index] = device.IsGamepadConnected(index);
    }
    return frame;
}

void ApplyInputFrame(InputDeviceState& device, const InputFrameSnapshot& frame) {
    device.Reset();
    for (const InputRecordedDigitalKey& entry : frame.digitalDown) {
        device.SetKeyDown(entry.key, true, entry.gamepadIndex);
    }
    for (const InputRecordedAnalogKey& entry : frame.analogValues) {
        device.SetAnalog(entry.key, entry.value, entry.gamepadIndex);
    }
    device.SetTouchPoints(frame.touchPoints);
    device.SetPointerPosition(frame.pointerX, frame.pointerY);
    device.SetHasFocus(frame.hasFocus);
    for (std::uint8_t index = 0U; index < InputDeviceState::kMaxGamepads; ++index) {
        device.SetGamepadConnected(index, frame.gamepadConnected[index]);
    }
}

void ReplayInputRecording(InputSubsystem& subsystem, std::span<const InputFrameSnapshot> recording) {
    InputDeviceState scratch;
    for (const InputFrameSnapshot& frame : recording) {
        ApplyInputFrame(scratch, frame);
        subsystem.EvaluateWithDeviceState(scratch, frame.deltaSeconds);
    }
}

std::vector<std::uint8_t> EncodeInputRecording(const InputRecording& recording) {
    std::vector<std::uint8_t> output;
    io::WriteRaw(output, InputAssetFormat::RecordingMagic.data(), InputAssetFormat::RecordingMagic.size());
    io::WriteUInt32(output, InputAssetFormat::BinaryVersion);
    io::WriteUInt32(output, static_cast<std::uint32_t>(recording.size()));
    for (const InputFrameSnapshot& frame : recording) {
        WriteFrame(output, frame);
    }
    return output;
}

InputAssetLoadResult<InputRecording> DecodeInputRecording(std::span<const std::uint8_t> bytes) {
    io::ByteReader reader(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    if (!ReadMagic(reader, InputAssetFormat::RecordingMagic)) {
        return InputAssetLoadResult<InputRecording>{.succeeded = false, .asset = {}, .error = "Invalid input recording magic"};
    }
    std::uint32_t version = 0U;
    if (!reader.ReadUInt32(version) || version != InputAssetFormat::BinaryVersion) {
        return InputAssetLoadResult<InputRecording>{.succeeded = false, .asset = {}, .error = "Unsupported input recording version"};
    }

    std::uint32_t frameCount = 0U;
    if (!reader.ReadUInt32(frameCount) || frameCount > InputAssetFormat::MaxRecordingFrameCount) {
        return InputAssetLoadResult<InputRecording>{.succeeded = false, .asset = {}, .error = "Corrupt input recording payload"};
    }

    InputRecording recording;
    recording.resize(frameCount);
    for (InputFrameSnapshot& frame : recording) {
        if (!ReadFrame(reader, frame)) {
            return InputAssetLoadResult<InputRecording>{.succeeded = false, .asset = {}, .error = "Corrupt input recording frame"};
        }
    }
    return InputAssetLoadResult<InputRecording>{.succeeded = true, .asset = std::move(recording), .error = {}};
}

InputAssetLoadResult<InputRecording> ReadInputRecording(const std::filesystem::path& path) {
    return DecodeInputRecording(io::ReadAllBytes(path));
}

bool WriteInputRecording(const std::filesystem::path& path, const InputRecording& recording) {
    return io::WriteBytesAtomically(path, EncodeInputRecording(recording));
}

} // namespace kb::input
