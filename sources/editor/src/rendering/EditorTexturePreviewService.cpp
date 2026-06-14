#include "rendering/EditorTexturePreviewService.hpp"

#if defined(_WIN32)
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/gdi/ScopedPen.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>

#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)

namespace kb::editor {
namespace {

constexpr std::array<char, 8> kImportedAssetMagic{ '2', '1', 'K', 'B', 'A', 'S', 'T', '\0' };

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

[[nodiscard]] std::uint16_t ReadLe16(const std::vector<std::uint8_t>& data, std::size_t offset) noexcept {
    return offset + 1U < data.size() ? static_cast<std::uint16_t>(data[offset] | (data[offset + 1U] << 8U)) : 0U;
}

[[nodiscard]] std::uint32_t ReadLe32(const std::vector<std::uint8_t>& data, std::size_t offset) noexcept {
    if (offset + 3U >= data.size()) {
        return 0U;
    }
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(data[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

[[nodiscard]] bool SkipString(const std::vector<std::uint8_t>& data, std::size_t& offset) noexcept {
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

[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadPayload(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (data.size() < 32U || !std::equal(kImportedAssetMagic.begin(), kImportedAssetMagic.end(), reinterpret_cast<const char*>(data.data()))) {
        return std::nullopt;
    }
    std::size_t offset = 32U;
    if (!SkipString(data, offset) || !SkipString(data, offset)) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(offset), data.end());
}

[[nodiscard]] std::optional<EditorTexturePreviewImage> DecodeTga(const std::vector<std::uint8_t>& data) {
    if (data.size() < 18U) {
        return std::nullopt;
    }
    const std::uint8_t imageType = data[2];
    const bool supported = imageType == 2U || imageType == 3U || imageType == 10U || imageType == 11U;
    const bool rle = imageType == 10U || imageType == 11U;
    if (data[1] != 0U || !supported) {
        return std::nullopt;
    }
    const int width = static_cast<int>(ReadLe16(data, 12U));
    const int height = static_cast<int>(ReadLe16(data, 14U));
    const int bitsPerPixel = static_cast<int>(data[16]);
    if (width <= 0 || height <= 0 || (bitsPerPixel != 8 && bitsPerPixel != 24 && bitsPerPixel != 32)) {
        return std::nullopt;
    }

    const std::size_t bytesPerPixel = static_cast<std::size_t>(bitsPerPixel / 8);
    const std::size_t pixelOffset = 18U + data[0];
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixelOffset > data.size() || (!rle && pixelCount * bytesPerPixel > data.size() - pixelOffset)) {
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
        return static_cast<std::uint32_t>(b) | (static_cast<std::uint32_t>(g) << 8U) | (static_cast<std::uint32_t>(r) << 16U) | (static_cast<std::uint32_t>(a) << 24U);
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
    EditorTexturePreviewImage image{ .width = width, .height = height };
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

[[nodiscard]] std::optional<EditorTexturePreviewImage> DecodeGdiplus(const std::vector<std::uint8_t>& data) {
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

    EditorTexturePreviewImage image{ .width = static_cast<int>(bitmap->GetWidth()), .height = static_cast<int>(bitmap->GetHeight()) };
    image.bgra.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            Gdiplus::Color pixel;
            if (bitmap->GetPixel(x, y, &pixel) != Gdiplus::Ok) {
                return std::nullopt;
            }
            image.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)] =
                static_cast<std::uint32_t>(pixel.GetBlue()) | (static_cast<std::uint32_t>(pixel.GetGreen()) << 8U) | (static_cast<std::uint32_t>(pixel.GetRed()) << 16U) | (static_cast<std::uint32_t>(pixel.GetAlpha()) << 24U);
        }
    }
    return image;
}

[[nodiscard]] std::optional<EditorTexturePreviewImage> Decode(const std::vector<std::uint8_t>& payload) {
    if (std::optional<EditorTexturePreviewImage> image = DecodeTga(payload); image.has_value()) {
        return image;
    }
    return DecodeGdiplus(payload);
}

class Cache {
public:
    [[nodiscard]] const EditorTexturePreviewImage* PreviewFor(const kb::assets::AssetMetadata& metadata) {
        if (!EditorTexturePreviewService::IsTextureAsset(metadata)) {
            return nullptr;
        }
        const std::uint64_t key = metadata.id.value ^ (metadata.contentHash + 0x9e3779b97f4a7c15ULL + (metadata.id.value << 6U) + (metadata.id.value >> 2U));
        if (const auto found = images_.find(key); found != images_.end()) {
            return found->second.has_value() ? &*found->second : nullptr;
        }
        std::optional<EditorTexturePreviewImage> image;
        if (std::optional<std::vector<std::uint8_t>> payload = ReadPayload(metadata.physicalPath); payload.has_value()) {
            image = Decode(*payload);
        }
        auto [iter, inserted] = images_.emplace(key, std::move(image));
        static_cast<void>(inserted);
        return iter->second.has_value() ? &*iter->second : nullptr;
    }

private:
    std::unordered_map<std::uint64_t, std::optional<EditorTexturePreviewImage>> images_;
};

[[nodiscard]] Cache& TextureCache() {
    static Cache cache;
    return cache;
}

} // namespace

bool EditorTexturePreviewService::IsTextureAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "Texture" || metadata.importCategory == "Texture";
}

const EditorTexturePreviewImage* EditorTexturePreviewService::PreviewFor(const kb::assets::AssetMetadata& metadata) {
    return TextureCache().PreviewFor(metadata);
}

void EditorTexturePreviewService::DrawContain(HDC dc, RECT target, const EditorTexturePreviewImage& image, bool border) {
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
    SetBrushOrgEx(dc, 0, 0, nullptr);
    static_cast<void>(StretchDIBits(
        dc,
        target.left,
        target.top,
        target.right - target.left,
        target.bottom - target.top,
        0,
        0,
        image.width,
        image.height,
        image.bgra.data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY));
    SetStretchBltMode(dc, oldMode);

    if (border) {
        ScopedPen borderPen{ 1, RGB(52, 59, 68) };
        const ScopedGdiObject selectedPen(dc, borderPen.handle);
        const ScopedGdiObject selectedBrush(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, target.left, target.top, target.right, target.bottom);
    }
}

} // namespace kb::editor

#endif
