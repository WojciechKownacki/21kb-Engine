#include "RendererTestSupport.hpp"

#include "kb/render/RendererCapabilityReport.hpp"
#include "kb/render/frame/FullscreenTexturePass.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kb::render::tests {
namespace {

using Rgb = std::array<double, 3U>;

// SMPTE ST 2084: luminance in nits to the signal, with the constants the standard defines.
double PqEncode(double nits) {
    constexpr double m1 = 2610.0 / 16384.0;
    constexpr double m2 = 2523.0 / 4096.0 * 128.0;
    constexpr double c1 = 3424.0 / 4096.0;
    constexpr double c2 = 2413.0 / 4096.0 * 32.0;
    constexpr double c3 = 2392.0 / 4096.0 * 32.0;
    const double power = std::pow(std::max(nits, 0.0) / 10000.0, m1);
    return std::pow((c1 + c2 * power) / (1.0 + c3 * power), m2);
}

// The Rec.709 to Rec.2020 matrix derived from the two sets of primaries and the shared D65 white.
std::array<std::array<double, 3U>, 3U> Rec709ToRec2020() {
    using Matrix = std::array<std::array<double, 3U>, 3U>;
    const auto rgbToXyz = [](const std::array<std::array<double, 2U>, 3U>& primaries) {
        constexpr std::array<double, 3U> white{ 0.3127 / 0.3290, 1.0, (1.0 - 0.3127 - 0.3290) / 0.3290 };
        Matrix columns{};
        for (std::size_t column = 0U; column < 3U; ++column) {
            const double x = primaries[column][0];
            const double y = primaries[column][1];
            columns[0][column] = x / y;
            columns[1][column] = 1.0;
            columns[2][column] = (1.0 - x - y) / y;
        }
        const auto det = [](const Matrix& m) {
            return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        };
        std::array<double, 3U> scale{};
        const double total = det(columns);
        for (std::size_t column = 0U; column < 3U; ++column) {
            Matrix replaced = columns;
            for (std::size_t row = 0U; row < 3U; ++row) {
                replaced[row][column] = white[row];
            }
            scale[column] = det(replaced) / total;
        }
        for (std::size_t row = 0U; row < 3U; ++row) {
            for (std::size_t column = 0U; column < 3U; ++column) {
                columns[row][column] *= scale[column];
            }
        }
        return columns;
    };
    const Matrix rec709 = rgbToXyz({ { { 0.640, 0.330 }, { 0.300, 0.600 }, { 0.150, 0.060 } } });
    const Matrix rec2020 = rgbToXyz({ { { 0.708, 0.292 }, { 0.170, 0.797 }, { 0.131, 0.046 } } });
    // inverse(rec2020) * rec709 by cofactors.
    Matrix inverse{};
    const Matrix& m = rec2020;
    const double determinant = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
        m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            const std::size_t r0 = (column + 1U) % 3U;
            const std::size_t r1 = (column + 2U) % 3U;
            const std::size_t c0 = (row + 1U) % 3U;
            const std::size_t c1 = (row + 2U) % 3U;
            inverse[row][column] = (m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0]) / determinant;
        }
    }
    Matrix result{};
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            for (std::size_t k = 0U; k < 3U; ++k) {
                result[row][column] += inverse[row][k] * rec709[k][column];
            }
        }
    }
    return result;
}

Rgb ToRec2020(const Rgb& color) {
    const auto matrix = Rec709ToRec2020();
    Rgb result{};
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            result[row] += matrix[row][column] * color[column];
        }
    }
    return result;
}

double Aces(double x) {
    x = std::max(x, 0.0);
    return std::clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// The tonemap an HDR display gets: the operator up to scene value 1, then a shoulder toward `peak`.
double HdrTonemap(double x, double peak, bool aces) {
    const auto curve = [aces](double value) { return aces ? Aces(value) : std::max(value, 0.0); };
    constexpr double probe = 1.0 / 64.0;
    const double knee = curve(1.0);
    const double slope = (curve(1.0 + probe) - curve(1.0 - probe)) * (0.5 / probe);
    const double over = std::max(x - 1.0, 0.0);
    const double room = std::max(peak - knee, 1.0e-4);
    return curve(std::min(x, 1.0)) + slope * over / (1.0 + slope * over / room);
}

void PqEncodingMatchesStandardValues() {
    Require(std::fabs(PqEncode(100.0) - 0.5081) < 1.0e-3, "PQ(100 nits) must be 0.5081");
    Require(std::fabs(PqEncode(1000.0) - 0.7518) < 1.0e-3, "PQ(1000 nits) must be 0.7518");
    Require(std::fabs(PqEncode(10000.0) - 1.0) < 1.0e-3, "PQ(10000 nits) must be 1");
    Require(PqEncode(0.0) < 1.0e-6, "PQ(0 nits) must be 0");
}

void Rec709ToRec2020MatchesBt2087() {
    const Rgb red = ToRec2020({ 1.0, 0.0, 0.0 });
    Require(std::fabs(red[0] - 0.6274) < 1.0e-3 && std::fabs(red[1] - 0.0691) < 1.0e-3 && std::fabs(red[2] - 0.0164) < 1.0e-3,
        "Pure Rec.709 red must convert to the first column of the BT.2087 matrix");
    const Rgb white = ToRec2020({ 1.0, 1.0, 1.0 });
    for (const double channel : white) {
        Require(std::fabs(channel - 1.0) < 1.0e-3, "White must stay white across the primaries");
    }
}

void ResolverReportsWhyHdrIsOffOrOn() {
    const HdrOutputCapabilities supported{
        .backend = bgfx::RendererType::Direct3D11,
        .supportedFlags = BGFX_CAPS_HDR10,
        .hasWindow = true,
        .tenBitBackbuffer = true,
        .floatDisplayTarget = true,
    };
    DisplayConfig off{};
    const DisplayHdrOutputState offState = ResolveHdrOutput(off, supported);
    Require(!offState.active && offState.reason != nullptr && std::strstr(offState.reason, "off") != nullptr,
        "HDR output off in the settings must stay off and say so");

    DisplayConfig automatic{};
    automatic.hdrOutputMode = DisplayHdrOutputMode::Auto;
    const DisplayHdrOutputState onState = ResolveHdrOutput(automatic, supported);
    Require(onState.active && onState.paperWhiteNits == 200.0F && onState.maxNits == 1000.0F, "Auto must turn HDR on when everything is supported");

    HdrOutputCapabilities noHdrDisplay = supported;
    noHdrDisplay.supportedFlags = 0U;
    const DisplayHdrOutputState noDisplay = ResolveHdrOutput(automatic, noHdrDisplay);
    Require(!noDisplay.active && noDisplay.reason != nullptr && std::strstr(noDisplay.reason, "HDR10") != nullptr,
        "Auto without BGFX_CAPS_HDR10 must give SDR and name HDR10 in the reason");

    HdrOutputCapabilities metal = supported;
    metal.backend = bgfx::RendererType::Metal;
    Require(!ResolveHdrOutput(automatic, metal).active, "Auto must stay SDR where the HDR shader variants are not generated");
    HdrOutputCapabilities eightBit = supported;
    eightBit.tenBitBackbuffer = false;
    Require(!ResolveHdrOutput(automatic, eightBit).active && std::strstr(ResolveHdrOutput(automatic, eightBit).reason, "10-bit") != nullptr,
        "Auto without a 10-bit back buffer must give SDR and say why");

    DisplayConfig inverted = automatic;
    inverted.hdrMaxNits = 100.0F;
    Require(!ResolveHdrOutput(inverted, supported).active, "A maximum brightness below paper white must not turn HDR on");
}

#if defined(_WIN32)
constexpr std::uint16_t kSize = 64U;

[[nodiscard]] bgfx::TextureHandle SolidTexture(const std::array<float, 3U>& color) {
    std::vector<std::uint16_t> halves(static_cast<std::size_t>(kSize) * kSize * 4U);
    for (std::size_t texel = 0U; texel < static_cast<std::size_t>(kSize) * kSize; ++texel) {
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
            halves[texel * 4U + channel] = bx::halfFromFloat(color[channel]);
        }
        halves[texel * 4U + 3U] = bx::halfFromFloat(1.0F);
    }
    return bgfx::createTexture2D(kSize, kSize, false, 1U, bgfx::TextureFormat::RGBA16F, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        bgfx::copy(halves.data(), static_cast<std::uint32_t>(halves.size() * sizeof(std::uint16_t))));
}

class DisplayHarness {
public:
    [[nodiscard]] bool Init() {
        window_ = CreateWindowExW(0, L"STATIC", L"hdr", WS_OVERLAPPEDWINDOW, 0, 0, kSize, kSize, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        bgfx::Init init;
        init.type = bgfx::RendererType::Direct3D11;
        init.swapChain.width = kSize;
        init.swapChain.height = kSize;
        init.swapChain.nwh = window_;
        init.reset = BGFX_RESET_NONE;
        if (!bgfx::init(init)) {
            return false;
        }
        composed_ = bgfx::createTexture2D(kSize, kSize, false, 1U, bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        composedBuffer_ = bgfx::createFrameBuffer(1U, &composed_, false);
        for (std::size_t index = 0U; index < outputs_.size(); ++index) {
            outputs_[index] = bgfx::createTexture2D(kSize, kSize, false, 1U, kFormats[index], BGFX_TEXTURE_RT);
            outputBuffers_[index] = bgfx::createFrameBuffer(1U, &outputs_[index], false);
            readbacks_[index] = bgfx::createTexture2D(kSize, kSize, false, 1U, kFormats[index], BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
        }
        bgfx::frame();
        bgfx::frame();
        return bgfx::isValid(composedBuffer_) && bgfx::isValid(outputBuffers_[0]) && bgfx::isValid(outputBuffers_[1]) &&
            bgfx::isValid(readbacks_[0]) && bgfx::isValid(readbacks_[1]);
    }

    [[nodiscard]] bgfx::FrameBufferHandle ComposedBuffer() const noexcept { return composedBuffer_; }
    [[nodiscard]] bgfx::TextureHandle Composed() const noexcept { return composed_; }
    // Output 0 is RGB10A2 (the HDR10 encode), output 1 RGBA32F (an SDR display).
    [[nodiscard]] bgfx::FrameBufferHandle OutputBuffer(std::size_t index) const noexcept { return outputBuffers_[index]; }

    // The bytes of output `index` after the views the caller submitted.
    [[nodiscard]] std::vector<std::uint8_t> Read(std::size_t index) {
        bgfx::frame();
        bgfx::blit(ViewReadback, bgfx::TextureRegion{ .handle = readbacks_[index] },
            bgfx::TextureRegion{ .handle = outputs_[index], .width = kSize, .height = kSize });
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kSize) * kSize * (index == 0U ? 4U : 16U));
        const std::uint32_t readyFrame = bgfx::read(bgfx::TextureRegion{ .handle = readbacks_[index] }, bytes.data());
        std::uint32_t frame = bgfx::frame();
        for (int guard = 0; frame < readyFrame && guard < 8; ++guard) {
            frame = bgfx::frame();
        }
        Require(frame >= readyFrame, "HDR output readback did not become ready");
        return bytes;
    }

    void Shutdown() {
        for (std::size_t index = 0U; index < outputs_.size(); ++index) {
            bgfx::destroy(readbacks_[index]);
            bgfx::destroy(outputBuffers_[index]);
            bgfx::destroy(outputs_[index]);
        }
        bgfx::destroy(composedBuffer_);
        bgfx::destroy(composed_);
        bgfx::shutdown();
        if (window_ != nullptr) {
            DestroyWindow(window_);
            window_ = nullptr;
        }
    }

    static constexpr std::uint16_t ViewReadback = 10U;

private:
    static constexpr std::array<bgfx::TextureFormat::Enum, 2U> kFormats{ bgfx::TextureFormat::RGB10A2, bgfx::TextureFormat::RGBA32F };

    HWND window_ = nullptr;
    bgfx::TextureHandle composed_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle composedBuffer_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::TextureHandle, 2U> outputs_{ { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE } };
    std::array<bgfx::FrameBufferHandle, 2U> outputBuffers_{ { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE } };
    std::array<bgfx::TextureHandle, 2U> readbacks_{ { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE } };
};

// The 10-bit channels of the pixel at the middle of an RGB10A2 image.
std::array<int, 3U> CentreRgb10(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t packed = 0U;
    std::memcpy(&packed, bytes.data() + (static_cast<std::size_t>(kSize / 2U) * kSize + kSize / 2U) * 4U, sizeof(packed));
    return { static_cast<int>(packed & 0x3FFU), static_cast<int>((packed >> 10U) & 0x3FFU), static_cast<int>((packed >> 20U) & 0x3FFU) };
}

std::array<float, 3U> CentreRgba32(const std::vector<std::uint8_t>& bytes) {
    std::array<float, 3U> color{};
    std::memcpy(color.data(), bytes.data() + (static_cast<std::size_t>(kSize / 2U) * kSize + kSize / 2U) * 16U, sizeof(color));
    return color;
}

void HdrComposeAndEncodeMatchCpuReference(DisplayHarness& harness, FullscreenTexturePass& pass) {
    constexpr double paperWhite = 200.0;
    constexpr double peak = 5.0;
    struct Case {
        const char* name;
        std::array<float, 3U> scene;
        FullscreenTextureTonemapOperator tonemap;
    };
    const std::array<Case, 7U> cases{ {
        { "mid gray", { { 0.5F, 0.5F, 0.5F } }, FullscreenTextureTonemapOperator::None },
        { "paper white", { { 1.0F, 1.0F, 1.0F } }, FullscreenTextureTonemapOperator::None },
        { "shoulder", { { 4.0F, 4.0F, 4.0F } }, FullscreenTextureTonemapOperator::None },
        { "very bright", { { 1000.0F, 1000.0F, 1000.0F } }, FullscreenTextureTonemapOperator::None },
        { "colour", { { 0.5F, 0.125F, 0.03F } }, FullscreenTextureTonemapOperator::None },
        { "aces below the knee", { { 0.5F, 0.5F, 0.5F } }, FullscreenTextureTonemapOperator::Aces },
        { "aces above the knee", { { 6.0F, 3.0F, 1.5F } }, FullscreenTextureTonemapOperator::Aces },
    } };
    for (const Case& test : cases) {
        const bgfx::TextureHandle scene = SolidTexture(test.scene);
        FullscreenTextureOutputTransform compose{ .tonemap = test.tonemap };
        compose.hdr = { .stage = FullscreenTextureHdrStage::Compose, .peakRatio = static_cast<float>(peak) };
        Require(pass.Submit(FullscreenTexturePassDesc{ .viewId = 0U, .sourceTexture = scene, .frameBuffer = harness.ComposedBuffer(),
                    .extent = { kSize, kSize }, .outputTransform = compose, .forceOpaqueAlpha = true }),
            "The HDR compose pass must submit");
        FullscreenTextureOutputTransform encode{};
        encode.hdr = { .stage = FullscreenTextureHdrStage::Encode, .paperWhiteNits = static_cast<float>(paperWhite) };
        Require(pass.Submit(FullscreenTexturePassDesc{ .viewId = 1U, .sourceTexture = harness.Composed(), .frameBuffer = harness.OutputBuffer(0U),
                    .extent = { kSize, kSize }, .outputTransform = encode, .forceOpaqueAlpha = true }),
            "The HDR encode pass must submit");
        const std::array<int, 3U> pq = CentreRgb10(harness.Read(0U));

        Rgb tonemapped{};
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
            tonemapped[channel] = HdrTonemap(test.scene[channel], peak, test.tonemap == FullscreenTextureTonemapOperator::Aces);
        }
        const Rgb rec2020 = ToRec2020(tonemapped);
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
            const double expected = PqEncode(rec2020[channel] * paperWhite) * 1023.0;
            std::fprintf(stderr, "HDR10 %s channel %zu: shader %d, reference %.2f\n", test.name, channel, pq[channel], expected);
            Require(std::fabs(static_cast<double>(pq[channel]) - expected) <= 2.0, "HDR10 output differs from the PQ reference by more than 2/1023");
        }
        bgfx::destroy(scene);
    }
}

// An SDR display is what it was: the settings of an HDR display are ignored, and the colour is the tonemap,
// the sRGB encoding and the dither of the unchanged shader.
void SdrDisplayIsUnchanged(DisplayHarness& harness, FullscreenTexturePass& pass) {
    const bgfx::TextureHandle scene = SolidTexture({ { 0.5F, 0.25F, 0.0625F } });
    const auto render = [&](FullscreenTextureHdrOutput hdr) {
        FullscreenTextureOutputTransform transform{};
        transform.hdr = hdr;
        Require(pass.Submit(FullscreenTexturePassDesc{ .viewId = 0U, .sourceTexture = scene, .frameBuffer = harness.OutputBuffer(1U),
                    .extent = { kSize, kSize }, .outputTransform = transform, .forceOpaqueAlpha = true }),
            "The SDR display pass must submit");
        return harness.Read(1U);
    };
    const std::vector<std::uint8_t> plain = render({});
    const std::vector<std::uint8_t> ignored = render({ .stage = FullscreenTextureHdrStage::Off, .peakRatio = 9.0F, .paperWhiteNits = 1.0F });
    Require(plain == ignored, "An SDR display must ignore the HDR settings completely");

    const std::array<float, 3U> shown = CentreRgba32(plain);
    const double centre = kSize / 2.0 + 0.5;
    const double noise = [&] {
        const double dot = centre * 0.06711056 + centre * 0.00583715;
        const double inner = dot - std::floor(dot);
        const double outer = 52.9829189 * inner;
        return outer - std::floor(outer);
    }();
    const std::array<double, 3U> input{ 0.5, 0.25, 0.0625 };
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
        const double aces = Aces(input[channel]);
        const double srgb = aces <= 0.0031308 ? aces * 12.92 : 1.055 * std::pow(aces, 1.0 / 2.4) - 0.055;
        const double expected = srgb + (noise - 0.5) / 255.0;
        std::fprintf(stderr, "SDR channel %zu: shader %.6f, reference %.6f\n", channel, shown[channel], expected);
        Require(std::fabs(shown[channel] - expected) < 2.0e-4, "The SDR display image changed");
    }
    bgfx::destroy(scene);
}
#endif

} // namespace

void RunDisplayHdrOutputCpuTests() {
    PqEncodingMatchesStandardValues();
    Rec709ToRec2020MatchesBt2087();
    ResolverReportsWhyHdrIsOffOrOn();
}

void RunDisplayHdrOutputGpuTests() {
#if defined(_WIN32)
    DisplayHarness harness;
    Require(harness.Init(), "HDR output validation requires a Direct3D11 device");
    FullscreenTexturePass pass;
    Require(pass.Initialize(), "The display programs must load");
    SdrDisplayIsUnchanged(harness, pass);
    HdrComposeAndEncodeMatchCpuReference(harness, pass);
    pass.Shutdown();
    harness.Shutdown();
#else
    Require(false, "HDR output validation requires a Direct3D11 device");
#endif
}

} // namespace kb::render::tests
