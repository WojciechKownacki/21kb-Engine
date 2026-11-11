#include "engine/audio/AudioCapture.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <utility>
#if !defined(NDEBUG)
#include <thread>
#endif

namespace kb::audio {
namespace {

// Published recordings rotate through this many asset slots.
constexpr std::uint32_t kPublishedClipSlots = 4U;
constexpr std::size_t kWavHeaderBytes = 44U;
constexpr std::uint16_t kWavFormatFloat = 3U;

std::atomic<AudioCapturePermissionProvider> g_permissionProvider{ nullptr };

IAudioCaptureBackend* CaptureBackend(kb::scene::Scene& scene) noexcept {
    auto& state = kb::scene::SceneAccess::State(scene);
#if !defined(NDEBUG)
    assert(state.audioPlaybackOwnerThread == std::this_thread::get_id() && "audio capture requires the scene owner thread");
#endif
    return dynamic_cast<IAudioCaptureBackend*>(state.audioPlaybackBackend);
}

[[nodiscard]] std::size_t FramesForSeconds(float seconds) noexcept {
    return static_cast<std::size_t>(std::llround(static_cast<double>(seconds) * kAudioCaptureSampleRate));
}

[[nodiscard]] bool IsSecondsInHistory(float seconds) noexcept {
    return std::isfinite(seconds) && seconds > 0.0F && seconds <= kAudioCaptureHistorySeconds;
}

void PutUInt16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void PutUInt32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) noexcept {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (8U * index)) & 0xFFU);
    }
}

[[nodiscard]] std::uint64_t HashBytes(const std::vector<std::uint8_t>& bytes) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : bytes) {
        hash = (hash ^ byte) * 1099511628211ULL;
    }
    return hash;
}

} // namespace

std::string_view ToString(AudioCaptureStatus status) noexcept {
    switch (status) {
    case AudioCaptureStatus::Success: return "microphone capture succeeded";
    case AudioCaptureStatus::BackendUnavailable: return "microphone capture requires an active audio backend";
    case AudioCaptureStatus::Unsupported: return "active audio backend does not support microphone capture";
    case AudioCaptureStatus::DeviceNotFound: return "microphone device was not found";
    case AudioCaptureStatus::PermissionDenied: return "microphone permission was not granted";
    case AudioCaptureStatus::PermissionPending: return "microphone permission is being requested; start again after the answer";
    case AudioCaptureStatus::AlreadyCapturing: return "microphone is already capturing";
    case AudioCaptureStatus::DeviceInitializationFailed: return "microphone device could not be started";
    case AudioCaptureStatus::InvalidSettings: return "microphone request has an invalid setting";
    case AudioCaptureStatus::NoAudio: return "microphone has captured no audio yet";
    case AudioCaptureStatus::ClipRegistrationFailed: return "microphone clip could not be registered as an audio asset";
    }
    return "microphone capture failed";
}

void SetAudioCapturePermissionProvider(AudioCapturePermissionProvider provider) noexcept {
    g_permissionProvider.store(provider, std::memory_order_release);
}

AudioCaptureLevel ComputeAudioCaptureLevel(std::span<const float> samples) noexcept {
    if (samples.empty()) {
        return {};
    }
    double energy = 0.0;
    float peak = 0.0F;
    for (const float sample : samples) {
        if (!std::isfinite(sample)) {
            continue;
        }
        energy += static_cast<double>(sample) * static_cast<double>(sample);
        peak = std::max(peak, std::fabs(sample));
    }
    return { .rms = static_cast<float>(std::sqrt(energy / static_cast<double>(samples.size()))), .peak = peak };
}

std::shared_ptr<AudioClipAsset> MakeAudioClipFromPcm(std::span<const float> samples, std::uint32_t sampleRate, std::uint16_t channels) {
    if (samples.empty() || sampleRate == 0U || channels == 0U || samples.size() % channels != 0U ||
        samples.size() > (0xFFFFFFFFU - kWavHeaderBytes) / sizeof(float)) {
        return nullptr;
    }
    auto clip = std::make_shared<AudioClipAsset>();
    clip->sourceExtension = ".wav";
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));
    std::vector<std::uint8_t>& bytes = clip->encodedBytes;
    bytes.resize(kWavHeaderBytes + dataBytes);
    std::memcpy(bytes.data(), "RIFF", 4U);
    PutUInt32(bytes, 4U, static_cast<std::uint32_t>(kWavHeaderBytes - 8U) + dataBytes);
    std::memcpy(bytes.data() + 8U, "WAVEfmt ", 8U);
    PutUInt32(bytes, 16U, 16U);
    PutUInt16(bytes, 20U, kWavFormatFloat);
    PutUInt16(bytes, 22U, channels);
    PutUInt32(bytes, 24U, sampleRate);
    PutUInt32(bytes, 28U, sampleRate * channels * static_cast<std::uint32_t>(sizeof(float)));
    PutUInt16(bytes, 32U, static_cast<std::uint16_t>(channels * sizeof(float)));
    PutUInt16(bytes, 34U, 32U);
    std::memcpy(bytes.data() + 36U, "data", 4U);
    PutUInt32(bytes, 40U, dataBytes);
    std::memcpy(bytes.data() + kWavHeaderBytes, samples.data(), dataBytes);
    return clip;
}

AudioCaptureRingBuffer::AudioCaptureRingBuffer(std::size_t capacityFrames)
    : capacity_(std::max<std::size_t>(capacityFrames, 1U)),
      data_(std::make_unique<std::atomic<float>[]>(capacity_)) {}

void AudioCaptureRingBuffer::Write(std::span<const float> samples) noexcept {
    // Only the writer changes published_, so a relaxed read sees its own last store.
    const std::uint64_t first = published_.load(std::memory_order_relaxed);
    std::uint64_t skipped = 0U;
    if (samples.size() > capacity_) {
        // A block longer than the history would overwrite itself; only its newest part can be read back.
        skipped = samples.size() - capacity_;
        samples = samples.subspan(static_cast<std::size_t>(skipped));
    }
    const std::uint64_t begin = first + skipped;
    const std::uint64_t end = begin + samples.size();
    reserved_.store(end, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    std::size_t position = static_cast<std::size_t>(begin % capacity_);
    for (const float sample : samples) {
        data_[position].store(sample, std::memory_order_relaxed);
        if (++position == capacity_) {
            position = 0U;
        }
    }
    published_.store(end, std::memory_order_release);
}

std::size_t AudioCaptureRingBuffer::ReadRecent(std::span<float> out) const noexcept {
    const std::uint64_t end = published_.load(std::memory_order_acquire);
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>({ out.size(), end, capacity_ }));
    const std::uint64_t first = end - count;
    std::size_t position = static_cast<std::size_t>(first % capacity_);
    for (std::size_t index = 0U; index < count; ++index) {
        out[index] = data_[position].load(std::memory_order_relaxed);
        if (++position == capacity_) {
            position = 0U;
        }
    }
    // The writer may have started to overwrite the oldest samples while they were copied.
    std::atomic_thread_fence(std::memory_order_acquire);
    const std::uint64_t reserved = reserved_.load(std::memory_order_relaxed);
    const std::uint64_t readableFrom = reserved > capacity_ ? reserved - capacity_ : 0U;
    if (readableFrom <= first) {
        return count;
    }
    const auto lost = static_cast<std::size_t>(std::min<std::uint64_t>(readableFrom - first, count));
    std::copy(out.begin() + static_cast<std::ptrdiff_t>(lost), out.begin() + static_cast<std::ptrdiff_t>(count), out.begin());
    return count - lost;
}

bool AudioCapture::HasBackend(kb::scene::Scene& scene) noexcept {
    return CaptureBackend(scene) != nullptr;
}

std::vector<AudioCaptureDeviceInfo> AudioCapture::Devices(kb::scene::Scene& scene) {
    IAudioCaptureBackend* backend = CaptureBackend(scene);
    return backend == nullptr ? std::vector<AudioCaptureDeviceInfo>{} : backend->CaptureDevices(scene);
}

AudioCaptureStatus AudioCapture::Start(kb::scene::Scene& scene, const AudioCaptureDesc& desc) {
    IAudioCaptureBackend* backend = CaptureBackend(scene);
    if (backend == nullptr) {
        return AudioPlayback::HasBackend(scene) ? AudioCaptureStatus::Unsupported : AudioCaptureStatus::BackendUnavailable;
    }
    if (const AudioCapturePermissionProvider provider = g_permissionProvider.load(std::memory_order_acquire)) {
        switch (provider()) {
        case AudioCapturePermission::Granted: break;
        case AudioCapturePermission::Pending: return AudioCaptureStatus::PermissionPending;
        case AudioCapturePermission::Denied: return AudioCaptureStatus::PermissionDenied;
        }
    }
    return backend->StartCapture(scene, desc);
}

void AudioCapture::Stop(kb::scene::Scene& scene) noexcept {
    if (IAudioCaptureBackend* backend = CaptureBackend(scene)) {
        backend->StopCapture(scene);
    }
}

bool AudioCapture::IsCapturing(kb::scene::Scene& scene) noexcept {
    IAudioCaptureBackend* backend = CaptureBackend(scene);
    return backend != nullptr && backend->IsCapturing(scene);
}

AudioCaptureLevel AudioCapture::Level(kb::scene::Scene& scene, float windowSeconds) {
    IAudioCaptureBackend* backend = CaptureBackend(scene);
    if (backend == nullptr || !IsSecondsInHistory(windowSeconds) || !backend->IsCapturing(scene)) {
        return {};
    }
    std::vector<float> window(std::max<std::size_t>(FramesForSeconds(windowSeconds), 1U));
    window.resize(backend->ReadCaptured(scene, window));
    return ComputeAudioCaptureLevel(window);
}

AudioCaptureClip AudioCapture::RecordToClip(kb::scene::Scene& scene, float seconds) {
    IAudioCaptureBackend* backend = CaptureBackend(scene);
    if (backend == nullptr) {
        return { .status = AudioPlayback::HasBackend(scene) ? AudioCaptureStatus::Unsupported : AudioCaptureStatus::BackendUnavailable };
    }
    if (!IsSecondsInHistory(seconds)) {
        return { .status = AudioCaptureStatus::InvalidSettings };
    }
    std::vector<float> samples(FramesForSeconds(seconds));
    samples.resize(backend->ReadCaptured(scene, samples));
    if (samples.empty()) {
        return { .status = AudioCaptureStatus::NoAudio };
    }
    return {
        .status = AudioCaptureStatus::Success,
        .clip = MakeAudioClipFromPcm(samples, kAudioCaptureSampleRate),
        .sampleRate = kAudioCaptureSampleRate,
        .frames = samples.size(),
    };
}

std::uint64_t AudioCapture::PublishClip(kb::scene::Scene& scene, const AudioCaptureClip& clip) {
    if (!clip.Succeeded()) {
        return 0U;
    }
    static std::atomic<std::uint32_t> nextSlot{ 0U };
    const std::string slot = std::to_string(nextSlot.fetch_add(1U, std::memory_order_relaxed) % kPublishedClipSlots);
    kb::assets::AssetManager& assets = scene.Assets().Manager();
    const kb::assets::AssetId id = kb::assets::MakeAssetId("runtime-audio/microphone-" + slot + ".wav");
    kb::assets::AssetMetadata metadata;
    metadata.id = id;
    metadata.type = "AudioClip";
    metadata.virtualPath = "/Runtime/Audio/Microphone" + slot + ".wav";
    metadata.contentHash = HashBytes(clip.clip->encodedBytes);
    if (!assets.RegisterGeneratedAsset(std::move(metadata)) || !assets.PublishRuntimeAsset(id, clip.clip)) {
        return 0U;
    }
    return id.value;
}

} // namespace kb::audio
