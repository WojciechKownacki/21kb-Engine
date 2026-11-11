#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace kb::scene {
enum class YuvPlaneLayout : std::uint8_t { Nv12, I420 };
enum class YuvMatrix : std::uint8_t { Bt601, Bt709 };

// One decoded 4:2:0 picture as a codec hands it over: rows padded to `stride` bytes, the luma plane padded to
// `sliceHeight` rows, and a visible window of `width` x `height` pixels at (`cropLeft`, `cropTop`).
struct YuvPicture {
    YuvPlaneLayout layout = YuvPlaneLayout::Nv12;
    YuvMatrix matrix = YuvMatrix::Bt709;
    bool fullRange = false;
    std::uint32_t width = 0, height = 0;
    std::uint32_t cropLeft = 0, cropTop = 0;
    std::uint32_t stride = 0, sliceHeight = 0;
};

// Writes the visible window as top-down RGBA8 with opaque alpha into `rgba` (width * height * 4 bytes).
// Returns false when the description is inconsistent or `bytes` is too short for it.
[[nodiscard]] bool ConvertYuvToRgba(const YuvPicture& picture, std::span<const std::uint8_t> bytes, std::uint8_t* rgba) noexcept;

enum class PcmEncoding : std::uint8_t { Int16, Float32 };

// Appends interleaved PCM of 1, 2 or 6 channels (L R C LFE Ls Rs) as stereo float frames.
// Returns false for another channel count or a byte count that is not a whole number of frames.
[[nodiscard]] bool AppendPcmAsStereo(PcmEncoding encoding, std::uint32_t channels, std::span<const std::uint8_t> bytes,
    std::vector<float>& stereo);

// Resamples interleaved stereo with linear interpolation to exactly `targetFrames` frames. Frames past the end
// of the source are silent.
[[nodiscard]] std::vector<float> ResampleStereoLinear(std::span<const float> stereo, std::uint32_t sourceRate,
    std::uint32_t targetRate, std::size_t targetFrames);
}
