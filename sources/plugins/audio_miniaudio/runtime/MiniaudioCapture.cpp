#include "runtime/MiniaudioCapture.hpp"

#include <string>
#include <utility>

namespace kb::audio_miniaudio {

MiniaudioCapture::~MiniaudioCapture() {
    Stop();
    if (ownsContext_) {
        ma_context_uninit(&ownContext_);
    }
}

void MiniaudioCapture::UseContext(ma_context* context) noexcept {
    Stop();
    if (ownsContext_) {
        ma_context_uninit(&ownContext_);
        ownsContext_ = false;
    }
    context_ = context;
}

bool MiniaudioCapture::EnsureContext() {
    if (context_ != nullptr) {
        return true;
    }
    if (ma_context_init(nullptr, 0U, nullptr, &ownContext_) != MA_SUCCESS) {
        return false;
    }
    ownsContext_ = true;
    context_ = &ownContext_;
    return true;
}

std::vector<kb::audio::AudioCaptureDeviceInfo> MiniaudioCapture::Devices() {
    std::vector<kb::audio::AudioCaptureDeviceInfo> devices;
    ma_device_info* playback = nullptr;
    ma_device_info* capture = nullptr;
    ma_uint32 playbackCount = 0U;
    ma_uint32 captureCount = 0U;
    if (!EnsureContext() || ma_context_get_devices(context_, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS) {
        return devices;
    }
    devices.reserve(captureCount);
    for (ma_uint32 index = 0U; index < captureCount; ++index) {
        devices.push_back({ .name = capture[index].name, .isDefault = capture[index].isDefault != 0U });
    }
    return devices;
}

kb::audio::AudioCaptureStatus MiniaudioCapture::Start(const kb::audio::AudioCaptureDesc& desc) {
    using kb::audio::AudioCaptureStatus;
    if (deviceReady_) {
        return AudioCaptureStatus::AlreadyCapturing;
    }
    if (!EnsureContext()) {
        return AudioCaptureStatus::BackendUnavailable;
    }
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1U;
    config.sampleRate = kb::audio::kAudioCaptureSampleRate;
    config.dataCallback = &MiniaudioCapture::OnData;
    config.pUserData = this;
    ma_device_id selected{};
    if (!desc.deviceName.empty()) {
        ma_device_info* playback = nullptr;
        ma_device_info* capture = nullptr;
        ma_uint32 playbackCount = 0U;
        ma_uint32 captureCount = 0U;
        if (ma_context_get_devices(context_, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS) {
            return AudioCaptureStatus::DeviceInitializationFailed;
        }
        bool found = false;
        for (ma_uint32 index = 0U; index < captureCount && !found; ++index) {
            if (desc.deviceName == capture[index].name) {
                selected = capture[index].id;
                found = true;
            }
        }
        if (!found) {
            return AudioCaptureStatus::DeviceNotFound;
        }
        config.capture.pDeviceID = &selected;
    }
    ring_ = std::make_unique<kb::audio::AudioCaptureRingBuffer>(
        static_cast<std::size_t>(kb::audio::kAudioCaptureHistorySeconds * static_cast<float>(kb::audio::kAudioCaptureSampleRate)));
    ma_result result = ma_device_init(context_, &config, &device_);
    if (result == MA_SUCCESS) {
        result = ma_device_start(&device_);
        if (result != MA_SUCCESS) {
            ma_device_uninit(&device_);
        }
    }
    if (result != MA_SUCCESS) {
        ring_.reset();
        return result == MA_ACCESS_DENIED ? AudioCaptureStatus::PermissionDenied : AudioCaptureStatus::DeviceInitializationFailed;
    }
    deviceReady_ = true;
    return AudioCaptureStatus::Success;
}

void MiniaudioCapture::Stop() noexcept {
    if (!deviceReady_) {
        return;
    }
    // Uninitializing waits for the data callback, so nothing writes to the ring afterwards.
    ma_device_uninit(&device_);
    deviceReady_ = false;
}

std::size_t MiniaudioCapture::ReadRecent(std::span<float> out) const noexcept {
    return ring_ == nullptr ? 0U : ring_->ReadRecent(out);
}

std::uint64_t MiniaudioCapture::CapturedFrames() const noexcept {
    return ring_ == nullptr ? 0U : ring_->WrittenFrames();
}

void MiniaudioCapture::OnData(ma_device* device, void*, const void* input, ma_uint32 frames) {
    auto* self = static_cast<MiniaudioCapture*>(device->pUserData);
    if (input != nullptr && self->ring_ != nullptr) {
        self->ring_->Write(std::span<const float>{ static_cast<const float*>(input), frames });
    }
}

} // namespace kb::audio_miniaudio
