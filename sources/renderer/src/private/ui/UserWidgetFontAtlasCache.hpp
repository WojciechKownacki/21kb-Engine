#pragma once

#include "private/ui/UserWidgetDrawData.hpp"

#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "kb/render/resources/RenderHandles.hpp"

#include <bgfx/bgfx.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace kb::assets {
class AssetManager;
}

namespace kb::render {

class RenderResourceRegistry;

class UserWidgetFontAtlasCache {
  public:
    UserWidgetFontAtlasCache() = default;
    ~UserWidgetFontAtlasCache() = default;

    UserWidgetFontAtlasCache(const UserWidgetFontAtlasCache&) = delete;
    UserWidgetFontAtlasCache& operator=(const UserWidgetFontAtlasCache&) = delete;

    [[nodiscard]] std::span<const UserWidgetTextRun> Prepare(std::uint64_t sceneId, kb::assets::AssetManager& assets,
                                                             RenderResourceRegistry& resources,
                                                             const kb::scene::UIPresentationSnapshot& snapshot);
    [[nodiscard]] bgfx::TextureHandle Resolve(std::uint64_t sceneId, std::uint64_t fontAssetId, std::uint32_t pixelSize,
                                              const RenderResourceRegistry& resources) const noexcept;
    void ReleaseScene(std::uint64_t sceneId, RenderResourceRegistry& resources) noexcept;
    void Shutdown(RenderResourceRegistry& resources) noexcept;

    // Internal value types are public because the translation-unit-local layout
    // helpers operate on them; the containing cache itself remains renderer-private.
    struct FontKey {
        std::uint64_t sceneId = 0U;
        std::uint64_t assetId = 0U;
        std::uint32_t pixelSize = 0U;

        [[nodiscard]] bool operator==(const FontKey&) const noexcept = default;
    };

    struct FontKeyHash {
        [[nodiscard]] std::size_t operator()(FontKey key) const noexcept;
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
    };

    struct FontEntry {
        kb::assets::AssetHandle<kb::assets::ImportedAsset> asset;
        std::uint64_t loadGeneration = 0U;
        RenderTextureHandle texture{};
        std::uint16_t atlasWidth = 0U;
        std::uint16_t atlasHeight = 0U;
        std::uint16_t glyphPadding = 0U;
        float ascent = 0.0F;
        float descent = 0.0F;
        float lineGap = 0.0F;
        float scale = 0.0F;
        std::vector<std::uint32_t> codepoints;
        std::unordered_map<std::uint32_t, Glyph> glyphs;
    };

  private:
    [[nodiscard]] FontEntry* Ensure(const FontKey& key, std::span<const std::uint32_t> codepoints,
                                    std::uint16_t glyphPadding, kb::assets::AssetManager& assets,
                                    RenderResourceRegistry& resources);
    [[nodiscard]] bool Rebuild(const FontKey& key, FontEntry& entry, std::span<const std::uint32_t> codepoints,
                               std::uint16_t glyphPadding, RenderResourceRegistry& resources);
    void Layout(const kb::scene::UIPresentationItem& item, const FontKey& key, const FontEntry& entry,
                UserWidgetTextRun& run) const;

    std::unordered_map<FontKey, FontEntry, FontKeyHash> fonts_;
    std::vector<UserWidgetTextRun> textRuns_;
};

} // namespace kb::render
