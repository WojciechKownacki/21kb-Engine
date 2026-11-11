#include "scene/VideoMediaConversion.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace kb::scene {
namespace {
// 16.16 fixed-point factors of the YCbCr to RGB matrix.
struct Coefficients {
    std::int32_t luma, red, blueGreen, redGreen, blue;
    std::int32_t lumaOffset;
};
constexpr std::int32_t Fixed(double value) {
    return value >= 0 ? static_cast<std::int32_t>(value * 65536.0 + 0.5) : -static_cast<std::int32_t>(-value * 65536.0 + 0.5);
}
constexpr Coefficients MakeCoefficients(double kr, double kb, bool fullRange) {
    const double kg = 1.0 - kr - kb;
    const double yScale = fullRange ? 1.0 : 255.0 / 219.0;
    const double cScale = fullRange ? 1.0 : 255.0 / 224.0;
    return {Fixed(yScale), Fixed(cScale * 2.0 * (1.0 - kr)), Fixed(cScale * 2.0 * (1.0 - kb) * kb / kg),
        Fixed(cScale * 2.0 * (1.0 - kr) * kr / kg), Fixed(cScale * 2.0 * (1.0 - kb)), fullRange ? 0 : 16};
}
constexpr Coefficients kCoefficients[2][2] = {
    {MakeCoefficients(0.299, 0.114, false), MakeCoefficients(0.299, 0.114, true)},
    {MakeCoefficients(0.2126, 0.0722, false), MakeCoefficients(0.2126, 0.0722, true)}};
std::uint8_t Clamp8(std::int32_t value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}
}
bool ConvertYuvToRgba(const YuvPicture& p, std::span<const std::uint8_t> bytes, std::uint8_t* rgba) noexcept {
    if (rgba == nullptr || p.width == 0 || p.height == 0 || p.stride == 0) return false;
    const std::uint64_t right = static_cast<std::uint64_t>(p.cropLeft) + p.width, bottom = static_cast<std::uint64_t>(p.cropTop) + p.height;
    if (right > p.stride || bottom > p.sliceHeight) return false;
    const std::uint64_t stride = p.stride, lumaBytes = stride * p.sliceHeight;
    const bool semiPlanar = p.layout == YuvPlaneLayout::Nv12;
    const std::uint64_t chromaStride = semiPlanar ? stride : (stride + 1U) / 2U;
    const std::uint64_t chromaRows = (static_cast<std::uint64_t>(p.sliceHeight) + 1U) / 2U;
    const std::uint64_t lastChromaRow = (bottom - 1U) >> 1U, lastColumn = (right - 1U) >> 1U;
    // Last byte read: the final luma pixel, and the final chroma sample(s) of the window.
    std::uint64_t needed = (bottom - 1U) * stride + right;
    if (semiPlanar) needed = std::max(needed, lumaBytes + lastChromaRow * chromaStride + lastColumn * 2U + 2U);
    else needed = std::max(needed, lumaBytes + chromaStride * chromaRows + lastChromaRow * chromaStride + lastColumn + 1U);
    if (bytes.size() < needed) return false;
    const std::uint8_t* luma = bytes.data();
    const std::uint8_t* chromaU = bytes.data() + lumaBytes;
    const std::uint8_t* chromaV = semiPlanar ? chromaU + 1 : chromaU + chromaStride * chromaRows;
    const std::size_t pixelStep = semiPlanar ? 2U : 1U;
    const Coefficients& k = kCoefficients[p.matrix == YuvMatrix::Bt709 ? 1 : 0][p.fullRange ? 1 : 0];
    for (std::uint32_t y = 0; y < p.height; ++y) {
        const std::uint64_t row = static_cast<std::uint64_t>(p.cropTop) + y;
        const std::uint8_t* yRow = luma + row * stride + p.cropLeft;
        const std::uint8_t* uRow = chromaU + (row >> 1U) * chromaStride;
        const std::uint8_t* vRow = chromaV + (row >> 1U) * chromaStride;
        std::uint8_t* out = rgba + static_cast<std::size_t>(y) * p.width * 4U;
        for (std::uint32_t x = 0; x < p.width; ++x, out += 4) {
            const std::size_t column = ((static_cast<std::size_t>(p.cropLeft) + x) >> 1U) * pixelStep;
            const std::int32_t luminance = (static_cast<std::int32_t>(yRow[x]) - k.lumaOffset) * k.luma;
            const std::int32_t cb = static_cast<std::int32_t>(uRow[column]) - 128, cr = static_cast<std::int32_t>(vRow[column]) - 128;
            out[0] = Clamp8((luminance + k.red * cr + 32768) >> 16);
            out[1] = Clamp8((luminance - k.blueGreen * cb - k.redGreen * cr + 32768) >> 16);
            out[2] = Clamp8((luminance + k.blue * cb + 32768) >> 16);
            out[3] = 255;
        }
    }
    return true;
}
bool AppendPcmAsStereo(PcmEncoding encoding, std::uint32_t channels, std::span<const std::uint8_t> bytes, std::vector<float>& stereo) {
    if (channels != 1 && channels != 2 && channels != 6) return false;
    const std::size_t sampleSize = encoding == PcmEncoding::Int16 ? 2U : 4U, frameSize = sampleSize * channels;
    if (bytes.size() % frameSize != 0) return false;
    const auto sample = [&](const std::uint8_t* at) {
        if (encoding == PcmEncoding::Float32) {
            float value = 0.0F;
            std::memcpy(&value, at, 4);
            return value;
        }
        std::int16_t value = 0;
        std::memcpy(&value, at, 2);
        return static_cast<float>(value) / 32768.0F;
    };
    const std::size_t frames = bytes.size() / frameSize, first = stereo.size();
    stereo.resize(first + frames * 2U);
    constexpr float kHalfPower = 0.70710678F;
    for (std::size_t i = 0; i < frames; ++i) {
        const std::uint8_t* at = bytes.data() + i * frameSize;
        float left, right;
        if (channels == 1) {
            left = right = sample(at);
        } else if (channels == 2) {
            left = sample(at);
            right = sample(at + sampleSize);
        } else {
            // L R C LFE Ls Rs: the centre and the surrounds join their side at -3 dB; the LFE is dropped.
            const float centre = sample(at + 2U * sampleSize) * kHalfPower;
            left = (sample(at) + centre + sample(at + 4U * sampleSize) * kHalfPower) / (1.0F + 2.0F * kHalfPower);
            right = (sample(at + sampleSize) + centre + sample(at + 5U * sampleSize) * kHalfPower) / (1.0F + 2.0F * kHalfPower);
        }
        stereo[first + i * 2U] = left;
        stereo[first + i * 2U + 1U] = right;
    }
    return true;
}
std::vector<float> ResampleStereoLinear(std::span<const float> stereo, std::uint32_t sourceRate, std::uint32_t targetRate, std::size_t targetFrames) {
    std::vector<float> out(targetFrames * 2U, 0.0F);
    const std::size_t sourceFrames = stereo.size() / 2U;
    if (sourceRate == 0 || targetRate == 0 || sourceFrames == 0) return out;
    const double step = static_cast<double>(sourceRate) / targetRate;
    for (std::size_t n = 0; n < targetFrames; ++n) {
        const double position = static_cast<double>(n) * step;
        const auto index = static_cast<std::size_t>(position);
        if (index >= sourceFrames) break;
        const float fraction = static_cast<float>(position - static_cast<double>(index));
        const std::size_t next = std::min(index + 1U, sourceFrames - 1U);
        for (std::size_t channel = 0; channel < 2U; ++channel) {
            const float a = stereo[index * 2U + channel], b = stereo[next * 2U + channel];
            out[n * 2U + channel] = a + (b - a) * fraction;
        }
    }
    return out;
}
}
