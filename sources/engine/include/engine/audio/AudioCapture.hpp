#pragma once

#include "engine/audio/AudioClipAsset.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::audio {

// Capture format: mono 32-bit float at 48 kHz. A device with another native format or rate is converted
// by the backend, so every reader sees the same stream.
inline constexpr std::uint32_t kAudioCaptureSampleRate = 48000U;
// How much recent audio a capture keeps for level readings and clips, in seconds.
inline constexpr float kAudioCaptureHistorySeconds = 30.0F;
// The span of recent audio a level reading covers unless the caller asks for another.
inline constexpr float kAudioCaptureLevelWindowSeconds = 0.05F;

enum class AudioCaptureStatus : std::uint8_t {
    Success,
    BackendUnavailable,
    Unsupported,
    DeviceNotFound,
    PermissionDenied,
    PermissionPending,
    AlreadyCapturing,
    DeviceInitializationFailed,
    InvalidSettings,
    NoAudio,
    ClipRegistrationFailed,
};

[[nodiscard]] std::string_view ToString(AudioCaptureStatus status) noexcept;

enum class AudioCapturePermission : std::uint8_t {
    Granted,
    // The platform is asking the user; starting again after the answer succeeds or is denied.
    Pending,
    Denied,
};

// The platform's answer to "may this process capture audio". A platform that asks the user (a phone) installs
// a provider that starts the request on its first call and keeps answering Pending until the user decides; with
// no provider every capture is permitted, as on a desktop. Installed once at startup, read on the owner thread.
using AudioCapturePermissionProvider = AudioCapturePermission (*)() noexcept;
void SetAudioCapturePermissionProvider(AudioCapturePermissionProvider provider) noexcept;

struct AudioCaptureDeviceInfo {
    std::string name;
    bool isDefault = false;
};

struct AudioCaptureDesc {
    // Exact device name from AudioCapture::Devices; empty selects the default input device.
    std::string deviceName;
};

struct AudioCaptureLevel {
    float rms = 0.0F;
    float peak = 0.0F;
};

// Root mean square and absolute peak of the samples. Non-finite samples count as silence.
[[nodiscard]] AudioCaptureLevel ComputeAudioCaptureLevel(std::span<const float> samples) noexcept;

// 32-bit float WAV held in memory, samples interleaved by channel: the clip plays through the ordinary clip path.
[[nodiscard]] std::shared_ptr<AudioClipAsset> MakeAudioClipFromPcm(std::span<const float> samples, std::uint32_t sampleRate,
    std::uint16_t channels = 1U);

// Recent captured samples shared between the device callback thread (one writer) and the scene owner
// thread (readers). Writing never blocks or allocates. A reader gets the newest samples, oldest first, and
// never a sample the writer was overwriting while it was copied.
class AudioCaptureRingBuffer final {
public:
    explicit AudioCaptureRingBuffer(std::size_t capacityFrames);

    void Write(std::span<const float> samples) noexcept;
    [[nodiscard]] std::size_t ReadRecent(std::span<float> out) const noexcept;
    [[nodiscard]] std::uint64_t WrittenFrames() const noexcept { return published_.load(std::memory_order_acquire); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }

private:
    std::size_t capacity_;
    std::unique_ptr<std::atomic<float>[]> data_;
    // Frames the writer has started to store; ahead of published_ only while a block is being written.
    std::atomic<std::uint64_t> reserved_{ 0U };
    // Frames fully stored.
    std::atomic<std::uint64_t> published_{ 0U };
};

// Optional capability of the registered audio backend, separate from the playback vtable. Every entry
// has the same scene owner-thread requirement as playback; the device callback never enters it.
class IAudioCaptureBackend {
public:
    virtual ~IAudioCaptureBackend() = default;

    [[nodiscard]] virtual std::vector<AudioCaptureDeviceInfo> CaptureDevices(kb::scene::Scene& scene) = 0;
    [[nodiscard]] virtual AudioCaptureStatus StartCapture(kb::scene::Scene& scene, const AudioCaptureDesc& desc) = 0;
    // Stops the device; what was captured stays readable until the next start.
    virtual void StopCapture(kb::scene::Scene& scene) noexcept = 0;
    [[nodiscard]] virtual bool IsCapturing(kb::scene::Scene& scene) noexcept = 0;
    // Copies up to out.size() of the newest captured frames, oldest first, and returns how many.
    [[nodiscard]] virtual std::size_t ReadCaptured(kb::scene::Scene& scene, std::span<float> out) noexcept = 0;
};

struct AudioCaptureClip {
    AudioCaptureStatus status = AudioCaptureStatus::BackendUnavailable;
    std::shared_ptr<AudioClipAsset> clip;
    std::uint32_t sampleRate = kAudioCaptureSampleRate;
    std::uint64_t frames = 0U;

    [[nodiscard]] bool Succeeded() const noexcept { return status == AudioCaptureStatus::Success && clip != nullptr; }
};

class AudioCapture final {
public:
    AudioCapture() = delete;

    [[nodiscard]] static bool HasBackend(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static std::vector<AudioCaptureDeviceInfo> Devices(kb::scene::Scene& scene);
    [[nodiscard]] static AudioCaptureStatus Start(kb::scene::Scene& scene, const AudioCaptureDesc& desc = {});
    static void Stop(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static bool IsCapturing(kb::scene::Scene& scene) noexcept;
    // Level of the newest `windowSeconds` of the capture (at most the history); silence while not capturing.
    [[nodiscard]] static AudioCaptureLevel Level(kb::scene::Scene& scene, float windowSeconds = kAudioCaptureLevelWindowSeconds);
    // Copies the newest `seconds` of the capture (at most the history) into an in-memory clip. A capture
    // that holds less than that yields the shorter clip; one that holds nothing yields NoAudio.
    [[nodiscard]] static AudioCaptureClip RecordToClip(kb::scene::Scene& scene, float seconds);
    // Registers the clip as a scene audio asset (a short rotating set of slots, so repeated recordings do
    // not grow the asset registry) and returns its id, which Audio.Play and AudioPlayDesc accept; 0 on failure.
    [[nodiscard]] static std::uint64_t PublishClip(kb::scene::Scene& scene, const AudioCaptureClip& clip);
};

} // namespace kb::audio
