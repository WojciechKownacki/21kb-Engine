#pragma once

#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/input/InputTouchPoint.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace kb::input {

// One digital key that was down, or one nonzero analog value, at the moment a
// frame was captured. gamepadIndex is only meaningful for gamepad keys (see
// InputDeviceState's normalized device model, LIB-116) and is otherwise 0.
struct InputRecordedDigitalKey {
    InputKey key = InputKey::None;
    std::uint8_t gamepadIndex = 0U;
};

struct InputRecordedAnalogKey {
    InputKey key = InputKey::None;
    std::uint8_t gamepadIndex = 0U;
    float value = 0.0F;
};

// A full, self-contained snapshot of everything InputDeviceState carries for
// one frame, plus the deltaSeconds that frame was evaluated with (trigger
// timing - Hold/Tap/etc, InputTriggers.hpp - depends on dt, so replaying the
// device state alone is not enough to reproduce identical results; dt must be
// recorded too). Only DOWN keys / non-zero analog values are stored (most of
// InputKey::Count's ~512 slots are idle almost every frame), not a dense
// array - keeps recordings small without losing any information Apply needs
// to reconstruct the exact original state.
struct InputFrameSnapshot {
    float deltaSeconds = 0.0F;
    std::vector<InputRecordedDigitalKey> digitalDown;
    std::vector<InputRecordedAnalogKey> analogValues;
    std::vector<InputTouchPoint> touchPoints;
    float pointerX = 0.0F;
    float pointerY = 0.0F;
    bool hasFocus = false;
    std::array<bool, InputDeviceState::kMaxGamepads> gamepadConnected{};
};

using InputRecording = std::vector<InputFrameSnapshot>;

// Captures everything currently in `device` (LIB-121) - every digital key that
// is down, every non-zero analog value (for every relevant gamepad slot),
// every touch point, pointer position, focus, and gamepad connectivity - into
// a self-contained snapshot, tagged with the deltaSeconds this frame was (or
// will be) evaluated with.
[[nodiscard]] InputFrameSnapshot CaptureInputFrame(const InputDeviceState& device, float deltaSeconds);

// Restores `device` to exactly what `frame` captured: Reset(), then replays
// every recorded digital/analog/touch/pointer/focus/connectivity value.
void ApplyInputFrame(InputDeviceState& device, const InputFrameSnapshot& frame);

// Replays every frame of `recording`, in order, through `subsystem` via
// EvaluateWithDeviceState - the same real entry point InputSubsystem::
// Evaluate uses, confirmed to have no hidden non-determinism (no wall-clock
// reads anywhere in InputMappingEvaluator/InputTriggers; deltaSeconds is the
// only time source, and it is part of the recording). A subsystem replayed
// this way against a recording captured from a live run reaches byte-
// identical action states frame-by-frame - the property deterministic tests
// need. Uses a scratch InputDeviceState owned by this call, not `subsystem`'s
// own device state, so replay does not depend on or mutate whatever the
// subsystem's live device state currently holds.
void ReplayInputRecording(InputSubsystem& subsystem, std::span<const InputFrameSnapshot> recording);

// Binary round-trip for a whole recording (LIB-121). Reuses InputAssetIO's
// magic/version/atomic-write conventions rather than inventing new I/O, same
// as every other kb::input binary format this session (InputAssetIO,
// InputRebinding).
[[nodiscard]] std::vector<std::uint8_t> EncodeInputRecording(const InputRecording& recording);
[[nodiscard]] InputAssetLoadResult<InputRecording> DecodeInputRecording(std::span<const std::uint8_t> bytes);
[[nodiscard]] InputAssetLoadResult<InputRecording> ReadInputRecording(const std::filesystem::path& path);
[[nodiscard]] bool WriteInputRecording(const std::filesystem::path& path, const InputRecording& recording);

} // namespace kb::input
