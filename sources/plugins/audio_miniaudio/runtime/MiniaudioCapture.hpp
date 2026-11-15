#pragma once

#include "engine/audio/AudioCapture.hpp"

#include <miniaudio.h>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace kb::audio_miniaudio {

// One input device capturing mono 32-bit float at the engine capture rate into a ring buffer. miniaudio
// converts the channel layout and sample rate of the native device, the same conversion playback uses.
class MiniaudioCapture final {
public:
    MiniaudioCapture() = default;
    ~MiniaudioCapture();

    MiniaudioCapture(const MiniaudioCapture&) = delete;
    MiniaudioCapture& operator=(const MiniaudioCapture&) = delete;
    MiniaudioCapture(MiniaudioCapture&&) = delete;
    MiniaudioCapture& operator=(MiniaudioCapture&&) = delete;

    // An externally owned context, when supplied, outlives the capture. Without one the capture opens its
    // own context with the platform's backends the first time it needs one.
    void UseContext(ma_context* context) noexcept;
    [[nodiscard]] std::vector<kb::audio::AudioCaptureDeviceInfo> Devices();
    [[nodiscard]] kb::audio::AudioCaptureStatus Start(const kb::audio::AudioCaptureDesc& desc);
    void Stop() noexcept;
    [[nodiscard]] bool IsCapturing() const noexcept { return deviceReady_; }
    [[nodiscard]] std::size_t ReadRecent(std::span<float> out) const noexcept;
    [[nodiscard]] std::uint64_t CapturedFrames() const noexcept;

private:
    static void OnData(ma_device* device, void* output, const void* input, ma_uint32 frames);
    [[nodiscard]] bool EnsureContext();

    ma_context* context_ = nullptr;
    ma_context ownContext_{};
    bool ownsContext_ = false;
    ma_device device_{};
    bool deviceReady_ = false;
    std::unique_ptr<kb::audio::AudioCaptureRingBuffer> ring_;
};

} // namespace kb::audio_miniaudio
