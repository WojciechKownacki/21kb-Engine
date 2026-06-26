#include "engine/assets/AssetMetadata.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/SceneRenderTarget.hpp"
#include "kb/render/SceneRenderTargetFormat.hpp"
#include "kb/render/post/ScenePostProcessTargets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLightingAccess.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"KBRenderSmokeWindow";
constexpr wchar_t kWindowTitle[] = L"21kb bgfx smoke";
constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;
constexpr std::uint32_t kSmokeRenderWidth = 640U;
constexpr std::uint32_t kSmokeRenderHeight = 360U;
constexpr std::uint32_t kMaterialTextureVariantFrameInterval = 12U;
constexpr std::size_t kMaterialTextureVariantPhaseCount = 4U;

class Win32RenderSurface final : public kb::render::RenderSurface {
public:
    explicit Win32RenderSurface(HWND window) noexcept
        : window_(window) {}

    [[nodiscard]] std::uint32_t Width() const noexcept override {
        RECT rect{};
        if (GetClientRect(window_, &rect) == 0) {
            return 0;
        }
        return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        RECT rect{};
        if (GetClientRect(window_, &rect) == 0) {
            return 0;
        }
        return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return window_;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }

private:
    HWND window_ = nullptr;
};

struct SmokeWindowState {
    kb::render::Renderer* renderer = nullptr;
};

struct SmokeOptions {
    std::uint32_t maxFrames = 600;
    bgfx::RendererType::Enum rendererType = bgfx::RendererType::Count;
    bool exerciseWindowEvents = false;
    bool validateScreenshot = true;
    bool validateMaterialTextureVariants = true;
    bool forceGpuDrivenCpuFallback = false;
    float autoExposureLuminance = 0.18F;
    float autoExposureBiasStops = 0.0F;
    std::string screenshotPath;
};

struct ScreenshotValidationStats {
    std::uint32_t sampledPixelCount = 0;
    std::uint32_t brightPixelCount = 0;
    std::uint32_t distinctColorCount = 0;
    std::uint32_t minLuma = 255;
    std::uint32_t maxLuma = 0;
};

struct CapturedScreenshot {
    std::string bgraPixels;
    int width = 0;
    int height = 0;
};

struct ScreenshotDeltaStats {
    std::uint32_t sampledPixelCount = 0;
    std::uint32_t changedPixelCount = 0;
    std::uint32_t maxChannelDelta = 0;
    double averageChannelDelta = 0.0;
};

[[nodiscard]] ScreenshotValidationStats AnalyzeBgraPixels(const std::string& pixels);
[[nodiscard]] bool ScreenshotLooksRendered(const ScreenshotValidationStats& stats) noexcept;
void PrepareWindowForCapture(HWND window) noexcept;
[[nodiscard]] bool CaptureClientScreenshotBgra(HWND window, CapturedScreenshot& capture);
[[nodiscard]] bool WriteScreenshotBmp(const CapturedScreenshot& capture, const char* path);
[[nodiscard]] ScreenshotDeltaStats AnalyzeScreenshotDelta(const CapturedScreenshot& baseline, const CapturedScreenshot& variant);
[[nodiscard]] bool MaterialTextureVariantChanged(const ScreenshotDeltaStats& stats) noexcept;
[[nodiscard]] std::array<CapturedScreenshot, kMaterialTextureVariantPhaseCount> ExtractMaterialTextureVariantRegionCaptures(const CapturedScreenshot& capture);
[[nodiscard]] bool ValidateMaterialTextureVariantCaptures(const std::array<CapturedScreenshot, kMaterialTextureVariantPhaseCount>& captures);

void WriteTriangleGltf(const std::filesystem::path& root) {
    const std::filesystem::path binPath = root / "mesh.bin";
    {
        const std::vector<float> positions{
            -0.8F, 0.05F, -0.6F,
            0.8F, 0.05F, -0.6F,
            0.0F, 0.05F, 0.8F,
        };
        const std::vector<float> normals{
            0.0F, 1.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
        };
        const std::vector<float> tangents{
            1.0F, 0.0F, 0.0F, 1.0F,
            1.0F, 0.0F, 0.0F, 1.0F,
            1.0F, 0.0F, 0.0F, 1.0F,
        };
        const std::vector<float> texCoords{
            0.0F, 0.0F,
            1.0F, 0.0F,
            0.5F, 1.0F,
        };
        const std::uint16_t indices[]{ 0U, 1U, 2U };
        const std::uint16_t padding = 0U;

        std::ofstream output{ binPath, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(positions.data()), static_cast<std::streamsize>(positions.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(normals.data()), static_cast<std::streamsize>(normals.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(tangents.data()), static_cast<std::streamsize>(tangents.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(texCoords.data()), static_cast<std::streamsize>(texCoords.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(indices), static_cast<std::streamsize>(sizeof(indices)));
        output.write(reinterpret_cast<const char*>(&padding), static_cast<std::streamsize>(sizeof(padding)));
    }

    std::ofstream output{ root / "triangle.gltf", std::ios::trunc };
    output
        << "{\n"
        << "  \"asset\": { \"version\": \"2.0\" },\n"
        << "  \"scene\": 0,\n"
        << "  \"scenes\": [{ \"nodes\": [0] }],\n"
        << "  \"nodes\": [{ \"mesh\": 0 }],\n"
        << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, \"TANGENT\": 2, \"TEXCOORD_0\": 3 }, \"indices\": 4 }] }],\n"
        << "  \"buffers\": [{ \"uri\": \"mesh.bin\", \"byteLength\": 152 }],\n"
        << "  \"bufferViews\": [\n"
        << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 48, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 120, \"byteLength\": 24, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 144, \"byteLength\": 6, \"target\": 34963 }\n"
        << "  ],\n"
        << "  \"accessors\": [\n"
        << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [-0.8, 0.05, -0.6], \"max\": [0.8, 0.05, 0.8] },\n"
        << "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        << "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\" },\n"
        << "    { \"bufferView\": 3, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        << "    { \"bufferView\": 4, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
        << "  ]\n"
        << "}\n";
}

void WriteTexture(const std::filesystem::path& path, std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha = 255U) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "size 1 1\n"
        << "rgba8 " << red << ' ' << green << ' ' << blue << ' ' << alpha << "\n";
}

void WriteMaterial(
    const std::filesystem::path& path,
    std::uint64_t albedoTextureId,
    std::uint64_t normalTextureId,
    std::uint64_t metallicRoughnessTextureId,
    float normalScale = 1.0F) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "baseColor 1.0 1.0 1.0 1.0\n"
        << "metallicFactor 1.0\n"
        << "roughnessFactor 1.0\n"
        << "normalScale " << normalScale << "\n"
        << "alphaMode OPAQUE\n"
        << "doubleSided true\n"
        << "albedoTextureAssetId " << albedoTextureId << "\n"
        << "normalTextureAssetId " << normalTextureId << "\n"
        << "metallicRoughnessTextureAssetId " << metallicRoughnessTextureId << "\n";
}

LRESULT CALLBACK SmokeWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SmokeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_SIZE:
        if (state != nullptr && state->renderer != nullptr && wParam != SIZE_MINIMIZED) {
            const auto width = static_cast<std::uint32_t>(LOWORD(lParam));
            const auto height = static_cast<std::uint32_t>(HIWORD(lParam));
            state->renderer->OnResize(width, height);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

[[nodiscard]] HWND CreateSmokeWindow(HINSTANCE instance, SmokeWindowState& state) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &SmokeWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return nullptr;
    }

    RECT rect{0, 0, kInitialWidth, kInitialHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    return CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        instance,
        &state);
}

void PrepareWindowForCapture(HWND window) noexcept {
    ShowWindow(window, SW_RESTORE);
    SetWindowPos(window, HWND_TOPMOST, 64, 64, kInitialWidth, kInitialHeight, SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    UpdateWindow(window);
    Sleep(50);
}

[[nodiscard]] bool CaptureClientScreenshotBgra(HWND window, CapturedScreenshot& capture) {
    PrepareWindowForCapture(window);

    RECT client{};
    if (GetClientRect(window, &client) == 0) {
        return false;
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    HDC windowDc = GetDC(window);
    if (windowDc == nullptr) {
        return false;
    }
    HDC memoryDc = CreateCompatibleDC(windowDc);
    HBITMAP bitmap = CreateCompatibleBitmap(windowDc, width, height);
    if (memoryDc == nullptr || bitmap == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memoryDc != nullptr) {
            DeleteDC(memoryDc);
        }
        ReleaseDC(window, windowDc);
        return false;
    }

    HGDIOBJ previous = SelectObject(memoryDc, bitmap);
    const BOOL copied = BitBlt(memoryDc, 0, 0, width, height, windowDc, 0, 0, SRCCOPY);
    SelectObject(memoryDc, previous);
    ReleaseDC(window, windowDc);
    if (copied == 0) {
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        return false;
    }

    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = width;
    infoHeader.biHeight = -height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;

    const DWORD pixelBytes = static_cast<DWORD>(width * height * 4);
    std::string pixels;
    pixels.resize(pixelBytes);
    if (GetDIBits(memoryDc, bitmap, 0, static_cast<UINT>(height), pixels.data(), reinterpret_cast<BITMAPINFO*>(&infoHeader), DIB_RGB_COLORS) == 0) {
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        return false;
    }
    DeleteObject(bitmap);
    DeleteDC(memoryDc);

    capture.bgraPixels = std::move(pixels);
    capture.width = width;
    capture.height = height;
    return true;
}

[[nodiscard]] bool WriteScreenshotBmp(const CapturedScreenshot& capture, const char* path) {
    if (path == nullptr || path[0] == '\0' || capture.width <= 0 || capture.height <= 0 || capture.bgraPixels.empty()) {
        return false;
    }

    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + static_cast<DWORD>(capture.bgraPixels.size());

    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = capture.width;
    infoHeader.biHeight = -capture.height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;

    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const BOOL wroteHeader = WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr);
    const BOOL wroteInfo = wroteHeader != 0 && WriteFile(file, &infoHeader, sizeof(infoHeader), &written, nullptr);
    const BOOL wrotePixels = wroteInfo != 0 && WriteFile(file, capture.bgraPixels.data(), static_cast<DWORD>(capture.bgraPixels.size()), &written, nullptr);
    CloseHandle(file);
    return wrotePixels != 0;
}

void ExerciseWindowEvent(HWND window, std::uint32_t frameCount) {
    switch (frameCount) {
    case 2:
        SetWindowPos(window, nullptr, 0, 0, 960, 540, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        break;
    case 4:
        ShowWindow(window, SW_MINIMIZE);
        break;
    case 6:
        ShowWindow(window, SW_RESTORE);
        break;
    case 8:
        SetWindowPos(window, nullptr, 0, 0, kInitialWidth, kInitialHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        break;
    default:
        break;
    }
}

[[nodiscard]] ScreenshotValidationStats AnalyzeBgraPixels(const std::string& pixels) {
    ScreenshotValidationStats stats{};
    std::unordered_set<std::uint32_t> colors;
    colors.reserve(256U);
    constexpr std::size_t kPixelStride = 4U;
    constexpr std::size_t kMaxSamples = 4096U;
    const std::size_t pixelCount = pixels.size() / kPixelStride;
    const std::size_t step = std::max<std::size_t>(1U, pixelCount / kMaxSamples);
    for (std::size_t pixel = 0; pixel < pixelCount; pixel += step) {
        const auto b = static_cast<std::uint8_t>(pixels[pixel * kPixelStride]);
        const auto g = static_cast<std::uint8_t>(pixels[pixel * kPixelStride + 1U]);
        const auto r = static_cast<std::uint8_t>(pixels[pixel * kPixelStride + 2U]);
        const std::uint32_t luma = (static_cast<std::uint32_t>(r) * 54U + static_cast<std::uint32_t>(g) * 183U + static_cast<std::uint32_t>(b) * 19U) / 256U;
        stats.minLuma = std::min(stats.minLuma, luma);
        stats.maxLuma = std::max(stats.maxLuma, luma);
        if (luma > 24U) {
            ++stats.brightPixelCount;
        }
        if (colors.size() < 512U) {
            colors.insert((static_cast<std::uint32_t>(r) << 16U) | (static_cast<std::uint32_t>(g) << 8U) | static_cast<std::uint32_t>(b));
        }
        ++stats.sampledPixelCount;
    }
    stats.distinctColorCount = static_cast<std::uint32_t>(colors.size());
    return stats;
}

[[nodiscard]] bool ScreenshotLooksRendered(const ScreenshotValidationStats& stats) noexcept {
    return stats.sampledPixelCount > 0U &&
        stats.brightPixelCount > stats.sampledPixelCount / 100U &&
        stats.distinctColorCount >= 8U &&
        stats.maxLuma > stats.minLuma + 8U;
}

[[nodiscard]] ScreenshotDeltaStats AnalyzeScreenshotDelta(const CapturedScreenshot& baseline, const CapturedScreenshot& variant) {
    ScreenshotDeltaStats stats{};
    if (baseline.width != variant.width ||
        baseline.height != variant.height ||
        baseline.bgraPixels.size() != variant.bgraPixels.size() ||
        baseline.bgraPixels.empty()) {
        return stats;
    }

    constexpr std::size_t kPixelStride = 4U;
    constexpr std::size_t kMaxSamples = 8192U;
    const std::size_t pixelCount = baseline.bgraPixels.size() / kPixelStride;
    const std::size_t step = std::max<std::size_t>(1U, pixelCount / kMaxSamples);
    std::uint64_t totalChannelDelta = 0U;
    for (std::size_t pixel = 0; pixel < pixelCount; pixel += step) {
        const std::size_t offset = pixel * kPixelStride;
        const auto baselineBlue = static_cast<unsigned char>(baseline.bgraPixels[offset]);
        const auto baselineGreen = static_cast<unsigned char>(baseline.bgraPixels[offset + 1U]);
        const auto baselineRed = static_cast<unsigned char>(baseline.bgraPixels[offset + 2U]);
        const auto variantBlue = static_cast<unsigned char>(variant.bgraPixels[offset]);
        const auto variantGreen = static_cast<unsigned char>(variant.bgraPixels[offset + 1U]);
        const auto variantRed = static_cast<unsigned char>(variant.bgraPixels[offset + 2U]);
        const std::uint32_t blueDelta = static_cast<std::uint32_t>(baselineBlue > variantBlue ? baselineBlue - variantBlue : variantBlue - baselineBlue);
        const std::uint32_t greenDelta = static_cast<std::uint32_t>(baselineGreen > variantGreen ? baselineGreen - variantGreen : variantGreen - baselineGreen);
        const std::uint32_t redDelta = static_cast<std::uint32_t>(baselineRed > variantRed ? baselineRed - variantRed : variantRed - baselineRed);
        const std::uint32_t channelDelta = std::max({ redDelta, greenDelta, blueDelta });
        stats.maxChannelDelta = std::max(stats.maxChannelDelta, channelDelta);
        totalChannelDelta += redDelta + greenDelta + blueDelta;
        if (redDelta + greenDelta + blueDelta >= 18U) {
            ++stats.changedPixelCount;
        }
        ++stats.sampledPixelCount;
    }
    if (stats.sampledPixelCount > 0U) {
        stats.averageChannelDelta = static_cast<double>(totalChannelDelta) / static_cast<double>(stats.sampledPixelCount * 3U);
    }
    return stats;
}

[[nodiscard]] bool MaterialTextureVariantChanged(const ScreenshotDeltaStats& stats) noexcept {
    return stats.sampledPixelCount > 0U &&
        stats.changedPixelCount >= std::max<std::uint32_t>(24U, stats.sampledPixelCount / 250U) &&
        stats.maxChannelDelta >= 10U &&
        stats.averageChannelDelta >= 0.35;
}

[[nodiscard]] std::array<CapturedScreenshot, kMaterialTextureVariantPhaseCount> ExtractMaterialTextureVariantRegionCaptures(const CapturedScreenshot& capture) {
    std::array<CapturedScreenshot, kMaterialTextureVariantPhaseCount> regions{};
    if (capture.width <= 0 || capture.height <= 0 || capture.bgraPixels.empty()) {
        return regions;
    }

    constexpr std::size_t kPixelStride = 4U;
    const int renderWidth = std::min<int>(capture.width, static_cast<int>(kSmokeRenderWidth));
    const int renderHeight = std::min<int>(capture.height, static_cast<int>(kSmokeRenderHeight));
    const int cropX = renderWidth / 5;
    const int cropY = renderHeight / 2;
    const int cropWidth = (renderWidth * 3) / 5;
    const int cropHeight = renderHeight / 3;
    const int regionWidth = cropWidth / static_cast<int>(regions.size());
    if (regionWidth <= 0 || cropHeight <= 0 || cropX + cropWidth > capture.width || cropY + cropHeight > capture.height) {
        return regions;
    }

    for (std::size_t regionIndex = 0; regionIndex < regions.size(); ++regionIndex) {
        CapturedScreenshot& region = regions[regionIndex];
        region.width = regionWidth;
        region.height = cropHeight;
        region.bgraPixels.resize(static_cast<std::size_t>(regionWidth * cropHeight) * kPixelStride);
        const int sourceX = cropX + static_cast<int>(regionIndex) * regionWidth;
        for (int y = 0; y < cropHeight; ++y) {
            const std::size_t sourceOffset = (static_cast<std::size_t>((cropY + y) * capture.width + sourceX)) * kPixelStride;
            const std::size_t targetOffset = static_cast<std::size_t>(y * regionWidth) * kPixelStride;
            std::memcpy(
                region.bgraPixels.data() + targetOffset,
                capture.bgraPixels.data() + sourceOffset,
                static_cast<std::size_t>(regionWidth) * kPixelStride);
        }
    }
    return regions;
}

[[nodiscard]] bool ValidateMaterialTextureVariantCaptures(const std::array<CapturedScreenshot, kMaterialTextureVariantPhaseCount>& captures) {
    const std::array<const char*, kMaterialTextureVariantPhaseCount - 1U> labels{
        "roughness",
        "metallic",
        "normal",
    };

    bool valid = true;
    for (std::size_t index = 1U; index < captures.size(); ++index) {
        const ScreenshotDeltaStats stats = AnalyzeScreenshotDelta(captures[0], captures[index]);
        const bool changed = MaterialTextureVariantChanged(stats);
        std::fprintf(
            changed ? stdout : stderr,
            "kb_render_smoke: material texture variant %s delta samples=%u changed=%u max=%u avg=%.3f\n",
            labels[index - 1U],
            stats.sampledPixelCount,
            stats.changedPixelCount,
            stats.maxChannelDelta,
            stats.averageChannelDelta);
        std::fflush(changed ? stdout : stderr);
        valid = valid && changed;
    }
    return valid;
}

[[nodiscard]] SmokeOptions ParseOptions(int argc, char** argv) {
    SmokeOptions options{};
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg == nullptr) {
            continue;
        }
        constexpr char kMaxFramesPrefix[] = "--max-frames=";
        constexpr std::size_t kMaxFramesPrefixLength = sizeof(kMaxFramesPrefix) - 1;
        constexpr char kRendererPrefix[] = "--renderer=";
        constexpr std::size_t kRendererPrefixLength = sizeof(kRendererPrefix) - 1;
        constexpr char kScreenshotPrefix[] = "--screenshot=";
        constexpr std::size_t kScreenshotPrefixLength = sizeof(kScreenshotPrefix) - 1;
        constexpr char kAutoExposureLuminancePrefix[] = "--auto-exposure-luminance=";
        constexpr std::size_t kAutoExposureLuminancePrefixLength = sizeof(kAutoExposureLuminancePrefix) - 1;
        constexpr char kAutoExposureBiasPrefix[] = "--auto-exposure-bias=";
        constexpr std::size_t kAutoExposureBiasPrefixLength = sizeof(kAutoExposureBiasPrefix) - 1;

        if (strncmp(arg, kMaxFramesPrefix, kMaxFramesPrefixLength) == 0) {
            const long parsed = std::strtol(arg + kMaxFramesPrefixLength, nullptr, 10);
            if (parsed > 0 && parsed <= 100000) {
                options.maxFrames = static_cast<std::uint32_t>(parsed);
            }
        } else if (strncmp(arg, kRendererPrefix, kRendererPrefixLength) == 0) {
            const char* renderer = arg + kRendererPrefixLength;
            if (strcmp(renderer, "d3d11") == 0 || strcmp(renderer, "direct3d11") == 0) {
                options.rendererType = bgfx::RendererType::Direct3D11;
            } else if (strcmp(renderer, "noop") == 0) {
                options.rendererType = bgfx::RendererType::Noop;
            }
        } else if (strcmp(arg, "--exercise-window-events") == 0) {
            options.exerciseWindowEvents = true;
        } else if (strcmp(arg, "--no-validate-screenshot") == 0) {
            options.validateScreenshot = false;
        } else if (strcmp(arg, "--no-validate-material-texture-variants") == 0) {
            options.validateMaterialTextureVariants = false;
        } else if (strcmp(arg, "--force-gpu-driven-cpu-fallback") == 0) {
            options.forceGpuDrivenCpuFallback = true;
        } else if (strncmp(arg, kAutoExposureLuminancePrefix, kAutoExposureLuminancePrefixLength) == 0) {
            const double parsed = std::strtod(arg + kAutoExposureLuminancePrefixLength, nullptr);
            if (parsed > 0.0 && parsed < 1.0e6) {
                options.autoExposureLuminance = static_cast<float>(parsed);
            }
        } else if (strncmp(arg, kAutoExposureBiasPrefix, kAutoExposureBiasPrefixLength) == 0) {
            const double parsed = std::strtod(arg + kAutoExposureBiasPrefixLength, nullptr);
            if (parsed > -32.0 && parsed < 32.0) {
                options.autoExposureBiasStops = static_cast<float>(parsed);
            }
        } else if (strncmp(arg, kScreenshotPrefix, kScreenshotPrefixLength) == 0) {
            options.screenshotPath = arg + kScreenshotPrefixLength;
        }
    }

    return options;
}

} // namespace

int main(int argc, char** argv) {
    const SmokeOptions options = ParseOptions(argc, argv);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    std::fprintf(stdout, "kb_render_smoke: start\n");
    std::fflush(stdout);

    kb::render::Renderer renderer;
    SmokeWindowState windowState{.renderer = &renderer};
    HWND window = CreateSmokeWindow(instance, windowState);
    if (window == nullptr) {
        std::fprintf(stderr, "kb_render_smoke: CreateSmokeWindow failed\n");
        std::fflush(stderr);
        return EXIT_FAILURE;
    }
    std::fprintf(stdout, "kb_render_smoke: window created\n");
    std::fflush(stdout);

    Win32RenderSurface surface(window);
    kb::render::DisplayConfig displayConfig{};
    displayConfig.syncMode = kb::render::DisplaySyncMode::VSync;
    displayConfig.targetFps = 180;
    if (options.rendererType != bgfx::RendererType::Count) {
        displayConfig.preferredBgfxRendererType = static_cast<std::int32_t>(options.rendererType);
    }

    if (!renderer.Initialize(surface, &displayConfig)) {
        std::fprintf(stderr, "kb_render_smoke: renderer.Initialize failed\n");
        std::fflush(stderr);
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    std::fprintf(stdout, "kb_render_smoke: renderer initialized\n");
    std::fflush(stdout);
    if (options.forceGpuDrivenCpuFallback) {
        renderer.SetGpuDrivenRuntimeDispatchEnabled(false);
    }
    renderer.SetRuntimeAssetDiscoveryIntervalFrames(1U);
    renderer.SetDefaultPostProcessSettings(kb::render::ScenePostProcessSettings{
        .autoExposureMetering = kb::render::ScenePostProcessSettings::AutoExposureMeteringMode::Manual,
        .outputTransform = kb::render::SceneDisplayOutputTransform{
            .autoExposure = kb::render::FullscreenTextureAutoExposureSettings{
                .enabled = true,
                .meteredAverageLuminance = options.autoExposureLuminance,
                .biasStops = options.autoExposureBiasStops,
            },
        },
    });
    renderer.SetDefaultSceneLightingConfig(kb::render::SceneRenderLightingConfig{
        .maxForwardLights = 4U,
        .ambientColor = {0.18F, 0.18F, 0.18F},
        .ambientIntensity = 0.45F,
        .environmentMode = kb::render::SceneRenderEnvironmentMode::Hemisphere,
        .environmentZenithColor = {0.82F, 0.86F, 0.92F},
        .environmentGroundColor = {0.12F, 0.12F, 0.14F},
        .environmentDiffuseIntensity = 0.55F,
        .environmentSpecularIntensity = 0.85F,
        .editorPreviewKeyLightEnabled = true,
        .editorPreviewKeyLightDirection = {-0.35F, -0.55F, -0.76F},
        .editorPreviewKeyLightColor = {1.0F, 0.96F, 0.90F},
        .editorPreviewKeyLightIntensity = 2.4F,
    });

    kb::scene::Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Smoke Camera",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{0.0F, 2.0F, -6.0F},
        },
    });
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{.primary = true});
    const kb::scene::SceneEntity keyLight = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Smoke Key Light",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{0.0F, 3.5F, -2.5F},
        },
    });
    scene.Components().Lights().Set(keyLight, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Directional,
        .color = kb::scene::Vec3{1.0F, 0.96F, 0.90F},
        .intensity = 1.8F,
        .castsShadow = false,
    });

    const std::filesystem::path assetRoot = std::filesystem::temp_directory_path() / "21kb_render_smoke_assets";
    std::error_code filesystemError;
    std::filesystem::remove_all(assetRoot, filesystemError);
    filesystemError.clear();
    std::filesystem::create_directories(assetRoot, filesystemError);
    if (filesystemError) {
        std::fprintf(stderr, "kb_render_smoke: asset temp directory failed\n");
        std::fflush(stderr);
        renderer.Shutdown();
        DestroyWindow(window);
        return EXIT_FAILURE;
    }

    WriteTriangleGltf(assetRoot);
    WriteTexture(assetRoot / "albedo.kbtex", 220U, 220U, 220U);
    WriteTexture(assetRoot / "normal_flat.kbtex", 128U, 128U, 255U);
    WriteTexture(assetRoot / "normal_tilt.kbtex", 255U, 128U, 128U);
    WriteTexture(assetRoot / "mr_rough_dielectric.kbtex", 255U, 230U, 0U);
    WriteTexture(assetRoot / "mr_smooth_dielectric.kbtex", 255U, 12U, 0U);
    WriteTexture(assetRoot / "mr_metallic.kbtex", 255U, 96U, 255U);
    kb::assets::AssetManager& assetManager = scene.Assets().Manager();
    static_cast<void>(assetManager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()));
    static_cast<void>(assetManager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));
    static_cast<void>(assetManager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()));
    if (!assetManager.Mounts().Mount("Smoke", assetRoot)) {
        std::fprintf(stderr, "kb_render_smoke: asset mount failed\n");
        std::fflush(stderr);
        renderer.Shutdown();
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    static_cast<void>(assetManager.DiscoverMountedAssets());
    const kb::assets::AssetMetadata* albedoMetadata = assetManager.Registry().FindByPath("/Smoke/albedo.kbtex");
    const kb::assets::AssetMetadata* flatNormalMetadata = assetManager.Registry().FindByPath("/Smoke/normal_flat.kbtex");
    const kb::assets::AssetMetadata* tiltNormalMetadata = assetManager.Registry().FindByPath("/Smoke/normal_tilt.kbtex");
    const kb::assets::AssetMetadata* roughDielectricMetadata = assetManager.Registry().FindByPath("/Smoke/mr_rough_dielectric.kbtex");
    const kb::assets::AssetMetadata* smoothDielectricMetadata = assetManager.Registry().FindByPath("/Smoke/mr_smooth_dielectric.kbtex");
    const kb::assets::AssetMetadata* metallicMetadata = assetManager.Registry().FindByPath("/Smoke/mr_metallic.kbtex");
    if (albedoMetadata == nullptr ||
        flatNormalMetadata == nullptr ||
        tiltNormalMetadata == nullptr ||
        roughDielectricMetadata == nullptr ||
        smoothDielectricMetadata == nullptr ||
        metallicMetadata == nullptr) {
        std::fprintf(stderr, "kb_render_smoke: texture discovery failed\n");
        std::fflush(stderr);
        renderer.Shutdown();
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    WriteMaterial(assetRoot / "baseline.kbmat", albedoMetadata->id.value, flatNormalMetadata->id.value, roughDielectricMetadata->id.value);
    WriteMaterial(assetRoot / "roughness.kbmat", albedoMetadata->id.value, flatNormalMetadata->id.value, smoothDielectricMetadata->id.value);
    WriteMaterial(assetRoot / "metallic.kbmat", albedoMetadata->id.value, flatNormalMetadata->id.value, metallicMetadata->id.value);
    WriteMaterial(assetRoot / "normal.kbmat", albedoMetadata->id.value, tiltNormalMetadata->id.value, roughDielectricMetadata->id.value, 3.0F);
    static_cast<void>(assetManager.DiscoverMountedAssets());
    const kb::assets::AssetMetadata* meshMetadata = assetManager.Registry().FindByPath("/Smoke/triangle.gltf");
    const kb::assets::AssetMetadata* baselineMaterialMetadata = assetManager.Registry().FindByPath("/Smoke/baseline.kbmat");
    const kb::assets::AssetMetadata* roughnessMaterialMetadata = assetManager.Registry().FindByPath("/Smoke/roughness.kbmat");
    const kb::assets::AssetMetadata* metallicMaterialMetadata = assetManager.Registry().FindByPath("/Smoke/metallic.kbmat");
    const kb::assets::AssetMetadata* normalMaterialMetadata = assetManager.Registry().FindByPath("/Smoke/normal.kbmat");
    if (meshMetadata == nullptr ||
        baselineMaterialMetadata == nullptr ||
        roughnessMaterialMetadata == nullptr ||
        metallicMaterialMetadata == nullptr ||
        normalMaterialMetadata == nullptr) {
        std::fprintf(stderr, "kb_render_smoke: mesh/material discovery failed\n");
        std::fflush(stderr);
        renderer.Shutdown();
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    const std::array<std::uint64_t, kMaterialTextureVariantPhaseCount> materialVariantAssetIds{
        baselineMaterialMetadata->id.value,
        roughnessMaterialMetadata->id.value,
        metallicMaterialMetadata->id.value,
        normalMaterialMetadata->id.value,
    };

    const std::array<float, kMaterialTextureVariantPhaseCount> materialVariantPositions{ -2.1F, -0.7F, 0.7F, 2.1F };
    const std::array<const char*, kMaterialTextureVariantPhaseCount> materialVariantNames{
        "Smoke Baseline Mesh",
        "Smoke Roughness Mesh",
        "Smoke Metallic Mesh",
        "Smoke Normal Mesh",
    };
    for (std::size_t index = 0; index < materialVariantAssetIds.size(); ++index) {
        const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
            .name = materialVariantNames[index],
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{materialVariantPositions[index], 0.0F, 0.0F},
                .localScale = kb::scene::Vec3{0.9F, 1.0F, 0.9F},
            },
        });
        scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{.meshAssetId = meshMetadata->id.value, .materialAssetId = materialVariantAssetIds[index]});
    }

    kb::render::SceneRenderTarget sceneTarget;
    kb::render::ScenePostProcessTargets postProcessTargets;
    if (!sceneTarget.Ensure(kb::render::SceneRenderTargetDesc{
            .extent = kb::render::RenderExtent{kSmokeRenderWidth, kSmokeRenderHeight},
            .colorPolicy = kb::render::SceneColorFormatPolicy::Auto,
        })) {
        std::fprintf(stderr, "kb_render_smoke: SceneRenderTarget.Ensure failed\n");
        std::fflush(stderr);
        renderer.Shutdown();
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    if (!postProcessTargets.Ensure(kb::render::ScenePostProcessTargetsDesc{
            .extent = kb::render::RenderExtent{kSmokeRenderWidth, kSmokeRenderHeight},
            .colorPolicy = kb::render::SceneColorFormatPolicy::Auto,
        })) {
        std::fprintf(stderr, "kb_render_smoke: ScenePostProcessTargets.Ensure failed\n");
        std::fflush(stderr);
        sceneTarget.Shutdown();
        renderer.Shutdown();
        DestroyWindow(window);
        return EXIT_FAILURE;
    }
    std::fprintf(
        stdout,
        "kb_render_smoke: scene target color=%s status=%s depth=%s depth_status=%s post_color=%s post_status=%s\n",
        kb::render::SceneTextureFormatName(sceneTarget.ColorSelection().format),
        kb::render::SceneTargetFormatSelectionStatusName(sceneTarget.ColorSelection().status),
        kb::render::SceneTextureFormatName(sceneTarget.DepthSelection().format),
        kb::render::SceneTargetFormatSelectionStatusName(sceneTarget.DepthSelection().status),
        kb::render::SceneTextureFormatName(postProcessTargets.ColorSelection().format),
        kb::render::SceneTargetFormatSelectionStatusName(postProcessTargets.ColorSelection().status));
    std::fflush(stdout);

    std::uint32_t frameCount = 0;
    bool screenshotWritten = options.screenshotPath.empty();
    const bool validateMaterialTextureVariants = options.validateScreenshot && options.validateMaterialTextureVariants;
    bool running = true;
    while (running && frameCount < options.maxFrames) {
        if (options.exerciseWindowEvents) {
            ExerciseWindowEvent(window, frameCount);
        }

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!running) {
            break;
        }

        if (renderer.BeginFrame()) {
            const kb::render::RenderSceneSubmitDesc desc{
                .target = kb::render::RenderSceneTargetBinding{
                    .frameBuffer = sceneTarget.FrameBuffer(),
                    .colorTexture = sceneTarget.ColorTexture(),
                    .depthTexture = sceneTarget.DepthTexture(),
                    .viewport = kb::render::RenderViewportDesc{
                        .id = kb::render::RenderViewportId{1U},
                        .extent = kb::render::RenderExtent{kSmokeRenderWidth, kSmokeRenderHeight},
                        .viewportIndex = 0U,
                    },
                },
                .postProcess = postProcessTargets.Binding(),
                .finalComposite = kb::render::RenderFinalCompositeTargetBinding{
                    .frameBuffer = BGFX_INVALID_HANDLE,
                    .extent = kb::render::RenderExtent{kSmokeRenderWidth, kSmokeRenderHeight},
                    .enabled = true,
                },
                .clearRgba = 0x101018FFU,
            };
            if (!renderer.SubmitScene(scene, desc)) {
                std::fprintf(stderr, "kb_render_smoke: renderer.SubmitScene failed\n");
                std::fflush(stderr);
                renderer.EndFrame();
                postProcessTargets.Shutdown();
                sceneTarget.Shutdown();
                renderer.Shutdown();
                DestroyWindow(window);
                UnregisterClassW(kWindowClassName, instance);
                return EXIT_FAILURE;
            }
            const kb::render::SceneRenderSubmitStats sceneStats = renderer.LastSceneSubmitStats();
            if (sceneStats.visibleMeshCount < 2U ||
                sceneStats.submittedMeshCount < 2U ||
                sceneStats.submittedDrawGroupCount < 1U ||
                sceneStats.submittedDrawCallCount < 1U ||
                sceneStats.HasMissingResources() ||
                sceneStats.gpuDrivenDrawCandidateCount < 2U ||
                (!options.forceGpuDrivenCpuFallback && sceneStats.gpuDrivenFeatureState != kb::render::SceneGpuDrivenFeatureState::ComputeCulling) ||
                (options.forceGpuDrivenCpuFallback && sceneStats.gpuDrivenFeatureState != kb::render::SceneGpuDrivenFeatureState::CpuValidationOnly) ||
                (!options.forceGpuDrivenCpuFallback && sceneStats.gpuDrivenCounterSource != kb::render::SceneGpuDrivenCounterSource::GpuDispatchCounters) ||
                (options.forceGpuDrivenCpuFallback && sceneStats.gpuDrivenCounterSource != kb::render::SceneGpuDrivenCounterSource::CpuCandidates) ||
                (!options.forceGpuDrivenCpuFallback && sceneStats.gpuCullingDispatchCount < 3U) ||
                (options.forceGpuDrivenCpuFallback && sceneStats.gpuCullingDispatchCount != 0U) ||
                (!options.forceGpuDrivenCpuFallback && sceneStats.gpuDrivenInputInstanceCount < 2U) ||
                (!options.forceGpuDrivenCpuFallback && sceneStats.gpuDrivenUploadBytes == 0U) ||
                (!options.forceGpuDrivenCpuFallback && sceneStats.gpuDrivenBufferCapacity < sceneStats.gpuDrivenInputInstanceCount) ||
                (options.forceGpuDrivenCpuFallback && sceneStats.gpuDrivenUploadBytes != 0U) ||
                sceneStats.gpuDrivenParityValidationStatus != kb::render::SceneGpuDrivenParityValidationStatus::Valid ||
                (!options.forceGpuDrivenCpuFallback && sceneStats.gpuDrivenFallbackReason != kb::render::SceneGpuDrivenFallbackReason::IndirectDrawUnsupported) ||
                (options.forceGpuDrivenCpuFallback && sceneStats.gpuDrivenFallbackReason != kb::render::SceneGpuDrivenFallbackReason::RuntimeGpuDispatchUnavailable) ||
                (options.forceGpuDrivenCpuFallback && sceneStats.gpuDrivenParityValidationCount < 2U)) {
                std::fprintf(
                    stderr,
                    "kb_render_smoke: unexpected scene stats visible=%u submitted=%u groups=%u draws=%u missing=%u mesh_binding=%u mesh_resource=%u material_binding=%u material_resource=%u texture_binding=%u texture_resource=%u gpu_candidates=%u gpu_inputs=%u gpu_upload=%llu gpu_capacity=%u gpu_state=%u gpu_source=%u gpu_dispatches=%u gpu_fallback=%u gpu_parity=%u gpu_parity_records=%u\n",
                    sceneStats.visibleMeshCount,
                    sceneStats.submittedMeshCount,
                    sceneStats.submittedDrawGroupCount,
                    sceneStats.submittedDrawCallCount,
                    sceneStats.HasMissingResources() ? 1U : 0U,
                    sceneStats.missingMeshBindingCount,
                    sceneStats.missingMeshResourceCount,
                    sceneStats.missingMaterialBindingCount,
                    sceneStats.missingMaterialResourceCount,
                    sceneStats.missingTextureBindingCount,
                    sceneStats.missingTextureResourceCount,
                    sceneStats.gpuDrivenDrawCandidateCount,
                    sceneStats.gpuDrivenInputInstanceCount,
                    static_cast<unsigned long long>(sceneStats.gpuDrivenUploadBytes),
                    sceneStats.gpuDrivenBufferCapacity,
                    static_cast<std::uint32_t>(sceneStats.gpuDrivenFeatureState),
                    static_cast<std::uint32_t>(sceneStats.gpuDrivenCounterSource),
                    sceneStats.gpuCullingDispatchCount,
                    static_cast<std::uint32_t>(sceneStats.gpuDrivenFallbackReason),
                    static_cast<std::uint32_t>(sceneStats.gpuDrivenParityValidationStatus),
                    sceneStats.gpuDrivenParityValidationCount);
                std::fflush(stderr);
                renderer.EndFrame();
                postProcessTargets.Shutdown();
                sceneTarget.Shutdown();
                renderer.Shutdown();
                DestroyWindow(window);
                UnregisterClassW(kWindowClassName, instance);
                return EXIT_FAILURE;
            }
            renderer.EndFrame();
            ++frameCount;

            if (!screenshotWritten && frameCount >= kMaterialTextureVariantFrameInterval) {
                const bool shouldCaptureMaterialVariant = validateMaterialTextureVariants;
                const bool shouldCaptureSingleScreenshot = !validateMaterialTextureVariants;
                if (shouldCaptureMaterialVariant || shouldCaptureSingleScreenshot) {
                    CapturedScreenshot capture{};
                    if (!CaptureClientScreenshotBgra(window, capture)) {
                        std::fprintf(stderr, "kb_render_smoke: screenshot capture failed: %s\n", options.screenshotPath.c_str());
                        std::fflush(stderr);
                        postProcessTargets.Shutdown();
                        sceneTarget.Shutdown();
                        renderer.Shutdown();
                        DestroyWindow(window);
                        UnregisterClassW(kWindowClassName, instance);
                        return EXIT_FAILURE;
                    }
                    if (options.validateScreenshot) {
                        const ScreenshotValidationStats stats = AnalyzeBgraPixels(capture.bgraPixels);
                        if (!ScreenshotLooksRendered(stats)) {
                            std::fprintf(
                                stderr,
                                "kb_render_smoke: screenshot validation failed samples=%u bright=%u colors=%u luma=[%u,%u]\n",
                                stats.sampledPixelCount,
                                stats.brightPixelCount,
                                stats.distinctColorCount,
                                stats.minLuma,
                                stats.maxLuma);
                            std::fflush(stderr);
                            postProcessTargets.Shutdown();
                            sceneTarget.Shutdown();
                            renderer.Shutdown();
                            DestroyWindow(window);
                            UnregisterClassW(kWindowClassName, instance);
                            return EXIT_FAILURE;
                        }
                    }

                    if (validateMaterialTextureVariants) {
                        const std::array<CapturedScreenshot, kMaterialTextureVariantPhaseCount> materialVariantRegionCaptures = ExtractMaterialTextureVariantRegionCaptures(capture);
                        if (!ValidateMaterialTextureVariantCaptures(materialVariantRegionCaptures)) {
                            std::fprintf(stderr, "kb_render_smoke: material texture variant validation failed\n");
                            std::fflush(stderr);
                            postProcessTargets.Shutdown();
                            sceneTarget.Shutdown();
                            renderer.Shutdown();
                            DestroyWindow(window);
                            UnregisterClassW(kWindowClassName, instance);
                            return EXIT_FAILURE;
                        }
                        screenshotWritten = WriteScreenshotBmp(capture, options.screenshotPath.c_str());
                    } else {
                        screenshotWritten = WriteScreenshotBmp(capture, options.screenshotPath.c_str());
                    }

                    if (screenshotWritten) {
                        std::fprintf(stdout, "kb_render_smoke: screenshot captured: %s\n", options.screenshotPath.c_str());
                        std::fflush(stdout);
                    } else {
                        std::fprintf(stderr, "kb_render_smoke: screenshot write failed: %s\n", options.screenshotPath.c_str());
                        std::fflush(stderr);
                        postProcessTargets.Shutdown();
                        sceneTarget.Shutdown();
                        renderer.Shutdown();
                        DestroyWindow(window);
                        UnregisterClassW(kWindowClassName, instance);
                        return EXIT_FAILURE;
                    }
                }
            }
        }
    }

    if (!screenshotWritten) {
        std::fprintf(stderr, "kb_render_smoke: screenshot was not captured before shutdown\n");
        std::fflush(stderr);
        postProcessTargets.Shutdown();
        sceneTarget.Shutdown();
        renderer.Shutdown();
        DestroyWindow(window);
        UnregisterClassW(kWindowClassName, instance);
        return EXIT_FAILURE;
    }

    postProcessTargets.Shutdown();
    sceneTarget.Shutdown();
    renderer.Shutdown();
    std::fprintf(stdout, "kb_render_smoke: renderer shutdown\n");
    std::fflush(stdout);
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    std::fprintf(stdout, "kb_render_smoke: rendered %u frames\n", frameCount);
    std::fflush(stdout);
    return EXIT_SUCCESS;
}
