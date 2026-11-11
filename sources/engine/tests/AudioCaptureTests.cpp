#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/audio/AudioCapture.hpp"
#include "engine/audio/AudioClipAsset.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

using kb::audio::AudioCapture;
using kb::audio::AudioCaptureStatus;

constexpr std::uint32_t kRate = kb::audio::kAudioCaptureSampleRate;

// A playback backend that is also a capture backend whose "device" is a ring buffer the test feeds.
class FakeCaptureBackend final : public kb::audio::IAudioPlaybackBackend, public kb::audio::IAudioCaptureBackend {
public:
    std::vector<kb::audio::AudioCaptureDeviceInfo> devices{ { "Desk Microphone", true }, { "Headset", false } };
    std::string startedDevice;
    int startCalls = 0;
    std::unique_ptr<kb::audio::AudioCaptureRingBuffer> ring;
    bool capturing = false;

    void Feed(float value, std::size_t frames) {
        const std::vector<float> block(frames, value);
        ring->Write(block);
    }

    std::vector<kb::audio::AudioCaptureDeviceInfo> CaptureDevices(kb::scene::Scene&) override { return devices; }
    AudioCaptureStatus StartCapture(kb::scene::Scene&, const kb::audio::AudioCaptureDesc& desc) override {
        ++startCalls;
        if (capturing) return AudioCaptureStatus::AlreadyCapturing;
        const bool known = desc.deviceName.empty() || std::ranges::any_of(devices, [&](const auto& device) { return device.name == desc.deviceName; });
        if (!known) return AudioCaptureStatus::DeviceNotFound;
        startedDevice = desc.deviceName;
        ring = std::make_unique<kb::audio::AudioCaptureRingBuffer>(static_cast<std::size_t>(kb::audio::kAudioCaptureHistorySeconds) * kRate);
        capturing = true;
        return AudioCaptureStatus::Success;
    }
    void StopCapture(kb::scene::Scene&) noexcept override { capturing = false; }
    bool IsCapturing(kb::scene::Scene&) noexcept override { return capturing; }
    std::size_t ReadCaptured(kb::scene::Scene&, std::span<float> out) noexcept override { return ring == nullptr ? 0U : ring->ReadRecent(out); }

    kb::audio::AudioPlayResult PlayOneShot(kb::scene::Scene&, const kb::audio::AudioPlayDesc&) override { return {}; }
    void StopAll(kb::scene::Scene&) noexcept override {}
    kb::audio::AudioSourceControlResult PlaySource(kb::scene::Scene&, kb::scene::SceneEntity) override { return {}; }
    kb::audio::AudioSourceControlResult PauseSource(kb::scene::Scene&, kb::scene::SceneEntity) override { return {}; }
    kb::audio::AudioSourceControlResult ResumeSource(kb::scene::Scene&, kb::scene::SceneEntity) override { return {}; }
    kb::audio::AudioSourceControlResult StopSource(kb::scene::Scene&, kb::scene::SceneEntity) override { return {}; }
    kb::audio::AudioSourceControlResult IsSourcePlaying(kb::scene::Scene&, kb::scene::SceneEntity) override { return {}; }
    kb::audio::AudioDeviceStatus DeviceStatus() const noexcept override { return kb::audio::AudioDeviceStatus::PlaybackAvailable; }
    kb::audio::AudioDeviceStatus Reinitialize(kb::scene::Scene&) noexcept override { return DeviceStatus(); }
    bool StopVoice(kb::scene::Scene&, std::uint64_t) noexcept override { return false; }
    bool PauseVoice(kb::scene::Scene&, std::uint64_t) noexcept override { return false; }
    bool ResumeVoice(kb::scene::Scene&, std::uint64_t) noexcept override { return false; }
    bool SeekVoice(kb::scene::Scene&, std::uint64_t, float) noexcept override { return false; }
    bool SetVoiceVolume(kb::scene::Scene&, std::uint64_t, float) noexcept override { return false; }
    bool SetVoiceMute(kb::scene::Scene&, std::uint64_t, bool) noexcept override { return false; }
    bool SetVoicePan(kb::scene::Scene&, std::uint64_t, float) noexcept override { return false; }
    bool SetVoicePitch(kb::scene::Scene&, std::uint64_t, float) noexcept override { return false; }
    bool SetVoiceLoop(kb::scene::Scene&, std::uint64_t, bool) noexcept override { return false; }
    bool IsVoicePlaying(kb::scene::Scene&, std::uint64_t) noexcept override { return false; }
    float VoicePlaybackSeconds(kb::scene::Scene&, std::uint64_t) noexcept override { return -1.0F; }
    bool AddVoiceMarker(kb::scene::Scene&, std::uint64_t, std::string_view, float, kb::scene::SceneEntity) override { return false; }
    bool SetMasterVolume(kb::scene::Scene&, float) noexcept override { return true; }
};

[[nodiscard]] std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint32_t value = 0U;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void RunRingBufferTest() {
    kb::audio::AudioCaptureRingBuffer ring{ 8U };
    std::vector<float> out(8U);
    kb::tests::Require(ring.ReadRecent(out) == 0U && ring.WrittenFrames() == 0U, "An empty capture ring must read nothing");

    const std::vector<float> first{ 0.0F, 1.0F, 2.0F, 3.0F, 4.0F };
    ring.Write(first);
    kb::tests::Require(ring.ReadRecent(out) == 5U && out[0] == 0.0F && out[4] == 4.0F, "A capture ring must return what was written, oldest first");

    const std::vector<float> second{ 5.0F, 6.0F, 7.0F, 8.0F, 9.0F, 10.0F };
    ring.Write(second);
    kb::tests::Require(ring.WrittenFrames() == 11U, "A capture ring must count every written frame");
    kb::tests::Require(ring.ReadRecent(out) == 8U && out[0] == 3.0F && out[7] == 10.0F, "A wrapped capture ring must return its newest frames in order");
    std::vector<float> few(3U);
    kb::tests::Require(ring.ReadRecent(few) == 3U && few[0] == 8.0F && few[2] == 10.0F, "A short read must return the newest frames");
    kb::tests::Require(ring.ReadRecent(std::span<float>{}) == 0U, "A read into no space must read nothing");

    kb::audio::AudioCaptureRingBuffer small{ 4U };
    std::vector<float> longBlock(10U);
    for (std::size_t index = 0U; index < longBlock.size(); ++index) longBlock[index] = static_cast<float>(index);
    small.Write(longBlock);
    std::vector<float> four(4U);
    kb::tests::Require(small.WrittenFrames() == 10U && small.ReadRecent(four) == 4U && four[0] == 6.0F && four[3] == 9.0F,
        "A block longer than the ring must keep its newest frames");

    // One writer and one reader: every read must be a run of consecutive frames, never a torn one.
    kb::audio::AudioCaptureRingBuffer shared{ 1024U };
    std::atomic<bool> done{ false };
    std::thread writer([&] {
        std::vector<float> block(64U);
        float next = 0.0F;
        for (int round = 0; round < 40000; ++round) {
            for (float& value : block) value = next++;
            shared.Write(block);
        }
        done.store(true);
    });
    std::vector<float> window(512U);
    bool consecutive = true;
    std::uint64_t reads = 0U;
    while (!done.load()) {
        const std::size_t count = shared.ReadRecent(window);
        for (std::size_t index = 1U; index < count; ++index) {
            if (window[index] != window[index - 1U] + 1.0F) consecutive = false;
        }
        ++reads;
    }
    writer.join();
    kb::tests::Require(consecutive && reads > 0U, "A capture ring read while it is written must never return torn frames");
}

void RunLevelTest() {
    kb::tests::Require(kb::audio::ComputeAudioCaptureLevel({}).rms == 0.0F, "An empty window must have no level");
    const std::vector<float> constant(480U, 0.5F);
    const kb::audio::AudioCaptureLevel steady = kb::audio::ComputeAudioCaptureLevel(constant);
    kb::tests::Require(std::fabs(steady.rms - 0.5F) < 1.0e-6F && steady.peak == 0.5F, "A constant signal must have equal RMS and peak");
    std::vector<float> sine(4800U);
    for (std::size_t index = 0U; index < sine.size(); ++index) {
        sine[index] = 0.5F * std::sin(6.2831853F * 100.0F * static_cast<float>(index) / static_cast<float>(kRate));
    }
    const kb::audio::AudioCaptureLevel tone = kb::audio::ComputeAudioCaptureLevel(sine);
    kb::tests::Require(std::fabs(tone.rms - 0.5F / std::sqrt(2.0F)) < 0.002F && tone.peak > 0.499F && tone.peak <= 0.5F,
        "A sine of whole cycles must have RMS of amplitude over root two");
    const std::vector<float> damaged{ 0.0F, std::nanf(""), -0.25F, 0.0F };
    const kb::audio::AudioCaptureLevel guarded = kb::audio::ComputeAudioCaptureLevel(damaged);
    kb::tests::Require(std::isfinite(guarded.rms) && guarded.peak == 0.25F, "A non-finite sample must count as silence");
}

void RunClipTest() {
    kb::tests::Require(kb::audio::MakeAudioClipFromPcm({}, kRate) == nullptr && kb::audio::MakeAudioClipFromPcm(std::vector<float>(4U), 0U) == nullptr,
        "A clip needs samples and a sample rate");
    std::vector<float> samples(480U, 0.25F);
    const std::shared_ptr<kb::audio::AudioClipAsset> clip = kb::audio::MakeAudioClipFromPcm(samples, kRate);
    kb::tests::Require(clip != nullptr && clip->IsMemoryBacked() && clip->sourceExtension == ".wav", "A captured clip must be an in-memory wav");
    const std::vector<std::uint8_t>& bytes = clip->encodedBytes;
    kb::tests::Require(bytes.size() == 44U + samples.size() * sizeof(float) && std::memcmp(bytes.data(), "RIFF", 4U) == 0 && std::memcmp(bytes.data() + 8U, "WAVE", 4U) == 0,
        "A captured clip must hold a wav header and its samples");
    std::uint16_t format = 0U;
    std::uint16_t channels = 0U;
    std::memcpy(&format, bytes.data() + 20U, 2U);
    std::memcpy(&channels, bytes.data() + 22U, 2U);
    kb::tests::Require(format == 3U && channels == 1U && ReadU32(bytes, 24U) == kRate && ReadU32(bytes, 40U) == samples.size() * sizeof(float),
        "A captured clip must be mono 32-bit float at the capture rate with its exact length");
}

void RunFacadeWithoutBackendTest() {
    kb::scene::Scene scene;
    kb::tests::Require(!AudioCapture::HasBackend(scene) && AudioCapture::Devices(scene).empty() && !AudioCapture::IsCapturing(scene),
        "Without an audio backend there are no capture devices");
    kb::tests::Require(AudioCapture::Start(scene) == AudioCaptureStatus::BackendUnavailable, "Starting without a backend must say it is unavailable");
    kb::tests::Require(AudioCapture::RecordToClip(scene, 1.0F).status == AudioCaptureStatus::BackendUnavailable && AudioCapture::Level(scene).rms == 0.0F,
        "Without a backend there is nothing to record");
}

void RunFacadeTest() {
    kb::scene::Scene scene;
    FakeCaptureBackend backend;
    kb::audio::AudioPlayback::RegisterBackend(scene, backend);

    kb::tests::Require(AudioCapture::HasBackend(scene) && AudioCapture::Devices(scene).size() == 2U && AudioCapture::Devices(scene)[0].isDefault,
        "The facade must list the backend's capture devices");
    kb::tests::Require(AudioCapture::Level(scene).peak == 0.0F && AudioCapture::RecordToClip(scene, 1.0F).status == AudioCaptureStatus::NoAudio,
        "A capture that never started has no level and no audio");
    kb::tests::Require(AudioCapture::Start(scene, { .deviceName = "Missing" }) == AudioCaptureStatus::DeviceNotFound, "A device that does not exist must be refused");
    kb::tests::Require(AudioCapture::Start(scene, { .deviceName = "Headset" }) == AudioCaptureStatus::Success && backend.startedDevice == "Headset" && AudioCapture::IsCapturing(scene),
        "A named device must start");
    kb::tests::Require(AudioCapture::Start(scene) == AudioCaptureStatus::AlreadyCapturing, "A second start must be refused while capturing");

    backend.Feed(0.25F, kRate / 2U);
    const kb::audio::AudioCaptureLevel level = AudioCapture::Level(scene);
    kb::tests::Require(std::fabs(level.rms - 0.25F) < 1.0e-6F && level.peak == 0.25F, "The level must cover the newest captured audio");
    kb::tests::Require(AudioCapture::Level(scene, 0.0F).rms == 0.0F && AudioCapture::Level(scene, 31.0F).rms == 0.0F, "A level window outside the history must read nothing");

    const kb::audio::AudioCaptureClip clip = AudioCapture::RecordToClip(scene, 0.25F);
    kb::tests::Require(clip.Succeeded() && clip.frames == kRate / 4U && clip.sampleRate == kRate && clip.clip->encodedBytes.size() == 44U + (kRate / 4U) * sizeof(float),
        "A clip of a quarter second must hold that many frames at the capture rate");
    const kb::audio::AudioCaptureClip shorter = AudioCapture::RecordToClip(scene, 2.0F);
    kb::tests::Require(shorter.Succeeded() && shorter.frames == kRate / 2U, "A capture shorter than the request must give the shorter clip");
    kb::tests::Require(AudioCapture::RecordToClip(scene, 0.0F).status == AudioCaptureStatus::InvalidSettings
            && AudioCapture::RecordToClip(scene, kb::audio::kAudioCaptureHistorySeconds + 1.0F).status == AudioCaptureStatus::InvalidSettings
            && AudioCapture::RecordToClip(scene, std::nanf("")).status == AudioCaptureStatus::InvalidSettings,
        "A clip length outside the history must be refused");

    const std::uint64_t clipId = AudioCapture::PublishClip(scene, clip);
    const kb::assets::AssetMetadata* metadata = clipId == 0U ? nullptr : scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ clipId });
    kb::tests::Require(metadata != nullptr && metadata->type == "AudioClip", "A published clip must be a registered audio clip asset");
    const auto loaded = scene.Assets().Manager().Load<kb::audio::AudioClipAsset>(kb::assets::AssetId{ clipId });
    kb::tests::Require(loaded.IsLoaded() && loaded->encodedBytes == clip.clip->encodedBytes, "A published clip must load with the recorded bytes");
    for (int round = 0; round < 6; ++round) {
        kb::tests::Require(AudioCapture::PublishClip(scene, clip) != 0U, "Repeated recordings must keep publishing");
    }
    std::size_t microphoneAssets = 0U;
    for (const kb::assets::AssetMetadata& asset : scene.Assets().Manager().Registry().All()) {
        if (asset.virtualPath.generic_string().starts_with("/Runtime/Audio/Microphone")) ++microphoneAssets;
    }
    kb::tests::Require(microphoneAssets > 0U && microphoneAssets <= 4U, "Repeated recordings must reuse a few clip slots");
    kb::tests::Require(AudioCapture::PublishClip(scene, kb::audio::AudioCaptureClip{}) == 0U, "A failed recording must not publish");

    AudioCapture::Stop(scene);
    kb::tests::Require(!AudioCapture::IsCapturing(scene) && AudioCapture::Level(scene).rms == 0.0F && AudioCapture::RecordToClip(scene, 0.1F).Succeeded(),
        "A stopped capture has no level but keeps what it captured");

    // The platform permission gates the start before the device is touched.
    const int callsBefore = backend.startCalls;
    kb::audio::SetAudioCapturePermissionProvider([]() noexcept { return kb::audio::AudioCapturePermission::Pending; });
    kb::tests::Require(AudioCapture::Start(scene) == AudioCaptureStatus::PermissionPending && backend.startCalls == callsBefore, "A pending permission must not start the device");
    kb::audio::SetAudioCapturePermissionProvider([]() noexcept { return kb::audio::AudioCapturePermission::Denied; });
    kb::tests::Require(AudioCapture::Start(scene) == AudioCaptureStatus::PermissionDenied && backend.startCalls == callsBefore, "A denied permission must not start the device");
    kb::audio::SetAudioCapturePermissionProvider([]() noexcept { return kb::audio::AudioCapturePermission::Granted; });
    kb::tests::Require(AudioCapture::Start(scene) == AudioCaptureStatus::Success, "A granted permission must let the device start");
    kb::audio::SetAudioCapturePermissionProvider(nullptr);
    AudioCapture::Stop(scene);
    kb::audio::AudioPlayback::UnregisterBackend(scene, backend);
}

void RunScriptApiTest() {
    kb::scene::Scene scene;
    FakeCaptureBackend backend;
    kb::audio::AudioPlayback::RegisterBackend(scene, backend);
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Microphone script API host did not initialize");
    for (const char* name : { "Microphone.Devices", "Microphone.Device", "Microphone.Start", "Microphone.Stop", "Microphone.IsRecording",
             "Microphone.Level", "Microphone.Peak", "Microphone.RecordToClip" }) {
        kb::tests::Require(host.Functions().FindSignature(name) != nullptr, "The Microphone script API did not register a function");
    }
    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };
    const auto call = [&](const char* function, std::vector<kb::script::ScriptFunctionArgument> arguments = {}) {
        return host.Functions().Call(function, std::span<const kb::script::ScriptFunctionArgument>{ arguments }, context);
    };
    using kb::script::ScriptFunctionArgument;
    using kb::script::ScriptValue;

    const auto devices = call("Microphone.Devices");
    kb::tests::Require(devices.Succeeded() && devices.Output("count")->AsInt() == 2 && devices.Output("defaultIndex")->AsInt() == 0, "Microphone.Devices must count the devices and find the default");
    const auto second = call("Microphone.Device", { ScriptFunctionArgument{ "index", ScriptValue{ 1 } } });
    kb::tests::Require(second.Succeeded() && second.Output("found")->AsBool() && second.Output("name")->AsString() == "Headset" && !second.Output("isDefault")->AsBool(),
        "Microphone.Device must name a device");
    kb::tests::Require(!call("Microphone.Device", { ScriptFunctionArgument{ "index", ScriptValue{ 5 } } }).Output("found")->AsBool(), "Microphone.Device must report a missing index");

    kb::tests::Require(!call("Microphone.IsRecording").Output("recording")->AsBool(), "The microphone must start stopped");
    const auto refused = call("Microphone.Start", { ScriptFunctionArgument{ "device", ScriptValue{ std::string{ "Nope" } } } });
    kb::tests::Require(refused.executed && !refused.Succeeded() && refused.errors.front().find("not found") != std::string::npos, "Microphone.Start must answer a missing device with the reason");
    kb::tests::Require(call("Microphone.Start").Succeeded() && call("Microphone.IsRecording").Output("recording")->AsBool(), "Microphone.Start must start the default device");
    backend.Feed(0.5F, kRate);
    kb::tests::Require(std::fabs(call("Microphone.Level").Output("level")->AsFloat() - 0.5F) < 1.0e-5F && call("Microphone.Peak", { ScriptFunctionArgument{ "window", ScriptValue{ 0.1F } } }).Output("peak")->AsFloat() == 0.5F,
        "Microphone.Level and Peak must read the capture");
    kb::tests::Require(!call("Microphone.Level", { ScriptFunctionArgument{ "window", ScriptValue{ -1.0F } } }).executed, "Microphone.Level must reject a window outside the history");

    const auto recorded = call("Microphone.RecordToClip", { ScriptFunctionArgument{ "seconds", ScriptValue{ 0.5F } } });
    kb::tests::Require(recorded.Succeeded() && std::fabs(recorded.Output("seconds")->AsFloat() - 0.5F) < 1.0e-4F && recorded.Output("sampleRate")->AsInt() == static_cast<int>(kRate),
        "Microphone.RecordToClip must report the clip length and rate");
    kb::assets::AssetId clipId{};
    kb::tests::Require(kb::assets::TryParseAssetId(recorded.Output("clip")->AsString(), clipId) && scene.Assets().Manager().Registry().Find(clipId) != nullptr,
        "Microphone.RecordToClip must return an asset id Audio.Play can use");
    kb::tests::Require(!call("Microphone.RecordToClip", { ScriptFunctionArgument{ "seconds", ScriptValue{ 0.0F } } }).executed, "Microphone.RecordToClip must reject a zero length");
    kb::tests::Require(call("Microphone.Stop").Output("stopped")->AsBool() && !call("Microphone.Stop").Output("stopped")->AsBool(), "Microphone.Stop must say whether it stopped a capture");
    kb::audio::AudioPlayback::UnregisterBackend(scene, backend);
}

} // namespace

namespace kb::tests {

void RunAudioCaptureTests() {
    RunRingBufferTest();
    RunLevelTest();
    RunClipTest();
    RunFacadeWithoutBackendTest();
    RunFacadeTest();
    RunScriptApiTest();
}

} // namespace kb::tests
