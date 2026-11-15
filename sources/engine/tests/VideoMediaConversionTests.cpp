#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "scene/VideoMediaConversion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace kb::tests {
namespace {
using kb::scene::YuvMatrix;
using kb::scene::YuvPicture;
using kb::scene::YuvPlaneLayout;

struct Rgb {
    int r, g, b;
};

// Independent formulation of the matrix: normalised luma and colour differences, then the primaries' weights.
Rgb Reference(int y, int cb, int cr, YuvMatrix matrix, bool fullRange) {
    const double kr = matrix == YuvMatrix::Bt709 ? 0.2126 : 0.299, kb = matrix == YuvMatrix::Bt709 ? 0.0722 : 0.114, kg = 1.0 - kr - kb;
    const double luma = fullRange ? y / 255.0 : (y - 16) / 219.0;
    const double pb = (cb - 128) / (fullRange ? 255.0 : 224.0), pr = (cr - 128) / (fullRange ? 255.0 : 224.0);
    const double r = luma + 2.0 * (1.0 - kr) * pr, b = luma + 2.0 * (1.0 - kb) * pb, g = (luma - kr * r - kb * b) / kg;
    const auto to8 = [](double v) { return static_cast<int>(std::clamp(std::lround(v * 255.0), 0L, 255L)); };
    return {to8(r), to8(g), to8(b)};
}

int LumaAt(int x, int y) { return 20 + (x * 29 + y * 17) % 200; }
int CbAt(int cx, int cy) { return 60 + (cx * 37 + cy * 23) % 140; }
int CrAt(int cx, int cy) { return 70 + (cx * 19 + cy * 41) % 150; }

// Builds a coded picture of `codedWidth` x `codedHeight` pixels inside padded storage; every padding byte is garbage.
std::vector<std::uint8_t> BuildPicture(YuvPlaneLayout layout, int codedWidth, int codedHeight, int stride, int sliceHeight) {
    const int chromaStride = layout == YuvPlaneLayout::Nv12 ? stride : stride / 2, chromaRows = sliceHeight / 2;
    const std::size_t luma = static_cast<std::size_t>(stride) * sliceHeight;
    std::vector<std::uint8_t> bytes(luma + static_cast<std::size_t>(chromaStride) * chromaRows * (layout == YuvPlaneLayout::Nv12 ? 1 : 2), 0xAB);
    for (int y = 0; y < codedHeight; ++y)
        for (int x = 0; x < codedWidth; ++x) bytes[static_cast<std::size_t>(y) * stride + x] = static_cast<std::uint8_t>(LumaAt(x, y));
    for (int cy = 0; cy < (codedHeight + 1) / 2; ++cy)
        for (int cx = 0; cx < (codedWidth + 1) / 2; ++cx) {
            if (layout == YuvPlaneLayout::Nv12) {
                bytes[luma + static_cast<std::size_t>(cy) * chromaStride + cx * 2] = static_cast<std::uint8_t>(CbAt(cx, cy));
                bytes[luma + static_cast<std::size_t>(cy) * chromaStride + cx * 2 + 1] = static_cast<std::uint8_t>(CrAt(cx, cy));
            } else {
                bytes[luma + static_cast<std::size_t>(cy) * chromaStride + cx] = static_cast<std::uint8_t>(CbAt(cx, cy));
                bytes[luma + static_cast<std::size_t>(chromaStride) * chromaRows + static_cast<std::size_t>(cy) * chromaStride + cx] =
                    static_cast<std::uint8_t>(CrAt(cx, cy));
            }
        }
    return bytes;
}

bool Near(int a, int b) { return std::abs(a - b) <= 1; }

// Converts the visible window and checks each pixel against the reference of the coded sample it came from.
std::vector<std::uint8_t> ConvertAndCheck(const YuvPicture& picture, const std::vector<std::uint8_t>& bytes, const char* message) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(picture.width) * picture.height * 4U, 0x55);
    Require(kb::scene::ConvertYuvToRgba(picture, bytes, rgba.data()), "A consistent picture must convert");
    for (std::uint32_t y = 0; y < picture.height; ++y)
        for (std::uint32_t x = 0; x < picture.width; ++x) {
            const int ax = static_cast<int>(picture.cropLeft + x), ay = static_cast<int>(picture.cropTop + y);
            const Rgb expected = Reference(LumaAt(ax, ay), CbAt(ax / 2, ay / 2), CrAt(ax / 2, ay / 2), picture.matrix, picture.fullRange);
            const std::uint8_t* pixel = rgba.data() + (static_cast<std::size_t>(y) * picture.width + x) * 4U;
            Require(Near(pixel[0], expected.r) && Near(pixel[1], expected.g) && Near(pixel[2], expected.b) && pixel[3] == 255, message);
        }
    return rgba;
}

void TestKnownColors() {
    // Limited-range black and white, and full-range white, with neutral colour difference.
    for (const auto layout : {YuvPlaneLayout::Nv12, YuvPlaneLayout::I420}) {
        for (const bool full : {false, true}) {
            YuvPicture picture;
            picture.layout = layout;
            picture.fullRange = full;
            picture.width = picture.height = 2;
            picture.stride = 2;
            picture.sliceHeight = 2;
            std::vector<std::uint8_t> bytes = {static_cast<std::uint8_t>(full ? 0 : 16), static_cast<std::uint8_t>(full ? 255 : 235),
                static_cast<std::uint8_t>(full ? 0 : 16), static_cast<std::uint8_t>(full ? 255 : 235), 128, 128};
            std::vector<std::uint8_t> rgba(16);
            Require(kb::scene::ConvertYuvToRgba(picture, bytes, rgba.data()), "A tight 2x2 picture must convert");
            Require(rgba[0] == 0 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 255, "Black luma must convert to black");
            Require(rgba[4] == 255 && rgba[5] == 255 && rgba[6] == 255 && rgba[7] == 255, "White luma must convert to white");
        }
    }
}

void TestStridePaddingAndBothLayouts() {
    for (const auto matrix : {YuvMatrix::Bt601, YuvMatrix::Bt709}) {
        for (const bool full : {false, true}) {
            YuvPicture picture;
            picture.matrix = matrix;
            picture.fullRange = full;
            picture.width = 6;
            picture.height = 4;
            picture.stride = 16;      // rows padded to 16 bytes
            picture.sliceHeight = 8;  // luma plane padded to 8 rows
            picture.layout = YuvPlaneLayout::Nv12;
            const auto semiPlanar = ConvertAndCheck(picture, BuildPicture(YuvPlaneLayout::Nv12, 6, 4, 16, 8), "NV12 with padded rows must match the reference");
            picture.layout = YuvPlaneLayout::I420;
            const auto planar = ConvertAndCheck(picture, BuildPicture(YuvPlaneLayout::I420, 6, 4, 16, 8), "I420 with padded rows must match the reference");
            Require(semiPlanar == planar, "NV12 and I420 of the same samples must convert identically");
        }
    }
}

void TestOddCropWindow() {
    for (const auto layout : {YuvPlaneLayout::Nv12, YuvPlaneLayout::I420}) {
        YuvPicture picture;
        picture.layout = layout;
        picture.matrix = YuvMatrix::Bt709;
        picture.cropLeft = 1;  // odd offsets start in the middle of a chroma sample
        picture.cropTop = 3;
        picture.width = 5;     // odd size
        picture.height = 3;
        picture.stride = 12;
        picture.sliceHeight = 10;
        const auto rgba = ConvertAndCheck(picture, BuildPicture(layout, 8, 8, 12, 10), "An odd crop window must read the chroma of its coded position");
        Require(rgba.size() == 5U * 3U * 4U, "The output must hold exactly the visible window");
    }
}

void TestInconsistentPicturesAreRejected() {
    YuvPicture picture;
    picture.width = picture.height = 4;
    picture.stride = 4;
    picture.sliceHeight = 4;
    std::vector<std::uint8_t> bytes(24, 128);
    std::vector<std::uint8_t> rgba(64);
    Require(kb::scene::ConvertYuvToRgba(picture, bytes, rgba.data()), "A tight 4x4 NV12 picture is 24 bytes");
    bytes.pop_back();
    Require(!kb::scene::ConvertYuvToRgba(picture, bytes, rgba.data()), "A truncated chroma plane must be rejected");
    bytes.resize(24, 128);
    picture.cropLeft = 1;
    Require(!kb::scene::ConvertYuvToRgba(picture, bytes, rgba.data()), "A crop window past the row stride must be rejected");
    picture.cropLeft = 0;
    picture.cropTop = 1;
    Require(!kb::scene::ConvertYuvToRgba(picture, bytes, rgba.data()), "A crop window past the plane height must be rejected");
    picture.cropTop = 0;
    picture.width = 0;
    Require(!kb::scene::ConvertYuvToRgba(picture, bytes, rgba.data()), "An empty window must be rejected");
}

std::vector<std::uint8_t> Int16Bytes(const std::vector<std::int16_t>& samples) {
    std::vector<std::uint8_t> bytes(samples.size() * 2U);
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

void TestPcmFoldAndResample() {
    std::vector<float> stereo;
    Require(kb::scene::AppendPcmAsStereo(kb::scene::PcmEncoding::Int16, 1, Int16Bytes({16384, -16384}), stereo) && stereo.size() == 4U &&
            NearlyEqual(stereo[0], 0.5F) && NearlyEqual(stereo[1], 0.5F) && NearlyEqual(stereo[2], -0.5F) && NearlyEqual(stereo[3], -0.5F),
        "Mono must be copied to both channels");
    stereo.clear();
    const float pair[2] = {0.25F, -0.75F};
    std::vector<std::uint8_t> floatBytes(sizeof(pair));
    std::memcpy(floatBytes.data(), pair, sizeof(pair));
    Require(kb::scene::AppendPcmAsStereo(kb::scene::PcmEncoding::Float32, 2, floatBytes, stereo) && stereo.size() == 2U &&
            NearlyEqual(stereo[0], 0.25F) && NearlyEqual(stereo[1], -0.75F),
        "Stereo float must pass through");
    stereo.clear();
    // 5.1 with only the centre channel set: it joins both sides at -3 dB, scaled by the sum of the side weights.
    Require(kb::scene::AppendPcmAsStereo(kb::scene::PcmEncoding::Int16, 6, Int16Bytes({0, 0, 32767, 0, 0, 0}), stereo) && stereo.size() == 2U &&
            std::fabs(stereo[0] - 0.2929F) < 0.001F && std::fabs(stereo[1] - 0.2929F) < 0.001F,
        "A 5.1 centre channel must fold into both sides");
    Require(!kb::scene::AppendPcmAsStereo(kb::scene::PcmEncoding::Int16, 3, Int16Bytes({1, 2, 3}), stereo), "Three channels have no defined fold");
    Require(!kb::scene::AppendPcmAsStereo(kb::scene::PcmEncoding::Int16, 2, Int16Bytes({1, 2, 3}), stereo), "A partial frame must be rejected");

    // Doubling the rate interpolates in between; the last source frame is held for the tail frame; frames past the source are silent.
    const std::vector<float> ramp = {0.0F, 10.0F, 1.0F, 11.0F, 2.0F, 12.0F, 3.0F, 13.0F};
    const auto up = kb::scene::ResampleStereoLinear(ramp, 2, 4, 8);
    const float expectedLeft[8] = {0.0F, 0.5F, 1.0F, 1.5F, 2.0F, 2.5F, 3.0F, 3.0F};
    for (std::size_t i = 0; i < 8U; ++i)
        Require(NearlyEqual(up[i * 2U], expectedLeft[i]) && NearlyEqual(up[i * 2U + 1U], expectedLeft[i] + 10.0F), "Upsampling must interpolate linearly");
    const auto same = kb::scene::ResampleStereoLinear(ramp, 48000, 48000, 6);
    Require(same.size() == 12U && NearlyEqual(same[6], 3.0F) && NearlyEqual(same[7], 13.0F) && same[8] == 0.0F && same[11] == 0.0F,
        "An equal rate must copy the source and pad with silence");
    Require(kb::scene::ResampleStereoLinear({}, 44100, 48000, 3) == std::vector<float>(6, 0.0F), "An empty source must give silence");
}
} // namespace

void RunVideoMediaConversionTests() {
    TestKnownColors();
    TestStridePaddingAndBothLayouts();
    TestOddCropWindow();
    TestInconsistentPicturesAreRejected();
    TestPcmFoldAndResample();
}
} // namespace kb::tests
