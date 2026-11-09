#pragma once
#include <cstdint>
#include <string>
namespace kb::scene { class Scene; }
namespace kb::audio {
// Counts belong to one initialized audio engine generation. Allocation bytes cover
// requests routed through the engine allocator, excluding assets, STL and driver memory.
struct AudioRuntimeDiagnostics {
    bool supported = false, offline = false, playbackAvailable = false;
    std::string backend;
    std::uint64_t generation = 0U, deviceBlocks = 0U, deviceFrames = 0U;
    // Short reads of an attached graph; an empty endpoint produces valid idle silence.
    std::uint64_t shortBlocks = 0U, callbackDeadlineMisses = 0U, worstCallbackNanoseconds = 0U;
    std::uint64_t allocatedBytes = 0U, peakAllocatedBytes = 0U, allocationCount = 0U;
    std::uint32_t sampleRate = 0U, periodFrames = 0U;
};
class IAudioDiagnosticsBackend {
public:
    virtual ~IAudioDiagnosticsBackend() = default;
    [[nodiscard]] virtual AudioRuntimeDiagnostics RuntimeDiagnostics() const = 0;
};
class AudioDiagnostics {
public:
    // Optional capability; call on the scene owner thread. Callback deadline misses
    // are measured mix overruns, not a count of hardware/OS output underruns.
    [[nodiscard]] static AudioRuntimeDiagnostics Report(kb::scene::Scene& scene);
};
} // namespace kb::audio
