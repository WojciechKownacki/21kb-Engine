#include "rendering/ProjectFilesAssetTileRenderer.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "rendering/EditorMeshThumbnailService.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/ProjectFilesAssetIconResolver.hpp"
#include "rendering/ProjectFilesAssetTileFrameRenderer.hpp"
#include "rendering/ProjectFilesAssetTileMetrics.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/ProjectFilesTileTextRenderer.hpp"
#include "rendering/gdi/ScopedBrush.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/gdi/ScopedPen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <limits>
#include <iterator>
#include <memory>
#include <string>

#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)

#include <optional>
#include <unordered_map>
#include <vector>

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;
using Frame = ProjectFilesAssetTileFrameRenderer;
using Metrics = ProjectFilesAssetTileMetrics;
using Text = ProjectFilesTileTextRenderer;

constexpr std::array<char, 8> kImportedAssetMagic{ '2', '1', 'K', 'B', 'A', 'S', 'T', '\0' };

struct ProjectFilesTextureThumbnailImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> bgra;
};

struct ProjectFilesMaterialPreviewImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> bgra;
};

struct ProjectFilesMaterialPreviewStyle {
    COLORREF baseColor = RGB(194, 168, 116);
    float roughness = 0.65F;
    bool loadedFromAsset = false;
};

struct ProjectFilesMaterialPreviewCacheEntry {
    std::uint64_t contentHash = 0U;
    std::filesystem::path physicalPath;
    ProjectFilesMaterialPreviewStyle style;
};

class ScopedComStream {
public:
    explicit ScopedComStream(IStream* stream) noexcept : stream_(stream) {}
    ~ScopedComStream() {
        if (stream_ != nullptr) {
            stream_->Release();
        }
    }

    ScopedComStream(const ScopedComStream&) = delete;
    ScopedComStream& operator=(const ScopedComStream&) = delete;

private:
    IStream* stream_ = nullptr;
};

[[nodiscard]] RECT ThumbnailRect(const RECT& tile, const ProjectFilesAssetTileVisualLayout& visual) noexcept {
    const int width = Draw::RectWidth(tile);
    const int availableHeight = std::max(1, static_cast<int>(visual.label.top - tile.top - 5));
    const int maximumSize = std::max(24, std::min(width - 6, availableHeight + 6));
    const int size = std::min(maximumSize, 128);
    const int left = tile.left + (width - size) / 2;
    const int top = tile.top + std::max(3, (availableHeight - size) / 2 + 3);
    return RECT{ left, top, left + size, top + size };
}

[[nodiscard]] RECT LargeIconRect(const RECT& tile, const ProjectFilesAssetTileVisualLayout& visual) noexcept {
    return ThumbnailRect(tile, visual);
}

[[nodiscard]] RECT TexturePreviewRect(const RECT& tile, const ProjectFilesAssetTileVisualLayout& visual) noexcept {
    RECT rect{ tile.left + 7, tile.top + 7, tile.right - 7, visual.label.top - 5 };
    if (rect.bottom <= rect.top + 8) {
        return ThumbnailRect(tile, visual);
    }
    return rect;
}

[[nodiscard]] RECT MaterialPreviewRect(const RECT& tile, const ProjectFilesAssetTileVisualLayout& visual) noexcept {
    RECT rect = ThumbnailRect(tile, visual);
    const int size = std::max(24, std::min(Draw::RectWidth(rect), Draw::RectHeight(rect)));
    const int centerX = (rect.left + rect.right) / 2;
    const int centerY = (rect.top + rect.bottom) / 2;
    return RECT{ centerX - size / 2, centerY - size / 2, centerX - size / 2 + size, centerY - size / 2 + size };
}

[[nodiscard]] int ToColorByte(float value) noexcept {
    return std::clamp(static_cast<int>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F)), 0, 255);
}

[[nodiscard]] COLORREF ToColorRef(float red, float green, float blue) noexcept {
    return RGB(ToColorByte(red), ToColorByte(green), ToColorByte(blue));
}

[[nodiscard]] ProjectFilesMaterialPreviewStyle MaterialPreviewStyleFromAsset(const kb::assets::AssetMetadata& metadata) {
    ProjectFilesMaterialPreviewStyle style{};
    if (metadata.type != "RenderMaterial" || metadata.physicalPath.empty()) {
        return style;
    }

    const std::optional<kb::render::RenderMaterialAssetData> material = kb::render::RenderMaterialAssetLoader::LoadMaterial(metadata.physicalPath);
    if (!material.has_value()) {
        return style;
    }

    style.baseColor = ToColorRef(material->desc.baseColor[0], material->desc.baseColor[1], material->desc.baseColor[2]);
    style.roughness = std::clamp(material->desc.roughnessFactor, 0.0F, 1.0F);
    style.loadedFromAsset = true;
    return style;
}

[[nodiscard]] std::uint16_t ReadLe16(const std::vector<std::uint8_t>& data, std::size_t offset) noexcept {
    if (offset + 1 >= data.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8U));
}

[[nodiscard]] std::uint32_t ReadLe32(const std::vector<std::uint8_t>& data, std::size_t offset) noexcept {
    if (offset + 3 >= data.size()) {
        return 0;
    }
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8U)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16U)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24U);
}

[[nodiscard]] bool SkipImportedAssetString(const std::vector<std::uint8_t>& data, std::size_t& offset) noexcept {
    if (offset + 4U > data.size()) {
        return false;
    }
    const std::uint32_t length = ReadLe32(data, offset);
    offset += 4U;
    if (length > data.size() - offset) {
        return false;
    }
    offset += length;
    return true;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadImportedTexturePayload(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> data(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (data.size() < 32U || !std::equal(kImportedAssetMagic.begin(), kImportedAssetMagic.end(), reinterpret_cast<const char*>(data.data()))) {
        return std::nullopt;
    }

    std::size_t offset = 32U;
    if (!SkipImportedAssetString(data, offset) || !SkipImportedAssetString(data, offset)) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(offset), data.end());
}

[[nodiscard]] std::optional<ProjectFilesTextureThumbnailImage> DecodeTga(const std::vector<std::uint8_t>& data) {
    if (data.size() < 18U) {
        return std::nullopt;
    }
    const std::uint8_t idLength = data[0];
    const std::uint8_t colorMapType = data[1];
    const std::uint8_t imageType = data[2];
    const bool trueColor = imageType == 2U || imageType == 10U;
    const bool grayscale = imageType == 3U || imageType == 11U;
    const bool rle = imageType == 10U || imageType == 11U;
    if (colorMapType != 0U || (!trueColor && !grayscale)) {
        return std::nullopt;
    }
    const int width = static_cast<int>(ReadLe16(data, 12U));
    const int height = static_cast<int>(ReadLe16(data, 14U));
    const int bitsPerPixel = static_cast<int>(data[16]);
    if (width <= 0 || height <= 0 || (bitsPerPixel != 8 && bitsPerPixel != 24 && bitsPerPixel != 32)) {
        return std::nullopt;
    }
    const std::size_t bytesPerPixel = static_cast<std::size_t>(bitsPerPixel / 8);
    const std::size_t pixelOffset = 18U + idLength;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t pixelBytes = pixelCount * bytesPerPixel;
    if (pixelOffset > data.size() || (!rle && pixelBytes > data.size() - pixelOffset)) {
        return std::nullopt;
    }

    auto readPixel = [&data, bytesPerPixel, bitsPerPixel](std::size_t source) -> std::optional<std::uint32_t> {
        if (source + bytesPerPixel > data.size()) {
            return std::nullopt;
        }
        std::uint8_t b = 0;
        std::uint8_t g = 0;
        std::uint8_t r = 0;
        std::uint8_t a = 255;
        if (bitsPerPixel == 8) {
            b = g = r = data[source];
        } else {
            b = data[source];
            g = data[source + 1U];
            r = data[source + 2U];
            if (bitsPerPixel == 32) {
                a = data[source + 3U];
            }
        }
        return static_cast<std::uint32_t>(b)
            | (static_cast<std::uint32_t>(g) << 8U)
            | (static_cast<std::uint32_t>(r) << 16U)
            | (static_cast<std::uint32_t>(a) << 24U);
    };

    std::vector<std::uint32_t> sourcePixels(pixelCount);
    if (rle) {
        std::size_t cursor = pixelOffset;
        std::size_t pixelIndex = 0;
        while (pixelIndex < pixelCount && cursor < data.size()) {
            const std::uint8_t packet = data[cursor++];
            const std::size_t runLength = static_cast<std::size_t>((packet & 0x7FU) + 1U);
            if (pixelIndex + runLength > pixelCount) {
                return std::nullopt;
            }
            if ((packet & 0x80U) != 0U) {
                const std::optional<std::uint32_t> pixel = readPixel(cursor);
                if (!pixel.has_value()) {
                    return std::nullopt;
                }
                cursor += bytesPerPixel;
                std::fill_n(sourcePixels.begin() + static_cast<std::ptrdiff_t>(pixelIndex), static_cast<std::ptrdiff_t>(runLength), *pixel);
                pixelIndex += runLength;
            } else {
                for (std::size_t index = 0; index < runLength; ++index) {
                    const std::optional<std::uint32_t> pixel = readPixel(cursor);
                    if (!pixel.has_value()) {
                        return std::nullopt;
                    }
                    cursor += bytesPerPixel;
                    sourcePixels[pixelIndex++] = *pixel;
                }
            }
        }
        if (pixelIndex != pixelCount) {
            return std::nullopt;
        }
    } else {
        for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
            const std::optional<std::uint32_t> pixel = readPixel(pixelOffset + pixelIndex * bytesPerPixel);
            if (!pixel.has_value()) {
                return std::nullopt;
            }
            sourcePixels[pixelIndex] = *pixel;
        }
    }

    const bool topOrigin = (data[17] & 0x20U) != 0U;
    ProjectFilesTextureThumbnailImage image{ .width = width, .height = height };
    image.bgra.resize(pixelCount);
    for (int y = 0; y < height; ++y) {
        const int sourceY = topOrigin ? y : height - 1 - y;
        for (int x = 0; x < width; ++x) {
            image.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                sourcePixels[static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
        }
    }
    return image;
}

[[nodiscard]] std::optional<ProjectFilesTextureThumbnailImage> DecodeWithGdiplus(const std::vector<std::uint8_t>& data) {
    if (data.empty() || data.size() > static_cast<std::size_t>(std::numeric_limits<UINT>::max())) {
        return std::nullopt;
    }

    HeroIconGdiplusRuntime::EnsureStarted();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (memory == nullptr) {
        return std::nullopt;
    }
    void* target = GlobalLock(memory);
    if (target == nullptr) {
        GlobalFree(memory);
        return std::nullopt;
    }
    std::memcpy(target, data.data(), data.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK || stream == nullptr) {
        GlobalFree(memory);
        return std::nullopt;
    }
    const ScopedComStream scopedStream(stream);

    std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromStream(stream));
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok || bitmap->GetWidth() == 0U || bitmap->GetHeight() == 0U) {
        return std::nullopt;
    }

    ProjectFilesTextureThumbnailImage image{ .width = static_cast<int>(bitmap->GetWidth()), .height = static_cast<int>(bitmap->GetHeight()) };
    image.bgra.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            Gdiplus::Color pixel;
            if (bitmap->GetPixel(x, y, &pixel) != Gdiplus::Ok) {
                return std::nullopt;
            }
            image.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)] =
                static_cast<std::uint32_t>(pixel.GetBlue())
                | (static_cast<std::uint32_t>(pixel.GetGreen()) << 8U)
                | (static_cast<std::uint32_t>(pixel.GetRed()) << 16U)
                | (static_cast<std::uint32_t>(pixel.GetAlpha()) << 24U);
        }
    }
    return image;
}

[[nodiscard]] std::optional<ProjectFilesTextureThumbnailImage> DecodeTexturePayload(const std::vector<std::uint8_t>& payload) {
    if (std::optional<ProjectFilesTextureThumbnailImage> image = DecodeTga(payload); image.has_value()) {
        return image;
    }
    return DecodeWithGdiplus(payload);
}

class ProjectFilesTextureThumbnailCache {
public:
    [[nodiscard]] const ProjectFilesTextureThumbnailImage* ThumbnailFor(const kb::assets::AssetMetadata& metadata) {
        if (!ProjectFilesAssetIconResolver::IsTexture(metadata)) {
            return nullptr;
        }

        const std::uint64_t key = metadata.id.value ^ (metadata.contentHash + 0x9e3779b97f4a7c15ULL + (metadata.id.value << 6U) + (metadata.id.value >> 2U));
        if (const auto found = images_.find(key); found != images_.end()) {
            return found->second.has_value() ? &*found->second : nullptr;
        }

        std::optional<ProjectFilesTextureThumbnailImage> image;
        if (std::optional<std::vector<std::uint8_t>> payload = ReadImportedTexturePayload(metadata.physicalPath); payload.has_value()) {
            image = DecodeTexturePayload(*payload);
        }
        auto [iter, inserted] = images_.emplace(key, std::move(image));
        static_cast<void>(inserted);
        return iter->second.has_value() ? &*iter->second : nullptr;
    }

private:
    std::unordered_map<std::uint64_t, std::optional<ProjectFilesTextureThumbnailImage>> images_;
};

[[nodiscard]] ProjectFilesTextureThumbnailCache& TextureThumbnailCache() {
    static ProjectFilesTextureThumbnailCache cache;
    return cache;
}

class ProjectFilesMaterialPreviewStyleCache {
public:
    [[nodiscard]] const ProjectFilesMaterialPreviewStyle* StyleFor(const kb::assets::AssetMetadata& metadata) {
        if (!ProjectFilesAssetIconResolver::IsMaterial(metadata)) {
            return nullptr;
        }

        const std::uint64_t key = CacheKey(metadata);
        if (const auto found = styles_.find(key); found != styles_.end()) {
            ProjectFilesMaterialPreviewCacheEntry& entry = found->second;
            if (entry.contentHash == metadata.contentHash && entry.physicalPath == metadata.physicalPath) {
                return &entry.style;
            }
            entry = ProjectFilesMaterialPreviewCacheEntry{
                .contentHash = metadata.contentHash,
                .physicalPath = metadata.physicalPath,
                .style = MaterialPreviewStyleFromAsset(metadata),
            };
            return &entry.style;
        }

        auto [iter, inserted] = styles_.emplace(
            key,
            ProjectFilesMaterialPreviewCacheEntry{
                .contentHash = metadata.contentHash,
                .physicalPath = metadata.physicalPath,
                .style = MaterialPreviewStyleFromAsset(metadata),
            });
        static_cast<void>(inserted);
        return &iter->second.style;
    }

private:
    [[nodiscard]] static std::uint64_t CacheKey(const kb::assets::AssetMetadata& metadata) {
        if (metadata.id.IsValid()) {
            return metadata.id.value;
        }
        return static_cast<std::uint64_t>(std::hash<std::string>{}(kb::assets::NormalizeAssetPath(metadata.virtualPath) + ":" + metadata.type));
    }

    std::unordered_map<std::uint64_t, ProjectFilesMaterialPreviewCacheEntry> styles_;
};

[[nodiscard]] ProjectFilesMaterialPreviewStyleCache& MaterialPreviewStyleCache() {
    static ProjectFilesMaterialPreviewStyleCache cache;
    return cache;
}

void DrawThumbnailBitmap(HDC dc, const RECT& target, const EditorMeshThumbnailImage& image) {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty() || target.right <= target.left || target.bottom <= target.top) {
        return;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const int oldMode = SetStretchBltMode(dc, HALFTONE);
    static_cast<void>(StretchDIBits(
        dc,
        target.left,
        target.top,
        Draw::RectWidth(target),
        Draw::RectHeight(target),
        0,
        0,
        image.width,
        image.height,
        image.bgra.data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY));
    SetStretchBltMode(dc, oldMode);

    ScopedPen border{ 1, RGB(52, 59, 68) };
    const ScopedGdiObject selectedPen(dc, border.handle);
    const ScopedGdiObject selectedBrush(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, target.left, target.top, target.right, target.bottom);
}

void DrawTextureThumbnailBitmap(HDC dc, const RECT& target, const ProjectFilesTextureThumbnailImage& image) {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty() || target.right <= target.left || target.bottom <= target.top) {
        return;
    }

    const int targetWidth = Draw::RectWidth(target);
    const int targetHeight = Draw::RectHeight(target);
    int sourceWidth = image.width;
    int sourceHeight = (targetHeight * image.width) / std::max(1, targetWidth);
    if (sourceHeight > image.height) {
        sourceHeight = image.height;
        sourceWidth = (targetWidth * image.height) / std::max(1, targetHeight);
    }
    sourceWidth = std::clamp(sourceWidth, 1, image.width);
    sourceHeight = std::clamp(sourceHeight, 1, image.height);
    const int sourceX = (image.width - sourceWidth) / 2;
    const int sourceY = (image.height - sourceHeight) / 2;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const int oldMode = SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    static_cast<void>(StretchDIBits(
        dc,
        target.left,
        target.top,
        targetWidth,
        targetHeight,
        sourceX,
        sourceY,
        sourceWidth,
        sourceHeight,
        image.bgra.data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY));
    SetStretchBltMode(dc, oldMode);

    ScopedPen border{ 1, RGB(52, 59, 68) };
    const ScopedGdiObject selectedPen(dc, border.handle);
    const ScopedGdiObject selectedBrush(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, target.left, target.top, target.right, target.bottom);
}

[[nodiscard]] float ColorChannel(COLORREF color, int shift) noexcept {
    return static_cast<float>((color >> shift) & 0xFF) / 255.0F;
}

[[nodiscard]] std::uint8_t ToByte(float value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F)), 0, 255));
}

[[nodiscard]] std::uint32_t PackBgra(float red, float green, float blue, float alpha = 1.0F) noexcept {
    return static_cast<std::uint32_t>(ToByte(blue))
        | (static_cast<std::uint32_t>(ToByte(green)) << 8U)
        | (static_cast<std::uint32_t>(ToByte(red)) << 16U)
        | (static_cast<std::uint32_t>(ToByte(alpha)) << 24U);
}

[[nodiscard]] std::uint32_t CompositeOver(std::uint32_t background, float red, float green, float blue, float alpha) noexcept {
    const float inv = 1.0F - alpha;
    const float bgB = static_cast<float>(background & 0xFFU) / 255.0F;
    const float bgG = static_cast<float>((background >> 8U) & 0xFFU) / 255.0F;
    const float bgR = static_cast<float>((background >> 16U) & 0xFFU) / 255.0F;
    return PackBgra(red * alpha + bgR * inv, green * alpha + bgG * inv, blue * alpha + bgB * inv);
}

[[nodiscard]] float SmoothCoverage(float signedDistance) noexcept {
    return std::clamp(signedDistance + 0.5F, 0.0F, 1.0F);
}

[[nodiscard]] ProjectFilesMaterialPreviewImage RenderMaterialPreviewImage(int width, int height, const ProjectFilesMaterialPreviewStyle& style, bool selected) {
    ProjectFilesMaterialPreviewImage image{ .width = width, .height = height };
    image.bgra.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    const COLORREF frameFill = selected ? RGB(31, 34, 39) : RGB(24, 27, 31);
    const COLORREF frameBorder = selected ? RGB(123, 143, 170) : RGB(48, 54, 62);
    const std::uint32_t fill = PackBgra(ColorChannel(frameFill, 0), ColorChannel(frameFill, 8), ColorChannel(frameFill, 16));
    const std::uint32_t border = PackBgra(ColorChannel(frameBorder, 0), ColorChannel(frameBorder, 8), ColorChannel(frameBorder, 16));
    std::fill(image.bgra.begin(), image.bgra.end(), fill);

    for (int x = 0; x < width; ++x) {
        image.bgra[static_cast<std::size_t>(x)] = border;
        image.bgra[static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = border;
    }
    for (int y = 0; y < height; ++y) {
        image.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)] = border;
        image.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(width - 1)] = border;
    }

    const COLORREF baseColor = style.loadedFromAsset ? style.baseColor : Draw::Blend(style.baseColor, RGB(232, 212, 170), 34);
    const float baseR = ColorChannel(baseColor, 0);
    const float baseG = ColorChannel(baseColor, 8);
    const float baseB = ColorChannel(baseColor, 16);
    const float radius = static_cast<float>(std::max(8, std::min(width - 14, height - 14))) * 0.5F;
    const float centerX = static_cast<float>(width) * 0.5F;
    const float centerY = static_cast<float>(height) * 0.5F;
    const float shadowCenterY = centerY + radius * 0.72F;
    const float shadowRx = radius * 0.78F;
    const float shadowRy = std::max(2.0F, radius * 0.18F);
    const float roughness = std::clamp(style.roughness, 0.0F, 1.0F);

    constexpr float lightX = -0.46F;
    constexpr float lightY = -0.62F;
    constexpr float lightZ = 0.63F;
    constexpr float halfX = -0.27F;
    constexpr float halfY = -0.36F;
    constexpr float halfZ = 0.89F;

    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const float px = static_cast<float>(x) + 0.5F;
            const float py = static_cast<float>(y) + 0.5F;

            const float shadowDx = (px - (centerX + radius * 0.10F)) / shadowRx;
            const float shadowDy = (py - shadowCenterY) / shadowRy;
            const float shadowDistance = shadowDx * shadowDx + shadowDy * shadowDy;
            if (shadowDistance < 1.0F) {
                const float shadowAlpha = std::pow(1.0F - shadowDistance, 1.35F) * 0.24F;
                image.bgra[index] = CompositeOver(image.bgra[index], 0.02F, 0.025F, 0.032F, shadowAlpha);
            }

            const float nx = (px - centerX) / radius;
            const float ny = (py - centerY) / radius;
            const float distance2 = nx * nx + ny * ny;
            if (distance2 > 1.08F) {
                continue;
            }

            const float distance = std::sqrt(distance2);
            const float coverage = SmoothCoverage((1.0F - distance) * radius);
            if (coverage <= 0.0F) {
                continue;
            }

            const float nz = std::sqrt(std::max(0.0F, 1.0F - distance2));
            const float diffuse = std::max(0.0F, nx * lightX + ny * lightY + nz * lightZ);
            const float lowerShade = 1.0F - std::max(0.0F, ny) * 0.28F;
            const float rim = std::pow(std::clamp(1.0F - nz, 0.0F, 1.0F), 1.85F) * (selected ? 0.28F : 0.18F);
            const float specPower = 72.0F - roughness * 54.0F;
            const float specular = std::pow(std::max(0.0F, nx * halfX + ny * halfY + nz * halfZ), specPower) * (0.62F - roughness * 0.38F);
            const float sheen = std::pow(std::max(0.0F, (-nx * 0.35F) + (-ny * 0.72F) + (nz * 0.60F)), 18.0F) * 0.10F;
            const float shade = (0.34F + diffuse * 0.66F) * lowerShade;

            float red = baseR * shade + rim * 0.28F + specular + sheen;
            float green = baseG * shade + rim * 0.28F + specular + sheen;
            float blue = baseB * shade + rim * 0.30F + specular + sheen;

            const float edgeDarken = std::clamp((distance - 0.78F) / 0.22F, 0.0F, 1.0F) * 0.22F;
            red *= 1.0F - edgeDarken;
            green *= 1.0F - edgeDarken;
            blue *= 1.0F - edgeDarken;

            image.bgra[index] = CompositeOver(image.bgra[index], red, green, blue, coverage);
        }
    }
    return image;
}

void DrawMaterialPreviewBall(HDC dc, const RECT& target, const ProjectFilesMaterialPreviewStyle& style, bool selected) {
    RECT frame = Draw::Inset(target, 2, 2);
    const int width = Draw::RectWidth(frame);
    const int height = Draw::RectHeight(frame);
    if (width <= 2 || height <= 2) {
        return;
    }

    const ProjectFilesMaterialPreviewImage image = RenderMaterialPreviewImage(width, height, style, selected);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    static_cast<void>(StretchDIBits(
        dc,
        frame.left,
        frame.top,
        width,
        height,
        0,
        0,
        image.width,
        image.height,
        image.bgra.data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY));
}

void DrawFolderTile(HDC dc, RECT tile, const EditorTheme& theme, const EditorAssetFolderRow& folder, bool highlighted, const EditorAssetBrowserState& state) {
    Frame::Paint(dc, tile, theme, highlighted, highlighted && state.IsSelectionFocused());
    const int namePoint = Metrics::NamePointSize(tile);
    const ProjectFilesAssetTileVisualLayout visual = Metrics::ResolveVisualLayout(tile);
    Draw::DrawIconWithShadow(dc, visual.icon, HeroIconKind::Folder, Draw::FolderColor(highlighted), 1);
    if (state.TextEditMode() == EditorAssetTextEditMode::RenameFolder && Draw::SameVirtualPath(folder.virtualPath, state.TextEditTargetFolder())) {
        Draw::DrawCenteredEditField(dc, visual.label, theme, state.TextEditValue());
    } else {
        Text::PaintWrapped(dc, visual.label, folder.name.c_str(), highlighted ? Draw::Color(theme.textPrimary) : Draw::Blend(Draw::Color(theme.textPrimary), Draw::Color(theme.textSecondary), 18), namePoint, FW_MEDIUM);
    }
}

void DrawAssetTile(HDC dc, RECT tile, const EditorTheme& theme, const EditorAssetItemRow& asset, const EditorAssetBrowserState& state, EditorMeshThumbnailService& meshThumbnails) {
    Frame::Paint(dc, tile, theme, asset.selected, asset.selected && state.IsSelectionFocused());
    const int namePoint = Metrics::NamePointSize(tile);
    const ProjectFilesAssetTileVisualLayout visual = Metrics::ResolveVisualLayout(tile);
    if (const ProjectFilesTextureThumbnailImage* texture = TextureThumbnailCache().ThumbnailFor(asset.metadata)) {
        DrawTextureThumbnailBitmap(dc, TexturePreviewRect(tile, visual), *texture);
    } else if (const ProjectFilesMaterialPreviewStyle* materialStyle = MaterialPreviewStyleCache().StyleFor(asset.metadata)) {
        DrawMaterialPreviewBall(dc, MaterialPreviewRect(tile, visual), *materialStyle, asset.selected);
    } else if (const EditorMeshThumbnailImage* thumbnail = meshThumbnails.PreviewFor(asset.metadata)) {
        DrawThumbnailBitmap(dc, ThumbnailRect(tile, visual), *thumbnail);
    } else {
        const ProjectFilesAssetIcon icon = ProjectFilesAssetIconResolver::Resolve(asset.metadata, asset.selected);
        Draw::DrawIconWithShadow(dc, LargeIconRect(tile, visual), icon.kind, icon.color, icon.strokeWidth);
    }
    if (state.TextEditMode() == EditorAssetTextEditMode::RenameAsset && state.TextEditTargetAsset() == asset.metadata.id) {
        Draw::DrawCenteredEditField(dc, visual.label, theme, state.TextEditValue());
    } else {
        Text::PaintWrapped(dc, visual.label, asset.metadata.name.c_str(), Draw::Color(theme.textPrimary), namePoint, FW_MEDIUM);
    }
}

void DrawNewFolderTile(HDC dc, RECT tile, const EditorTheme& theme, const EditorAssetBrowserState& state) {
    Frame::Paint(dc, tile, theme, true);
    const ProjectFilesAssetTileVisualLayout visual = Metrics::ResolveVisualLayout(tile);
    Draw::DrawIconWithShadow(dc, visual.icon, HeroIconKind::Folder, Draw::FolderColor(true), 1);
    Draw::DrawCenteredEditField(dc, visual.label, theme, state.TextEditValue());
}

[[nodiscard]] int TotalTileCount(const EditorAssetBrowserState& state, const std::vector<EditorAssetFolderRow>& folders, const std::vector<EditorAssetItemRow>& assets) noexcept {
    return static_cast<int>(folders.size() + assets.size() + (state.TextEditMode() == EditorAssetTextEditMode::NewFolder ? 1U : 0U));
}

void DrawScrollbar(HDC dc, const EditorAssetBrowserLayoutRects& layout, const EditorTheme& theme, const EditorAssetBrowserState& state, int contentHeight) {
    static_cast<void>(theme);
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    const int viewportHeight = static_cast<int>(viewport.bottom - viewport.top);
    if (contentHeight <= viewportHeight) {
        return;
    }
    const RECT track = EditorAssetBrowserLayout::AssetScrollbarTrackRect(layout);
    const RECT thumb = EditorAssetBrowserLayout::ScrollbarThumbRect(track, viewportHeight, contentHeight, state.ContentScrollOffset());
    GdiDrawing::DrawSharpFrame(dc, track, RGB(22, 24, 27), RGB(38, 42, 47));
    const COLORREF thumbColor = state.IsContentScrollbarDragging() ? RGB(104, 116, 130) : RGB(76, 86, 98);
    const COLORREF thumbBorder = state.IsContentScrollbarDragging() ? RGB(128, 142, 158) : RGB(94, 105, 118);
    GdiDrawing::DrawSharpFrame(dc, thumb, thumbColor, thumbBorder);
}

} // namespace

void ProjectFilesAssetTileRenderer::Paint(
    HDC dc,
    const EditorAssetBrowserLayoutRects& layout,
    const EditorTheme& theme,
    const EditorAssetBrowserState& state,
    EditorMeshThumbnailService& meshThumbnails,
    const std::vector<EditorAssetFolderRow>& folders,
    const std::vector<EditorAssetItemRow>& assets) {
    constexpr int tileGap = 5;
    const int columns = EditorAssetBrowserLayout::AssetTileColumnCount(layout, state.ThumbnailScale());
    const int stepY = EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) + tileGap;
    const int totalItems = TotalTileCount(state, folders, assets);
    const int totalRows = (totalItems + columns - 1) / columns;
    const int contentHeight = totalRows * stepY;
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    const int maxOffset = std::max(0, contentHeight - static_cast<int>(viewport.bottom - viewport.top));
    const int scroll = std::clamp(state.ContentScrollOffset(), 0, maxOffset);
    const int firstIndex = std::clamp((scroll / stepY) * columns, 0, totalItems);
    const int visibleRows = (static_cast<int>(viewport.bottom - viewport.top) / stepY) + 3;
    const int lastIndex = std::clamp(firstIndex + visibleRows * columns, 0, totalItems);

    SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
    for (int globalIndex = firstIndex; globalIndex < lastIndex; ++globalIndex) {
        RECT tile = EditorAssetBrowserLayout::AssetTileRect(layout, globalIndex, state.ThumbnailScale());
        OffsetRect(&tile, 0, -scroll);
        if (globalIndex < static_cast<int>(folders.size())) {
            const EditorAssetFolderRow& folder = folders[static_cast<std::size_t>(globalIndex)];
            const bool highlighted = folder.selected
                || (state.ContextMenuTargetKind() == EditorAssetContextTargetKind::Folder
                    && Draw::SameVirtualPath(state.ContextMenuTargetFolder(), folder.virtualPath));
            DrawFolderTile(dc, tile, theme, folder, highlighted, state);
            continue;
        }
        int relative = globalIndex - static_cast<int>(folders.size());
        if (state.TextEditMode() == EditorAssetTextEditMode::NewFolder) {
            if (relative == 0) {
                DrawNewFolderTile(dc, tile, theme, state);
                continue;
            }
            --relative;
        }
        if (relative >= 0 && relative < static_cast<int>(assets.size())) {
            DrawAssetTile(dc, tile, theme, assets[static_cast<std::size_t>(relative)], state, meshThumbnails);
        }
    }
    RestoreDC(dc, -1);
    DrawScrollbar(dc, layout, theme, state, contentHeight);
}

} // namespace kb::editor

#endif
