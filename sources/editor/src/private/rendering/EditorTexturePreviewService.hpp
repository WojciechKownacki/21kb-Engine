#pragma once

#include "engine/assets/AssetMetadata.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace kb::editor {

#if defined(_WIN32)

struct EditorTexturePreviewImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> bgra;
};

struct EditorTexturePreviewScaledCacheStats {
    std::uint64_t hitCount = 0U;
    std::uint64_t missCount = 0U;
    std::size_t entryCount = 0U;
    std::size_t byteCount = 0U;
};

class EditorTexturePreviewService {
public:
    EditorTexturePreviewService() = delete;

    [[nodiscard]] static bool IsTextureAsset(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static const EditorTexturePreviewImage* PreviewFor(const kb::assets::AssetMetadata& metadata);
    // A plain image file (PNG, JPEG, BMP), read again when it is written anew.
    [[nodiscard]] static const EditorTexturePreviewImage* PreviewForFile(const std::filesystem::path& file);
    static void DrawContain(HDC dc, RECT target, const EditorTexturePreviewImage& image, bool border = true);
    [[nodiscard]] static EditorTexturePreviewScaledCacheStats ScaledCacheStats();
};

#endif

} // namespace kb::editor
