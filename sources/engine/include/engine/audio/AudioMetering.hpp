#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene { class Scene; }
namespace kb::audio {

struct AudioMeterChannel {
    float peak = 0.0F, rms = 0.0F, heldPeak = 0.0F;
    std::uint64_t clippedSamples = 0U, nonFiniteSamples = 0U;
};

struct AudioMeterReading {
    std::string bus; // Empty names the final master output.
    bool available = false; // False for an uninitialized or temporarily busy tap.
    std::uint32_t channels = 0U, blockFrames = 0U;
    std::uint64_t processedFrames = 0U;
    std::array<AudioMeterChannel, 8U> channel{};
};

struct AudioMeterReport {
    bool backendAvailable = false, supported = false, initialized = false, captureEnabled = false;
    std::uint64_t mixerAssetId = 0U;
    std::uint32_t sampleRate = 0U;
    AudioMeterReading master;
    std::vector<AudioMeterReading> buses;
};

// Optional capability, separate from the existing playback backend vtable.
// All entry points have the same scene owner-thread requirement as playback.
class IAudioMeteringBackend {
public:
    virtual ~IAudioMeteringBackend() = default;
    [[nodiscard]] virtual bool ConfigureMeterCapture(kb::scene::Scene&, bool enabled) = 0;
    [[nodiscard]] virtual bool ResetMeterCapture(kb::scene::Scene&) = 0;
    [[nodiscard]] virtual AudioMeterReport MeterReport(kb::scene::Scene&) = 0;
};

class AudioMetering {
public:
    // Configure/reset clear meter history without restarting voices or DSP.
    [[nodiscard]] static bool Configure(kb::scene::Scene& scene, bool enabled);
    [[nodiscard]] static bool Reset(kb::scene::Scene& scene);
    // Observation never enables capture, advances audio or installs pending edits.
    [[nodiscard]] static AudioMeterReport Report(kb::scene::Scene& scene);
};

} // namespace kb::audio
