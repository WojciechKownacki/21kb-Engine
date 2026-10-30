#pragma once

#include "private/ui/ScreenUIDrawData.hpp"
#include "private/ui/ScreenUITextMarkupParser.hpp"

#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "engine/ui/visual/UITextShaping.hpp"
#include "kb/render/resources/RenderHandles.hpp"

#include <bgfx/bgfx.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kb::assets {
class AssetManager;
}

namespace kb::scene {
struct SceneUIFrame;
struct SceneUIFrameElement;
} // namespace kb::scene

namespace kb::render {

class RenderResourceRegistry;

// The runs that are drawable this frame, plus the first text element that could not be
// prepared. A text element the atlas cannot serve loses its text; it must not take the rest
// of the UI - rectangles, images and borders - down with it. `failureReason` is null exactly
// when every authored text run prepared.
struct ScreenUIFontPreparation {
    std::span<const ScreenUITextRun> runs;
    std::uint64_t failedEntity = 0U;
    const char* failureReason = nullptr;
};

class ScreenUIFontAtlasCache {
  public:
    ScreenUIFontAtlasCache() = default;
    ~ScreenUIFontAtlasCache() = default;

    ScreenUIFontAtlasCache(const ScreenUIFontAtlasCache&) = delete;
    ScreenUIFontAtlasCache& operator=(const ScreenUIFontAtlasCache&) = delete;

    [[nodiscard]] ScreenUIFontPreparation Prepare(std::uint64_t sceneId, kb::assets::AssetManager& assets,
                                                  RenderResourceRegistry& resources,
                                                  const kb::scene::SceneUIFrame& frame, bool distanceFieldAvailable = true);
    [[nodiscard]] bgfx::TextureHandle Resolve(std::uint64_t sceneId, std::uint64_t fontAssetId, std::uint32_t pixelSize,
                                              std::string_view variations, bool distanceField, const RenderResourceRegistry& resources) const noexcept;
    void ReleaseScene(std::uint64_t sceneId, RenderResourceRegistry& resources) noexcept;
    void Shutdown(RenderResourceRegistry& resources) noexcept;

    struct TypographyCacheStats {
        std::size_t entries = 0U;
        std::size_t payloadBytes = 0U;
        std::uint64_t hits = 0U;
        std::uint64_t misses = 0U;
    };
    [[nodiscard]] TypographyCacheStats TextCacheStats() const noexcept;

    struct FontKey {
        std::uint64_t sceneId = 0U;
        std::uint64_t assetId = 0U;
        std::uint32_t pixelSize = 0U;
        std::string variations;
        bool distanceField = false;

        [[nodiscard]] bool operator==(const FontKey&) const noexcept = default;
    };

    struct FontKeyHash {
        [[nodiscard]] std::size_t operator()(const FontKey& key) const noexcept;
    };

    struct Glyph {
        std::uint32_t codepoint = 0U;
        float advance = 0.0F;
        float x0 = 0.0F;
        float y0 = 0.0F;
        float x1 = 0.0F;
        float y1 = 0.0F;
        float u0 = 0.0F;
        float v0 = 0.0F;
        float u1 = 0.0F;
        float v1 = 0.0F;
        bool distanceField = false;
    };

    struct FontEntry {
        struct ShapingEntry {
            std::vector<std::uint32_t> text;
            std::vector<std::uint32_t> styles;
            std::vector<kb::scene::UITextFontShapedGlyph> glyphs;
            std::size_t begin = 0U;
            std::size_t end = 0U;
            float scale = 0.0F;
            std::uint64_t fallbackAssetId = 0U;
            std::uint64_t fallbackGeneration = 0U;
            float fallbackScale = 0.0F;
            std::string language;
            std::size_t payloadBytes = 0U;
        };
        kb::assets::AssetHandle<kb::assets::ImportedAsset> asset;
        std::uint64_t assetId = 0U;
        std::uint64_t loadGeneration = 0U;
        RenderTextureHandle texture{};
        std::uint16_t atlasWidth = 0U;
        std::uint16_t atlasHeight = 0U;
        std::uint16_t glyphPadding = 0U;
        float ascent = 0.0F;
        float descent = 0.0F;
        float lineGap = 0.0F;
        float scale = 0.0F;
        std::vector<kb::scene::UITextFontVariation> variations;
        bool variationsParsed = false;
        std::string language;
        std::uint64_t lastUsedFrame = 0U;
        std::uint64_t lastCompactedFrame = 0U;
        std::vector<std::uint32_t> codepoints;
        std::vector<std::uint32_t> glyphIds;
        std::unordered_map<std::uint32_t, std::uint32_t> nominalGlyphs;
        std::unordered_map<std::uint32_t, Glyph> glyphs;
        // Per font/size/generation: at most 32 results and 64 KiB of vector payload.
        mutable std::vector<ShapingEntry> shapingCache;
        mutable std::size_t shapingBytes = 0U;
        mutable std::uint64_t shapingHits = 0U;
        mutable std::uint64_t shapingMisses = 0U;
    };

  private:
    [[nodiscard]] FontEntry* Ensure(const FontKey& key, std::span<const std::uint32_t> codepoints,
                                    std::uint16_t glyphPadding, kb::assets::AssetManager& assets,
                                    RenderResourceRegistry& resources);
    [[nodiscard]] bool Rebuild(const FontKey& key, FontEntry& entry, std::span<const std::uint32_t> codepoints,
                               std::uint16_t glyphPadding, RenderResourceRegistry& resources, bool forceBitmap = false);
    [[nodiscard]] bool Layout(const kb::scene::SceneUIFrameElement& element, const FontKey& key, FontEntry& entry,
                              const FontKey& fallbackKey, FontEntry* fallback,
                              const ScreenUITextMarkup& markup, ScreenUITextRun& run,
                              RenderResourceRegistry& resources);
    // Whether the text, laid out with `entry`'s metrics scaled by `factor`, fits the element's box and line
    // limit - the test auto size shrinks the font against.
    [[nodiscard]] static bool Fits(const kb::scene::SceneUIFrameElement& element, const FontEntry& entry, const FontEntry* fallback,
                                   const ScreenUITextMarkup& markup, float factor);

    std::unordered_map<FontKey, FontEntry, FontKeyHash> fonts_;
    std::vector<ScreenUITextRun> textRuns_;
    std::uint64_t prepareFrame_ = 0U;
};

} // namespace kb::render
