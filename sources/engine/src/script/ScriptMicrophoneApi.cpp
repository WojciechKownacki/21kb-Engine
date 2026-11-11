#include "engine/script/ScriptMicrophoneApi.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/audio/AudioCapture.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

// A call that cannot run at all: no scene or a malformed argument.
ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

// A call that ran and was refused by the device or the platform: a script can answer it.
ScriptFunctionCallResult Failed(kb::audio::AudioCaptureStatus status) {
    return ScriptFunctionCallResult{ .executed = true, .outputs = {}, .errors = { std::string{ kb::audio::ToString(status) } } };
}

ScriptFunctionCallResult Done(std::vector<ScriptFunctionArgument> outputs) {
    return ScriptFunctionCallResult{ .executed = true, .outputs = std::move(outputs), .errors = {} };
}

[[nodiscard]] ScriptFunctionArgument Out(std::string name, ScriptValue value) {
    return ScriptFunctionArgument{ std::move(name), std::move(value) };
}

[[nodiscard]] float WindowArg(std::span<const ScriptFunctionArgument> arguments) noexcept {
    const ScriptValue* value = FindArg(arguments, "window");
    return value == nullptr ? kb::audio::kAudioCaptureLevelWindowSeconds : value->AsFloat(kb::audio::kAudioCaptureLevelWindowSeconds);
}

[[nodiscard]] bool IsSecondsValid(float seconds) noexcept {
    return std::isfinite(seconds) && seconds > 0.0F && seconds <= kb::audio::kAudioCaptureHistorySeconds;
}

ScriptFunctionCallResult MicrophoneDevices(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) return Error("microphone api requires an active scene");
    const std::vector<kb::audio::AudioCaptureDeviceInfo> devices = kb::audio::AudioCapture::Devices(*context.scene);
    int defaultIndex = -1;
    for (std::size_t index = 0U; index < devices.size(); ++index) {
        if (devices[index].isDefault) {
            defaultIndex = static_cast<int>(index);
            break;
        }
    }
    return Done({ Out("count", ScriptValue{ static_cast<int>(devices.size()) }), Out("defaultIndex", ScriptValue{ defaultIndex }) });
}

ScriptFunctionCallResult MicrophoneDevice(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("microphone api requires an active scene");
    const ScriptValue* indexArgument = FindArg(arguments, "index");
    if (indexArgument == nullptr || indexArgument->Type() != ScriptValueType::Int) return Error("microphone device index must be an integer");
    const int index = indexArgument->AsInt(-1);
    const std::vector<kb::audio::AudioCaptureDeviceInfo> devices = kb::audio::AudioCapture::Devices(*context.scene);
    if (index < 0 || static_cast<std::size_t>(index) >= devices.size()) {
        return Done({ Out("found", ScriptValue{ false }), Out("name", ScriptValue{ std::string{} }), Out("isDefault", ScriptValue{ false }) });
    }
    const kb::audio::AudioCaptureDeviceInfo& device = devices[static_cast<std::size_t>(index)];
    return Done({ Out("found", ScriptValue{ true }), Out("name", ScriptValue{ device.name }), Out("isDefault", ScriptValue{ device.isDefault }) });
}

ScriptFunctionCallResult MicrophoneStart(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("microphone api requires an active scene");
    const ScriptValue* device = FindArg(arguments, "device");
    const kb::audio::AudioCaptureStatus status = kb::audio::AudioCapture::Start(
        *context.scene, kb::audio::AudioCaptureDesc{ .deviceName = device == nullptr ? std::string{} : device->AsString() });
    if (status != kb::audio::AudioCaptureStatus::Success) return Failed(status);
    return Done({ Out("started", ScriptValue{ true }) });
}

ScriptFunctionCallResult MicrophoneStop(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) return Error("microphone api requires an active scene");
    const bool wasCapturing = kb::audio::AudioCapture::IsCapturing(*context.scene);
    kb::audio::AudioCapture::Stop(*context.scene);
    return Done({ Out("stopped", ScriptValue{ wasCapturing }) });
}

ScriptFunctionCallResult MicrophoneIsRecording(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) return Error("microphone api requires an active scene");
    return Done({ Out("recording", ScriptValue{ kb::audio::AudioCapture::IsCapturing(*context.scene) }) });
}

ScriptFunctionCallResult MicrophoneLevel(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("microphone api requires an active scene");
    const float window = WindowArg(arguments);
    if (!IsSecondsValid(window)) return Error("microphone level window must be above 0 and at most the capture history in seconds");
    return Done({ Out("level", ScriptValue{ kb::audio::AudioCapture::Level(*context.scene, window).rms }) });
}

ScriptFunctionCallResult MicrophonePeak(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("microphone api requires an active scene");
    const float window = WindowArg(arguments);
    if (!IsSecondsValid(window)) return Error("microphone peak window must be above 0 and at most the capture history in seconds");
    return Done({ Out("peak", ScriptValue{ kb::audio::AudioCapture::Level(*context.scene, window).peak }) });
}

ScriptFunctionCallResult MicrophoneRecordToClip(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("microphone api requires an active scene");
    const ScriptValue* secondsArgument = FindArg(arguments, "seconds");
    const float seconds = secondsArgument == nullptr ? 0.0F : secondsArgument->AsFloat(0.0F);
    if (!IsSecondsValid(seconds)) return Error("microphone recording length must be above 0 and at most the capture history in seconds");
    const kb::audio::AudioCaptureClip recorded = kb::audio::AudioCapture::RecordToClip(*context.scene, seconds);
    if (!recorded.Succeeded()) return Failed(recorded.status);
    const std::uint64_t clipAssetId = kb::audio::AudioCapture::PublishClip(*context.scene, recorded);
    if (clipAssetId == 0U) return Failed(kb::audio::AudioCaptureStatus::ClipRegistrationFailed);
    return Done({
        Out("clip", ScriptValue{ kb::assets::ToString(kb::assets::AssetId{ clipAssetId }) }),
        Out("seconds", ScriptValue{ static_cast<float>(static_cast<double>(recorded.frames) / recorded.sampleRate) }),
        Out("sampleRate", ScriptValue{ static_cast<int>(recorded.sampleRate) }),
    });
}

[[nodiscard]] bool RegisterFn(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptMicrophoneApi::Register(ScriptRuntimeHost& host) {
    const auto pin = [](std::string name, ScriptValueType type, bool required) { return ScriptFunctionPin{ std::move(name), type, required }; };
    bool ok = RegisterFn(host, "Microphone.Devices", {}, { pin("count", ScriptValueType::Int, true), pin("defaultIndex", ScriptValueType::Int, true) },
        &MicrophoneDevices);
    ok = RegisterFn(host, "Microphone.Device", { pin("index", ScriptValueType::Int, true) },
        { pin("found", ScriptValueType::Bool, true), pin("name", ScriptValueType::String, true), pin("isDefault", ScriptValueType::Bool, true) },
        &MicrophoneDevice) && ok;
    ok = RegisterFn(host, "Microphone.Start", { pin("device", ScriptValueType::String, false) }, { pin("started", ScriptValueType::Bool, true) },
        &MicrophoneStart) && ok;
    ok = RegisterFn(host, "Microphone.Stop", {}, { pin("stopped", ScriptValueType::Bool, true) }, &MicrophoneStop) && ok;
    ok = RegisterFn(host, "Microphone.IsRecording", {}, { pin("recording", ScriptValueType::Bool, true) }, &MicrophoneIsRecording) && ok;
    ok = RegisterFn(host, "Microphone.Level", { pin("window", ScriptValueType::Float, false) }, { pin("level", ScriptValueType::Float, true) },
        &MicrophoneLevel) && ok;
    ok = RegisterFn(host, "Microphone.Peak", { pin("window", ScriptValueType::Float, false) }, { pin("peak", ScriptValueType::Float, true) },
        &MicrophonePeak) && ok;
    ok = RegisterFn(host, "Microphone.RecordToClip", { pin("seconds", ScriptValueType::Float, true) },
        { pin("clip", ScriptValueType::String, true), pin("seconds", ScriptValueType::Float, true), pin("sampleRate", ScriptValueType::Int, true) },
        &MicrophoneRecordToClip) && ok;
    return ok;
}

} // namespace kb::script
