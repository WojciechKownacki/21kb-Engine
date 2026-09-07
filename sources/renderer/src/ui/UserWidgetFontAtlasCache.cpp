#include "private/ui/UserWidgetFontAtlasCache.hpp"
#include "private/ui/UserWidgetFontPayloadValidator.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetImportTypes.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputText.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

namespace kb::render {
namespace {

constexpr std::uint32_t kReplacementCodepoint = 0xFFFDU;
constexpr std::uint16_t kInitialAtlasExtent = 256U;
constexpr std::uint16_t kMaximumAtlasExtent = 2048U;
constexpr std::uint16_t kMinimumGlyphPadding = 2U;

[[nodiscard]] std::vector<std::uint32_t> DecodeWellFormedUtf8(std::string_view text) {
    std::vector<char32_t> decoded(text.size());
    const kb::input::Utf8DecodeResult result = kb::input::DecodeUtf8(text, decoded);
    if (!result.wellFormed || result.truncated) {
        return {};
    }
    decoded.resize(result.codePointCount);
    std::vector<std::uint32_t> codepoints;
    codepoints.reserve(decoded.size());
    for (const char32_t codepoint : decoded) {
        codepoints.push_back(static_cast<std::uint32_t>(codepoint));
    }
    return codepoints;
}

[[nodiscard]] const UserWidgetFontAtlasCache::Glyph* FindGlyph(const UserWidgetFontAtlasCache::FontEntry& entry,
                                                               std::uint32_t codepoint) noexcept {
    auto found = entry.glyphs.find(codepoint);
    if (found == entry.glyphs.end()) {
        found = entry.glyphs.find(kReplacementCodepoint);
    }
    return found == entry.glyphs.end() ? nullptr : &found->second;
}

struct TextLine {
    std::vector<std::uint32_t> codepoints;
    float width = 0.0F;
};

[[nodiscard]] float MeasureCodepoints(const UserWidgetFontAtlasCache::FontEntry& entry,
                                      std::span<const std::uint32_t> codepoints) noexcept {
    float width = 0.0F;
    for (const std::uint32_t codepoint : codepoints) {
        const UserWidgetFontAtlasCache::Glyph* glyph = FindGlyph(entry, codepoint);
        if (glyph == nullptr) {
            continue;
        }
        width += glyph->advance;
    }
    return width;
}

} // namespace

std::size_t UserWidgetFontAtlasCache::FontKeyHash::operator()(FontKey key) const noexcept {
    std::size_t hash = static_cast<std::size_t>(key.sceneId ^ (key.sceneId >> 32U));
    hash ^= static_cast<std::size_t>(key.assetId ^ (key.assetId >> 32U)) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    hash ^= static_cast<std::size_t>(key.pixelSize) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::span<const UserWidgetTextRun>
UserWidgetFontAtlasCache::Prepare(std::uint64_t sceneId, kb::assets::AssetManager& assets,
                                  RenderResourceRegistry& resources,
                                  const kb::scene::UIPresentationSnapshot& snapshot) {
    textRuns_.clear();
    textRuns_.reserve(snapshot.items.size());
    for (const kb::scene::UIPresentationItem& item : snapshot.items) {
        if (!item.textStyle.has_value() || item.text.empty() || item.textStyle->fontAssetId == 0U) {
            continue;
        }
        const float scaledSize = item.textStyle->fontSize * std::max(item.canvasScale, 0.01F);
        const std::uint32_t pixelSize = static_cast<std::uint32_t>(std::clamp(std::lround(scaledSize), 4L, 256L));
        const FontKey key{
            .sceneId = sceneId,
            .assetId = item.textStyle->fontAssetId,
            .pixelSize = pixelSize,
        };
        std::vector<std::uint32_t> codepoints = DecodeWellFormedUtf8(item.text);
        if (codepoints.empty()) {
            continue;
        }
        codepoints.push_back(kReplacementCodepoint);
        const kb::scene::UIEffects effects = item.effects.value_or(kb::scene::UIEffects{});
        const float outline = effects.outlineEnabled ? effects.outlineWidth * item.canvasScale : 0.0F;
        const float shadow = effects.shadowEnabled ? effects.shadowBlur * item.canvasScale : 0.0F;
        const float requiredPadding =
            std::ceil(std::max(outline, shadow)) * 2.0F + static_cast<float>(kMinimumGlyphPadding);
        if (requiredPadding > static_cast<float>(std::numeric_limits<std::uint16_t>::max())) {
            continue;
        }
        FontEntry* entry = Ensure(key, codepoints, static_cast<std::uint16_t>(requiredPadding), assets, resources);
        if (entry == nullptr) {
            continue;
        }
        UserWidgetTextRun run{
            .ownerId = item.owner.Id(),
            .elementId = item.elementId,
            .fontAssetId = key.assetId,
            .pixelSize = key.pixelSize,
            .atlasWidth = entry->atlasWidth,
            .atlasHeight = entry->atlasHeight,
        };
        Layout(item, key, *entry, run);
        textRuns_.push_back(std::move(run));
    }
    return textRuns_;
}

bgfx::TextureHandle UserWidgetFontAtlasCache::Resolve(std::uint64_t sceneId, std::uint64_t fontAssetId,
                                                      std::uint32_t pixelSize,
                                                      const RenderResourceRegistry& resources) const noexcept {
    const auto found = fonts_.find(FontKey{sceneId, fontAssetId, pixelSize});
    if (found == fonts_.end()) {
        return BGFX_INVALID_HANDLE;
    }
    const RenderTextureResource* texture = resources.FindTexture(found->second.texture);
    if (texture != nullptr) {
        return texture->texture;
    }
    return BGFX_INVALID_HANDLE;
}

void UserWidgetFontAtlasCache::ReleaseScene(std::uint64_t sceneId, RenderResourceRegistry& resources) noexcept {
    for (auto it = fonts_.begin(); it != fonts_.end();) {
        if (it->first.sceneId == sceneId) {
            resources.DestroyTexture(it->second.texture);
            it = fonts_.erase(it);
        } else {
            ++it;
        }
    }
}

void UserWidgetFontAtlasCache::Shutdown(RenderResourceRegistry& resources) noexcept {
    for (auto& [key, entry] : fonts_) {
        static_cast<void>(key);
        resources.DestroyTexture(entry.texture);
    }
    fonts_.clear();
    textRuns_.clear();
}

UserWidgetFontAtlasCache::FontEntry* UserWidgetFontAtlasCache::Ensure(const FontKey& key,
                                                                      std::span<const std::uint32_t> codepoints,
                                                                      std::uint16_t glyphPadding,
                                                                      kb::assets::AssetManager& assets,
                                                                      RenderResourceRegistry& resources) {
    FontEntry& entry = fonts_[key];
    const std::uint64_t generation = assets.LoadGeneration(kb::assets::AssetId{key.assetId});
    const bool reloaded = entry.loadGeneration != generation;
    if (!entry.asset.IsLoaded() || reloaded) {
        entry.asset = assets.Load<kb::assets::ImportedAsset>(kb::assets::AssetId{key.assetId});
        entry.loadGeneration = assets.LoadGeneration(kb::assets::AssetId{key.assetId});
        if (!entry.asset.IsLoaded() || !UserWidgetFontPayloadValidator::Validate(*entry.asset)) {
            if (entry.texture.IsValid()) {
                resources.DestroyTexture(entry.texture);
            }
            fonts_.erase(key);
            return nullptr;
        }
        entry.codepoints.clear();
    }

    std::vector<std::uint32_t> merged = entry.codepoints;
    merged.insert(merged.end(), codepoints.begin(), codepoints.end());
    std::ranges::sort(merged);
    merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
    if ((merged != entry.codepoints || glyphPadding > entry.glyphPadding) &&
        !Rebuild(key, entry, merged, std::max(glyphPadding, entry.glyphPadding), resources)) {
        if (entry.texture.IsValid()) {
            resources.DestroyTexture(entry.texture);
        }
        fonts_.erase(key);
        return nullptr;
    }
    return &entry;
}

bool UserWidgetFontAtlasCache::Rebuild(const FontKey& key, FontEntry& entry, std::span<const std::uint32_t> codepoints,
                                       std::uint16_t glyphPadding, RenderResourceRegistry& resources) {
    if (entry.asset->payload.empty()) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(entry.asset->payload.data());
    const int fontOffset = stbtt_GetFontOffsetForIndex(bytes, 0);
    stbtt_fontinfo font{};
    if (fontOffset < 0 || stbtt_InitFont(&font, bytes, fontOffset) == 0) {
        return false;
    }
    entry.scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(key.pixelSize));
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    entry.ascent = static_cast<float>(ascent) * entry.scale;
    entry.descent = static_cast<float>(descent) * entry.scale;
    entry.lineGap = static_cast<float>(lineGap) * entry.scale;

    std::uint32_t initialExtent = kInitialAtlasExtent;
    const std::uint64_t minimumExtent = static_cast<std::uint64_t>(glyphPadding) * 2U + key.pixelSize;
    while (initialExtent < minimumExtent && initialExtent < kMaximumAtlasExtent) {
        initialExtent *= 2U;
    }
    if (initialExtent > kMaximumAtlasExtent || initialExtent < minimumExtent) {
        return false;
    }
    std::uint16_t extent = static_cast<std::uint16_t>(initialExtent);
    std::unordered_map<std::uint32_t, Glyph> glyphs;
    std::vector<std::uint8_t> alpha;
    bool packed = false;
    while (!packed && extent <= kMaximumAtlasExtent) {
        alpha.assign(static_cast<std::size_t>(extent) * extent, 0U);
        glyphs.clear();
        const int padding = static_cast<int>(glyphPadding);
        int x = padding;
        int y = padding;
        int rowHeight = 0;
        packed = true;
        for (const std::uint32_t codepoint : codepoints) {
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            stbtt_GetCodepointBitmapBox(&font, static_cast<int>(codepoint), entry.scale, entry.scale, &x0, &y0, &x1,
                                        &y1);
            const int width = std::max(x1 - x0, 0);
            const int height = std::max(y1 - y0, 0);
            if (x + width + padding > extent) {
                x = padding;
                y += rowHeight + padding;
                rowHeight = 0;
            }
            if (y + height + padding > extent) {
                packed = false;
                break;
            }
            int advance = 0;
            int leftBearing = 0;
            stbtt_GetCodepointHMetrics(&font, static_cast<int>(codepoint), &advance, &leftBearing);
            static_cast<void>(leftBearing);
            if (width > 0 && height > 0) {
                stbtt_MakeCodepointBitmap(&font, alpha.data() + static_cast<std::size_t>(y) * extent + x, width, height,
                                          extent, entry.scale, entry.scale, static_cast<int>(codepoint));
            }
            glyphs.emplace(codepoint, Glyph{
                                          .codepoint = codepoint,
                                          .advance = static_cast<float>(advance) * entry.scale,
                                          .x0 = static_cast<float>(x0),
                                          .y0 = static_cast<float>(y0),
                                          .x1 = static_cast<float>(x1),
                                          .y1 = static_cast<float>(y1),
                                          .u0 = static_cast<float>(x) / static_cast<float>(extent),
                                          .v0 = static_cast<float>(y) / static_cast<float>(extent),
                                          .u1 = static_cast<float>(x + width) / static_cast<float>(extent),
                                          .v1 = static_cast<float>(y + height) / static_cast<float>(extent),
                                      });
            x += width + padding;
            rowHeight = std::max(rowHeight, height);
        }
        if (!packed) {
            extent = static_cast<std::uint16_t>(extent * 2U);
        }
    }
    if (!packed || extent > kMaximumAtlasExtent) {
        return false;
    }

    std::vector<std::uint8_t> rgba(alpha.size() * 4U, 255U);
    for (std::size_t index = 0U; index < alpha.size(); ++index) {
        rgba[index * 4U + 3U] = alpha[index];
    }
    const bgfx::Memory* memory = bgfx::copy(rgba.data(), static_cast<std::uint32_t>(rgba.size()));
    const RenderTextureHandle newTexture = resources.RegisterTexture(RenderTextureDesc{
        .width = extent,
        .height = extent,
        .format = bgfx::TextureFormat::RGBA8,
        .flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        .memory = memory,
        .colorSpace = RenderTextureColorSpace::Linear,
    });
    if (!newTexture.IsValid()) {
        return false;
    }
    if (entry.texture.IsValid()) {
        resources.DestroyTexture(entry.texture);
    }
    entry.texture = newTexture;
    entry.atlasWidth = extent;
    entry.atlasHeight = extent;
    entry.glyphPadding = glyphPadding;
    entry.codepoints.assign(codepoints.begin(), codepoints.end());
    entry.glyphs = std::move(glyphs);
    return true;
}

void UserWidgetFontAtlasCache::Layout(const kb::scene::UIPresentationItem& item, const FontKey& key,
                                      const FontEntry& entry, UserWidgetTextRun& run) const {
    static_cast<void>(key);
    const std::vector<std::uint32_t> codepoints = DecodeWellFormedUtf8(item.text);
    std::vector<TextLine> lines(1U);
    const float availableWidth = item.rect.Width();
    const kb::scene::UITextWrapMode wrap = item.textStyle->wrapMode;
    for (const std::uint32_t codepoint : codepoints) {
        if (codepoint == '\n') {
            lines.emplace_back();
            continue;
        }
        const Glyph* glyph = FindGlyph(entry, codepoint);
        if (glyph == nullptr) {
            continue;
        }
        const float addition = glyph->advance;
        const bool canWrap = wrap != kb::scene::UITextWrapMode::NoWrap;
        if (canWrap && !lines.back().codepoints.empty() && lines.back().width + addition > availableWidth) {
            lines.emplace_back();
        }
        lines.back().codepoints.push_back(codepoint);
        lines.back().width += glyph->advance;
    }
    if (wrap == kb::scene::UITextWrapMode::Word && lines.size() > 1U) {
        // Character wrapping above guarantees bounds for long words. Move a split
        // suffix to the following line when a whitespace boundary is available.
        for (std::size_t lineIndex = 0U; lineIndex + 1U < lines.size(); ++lineIndex) {
            TextLine& line = lines[lineIndex];
            const auto boundary =
                std::find(line.codepoints.rbegin(), line.codepoints.rend(), static_cast<std::uint32_t>(' '));
            if (boundary == line.codepoints.rend()) {
                continue;
            }
            const std::size_t split = static_cast<std::size_t>(std::distance(line.codepoints.begin(), boundary.base()));
            std::vector<std::uint32_t> suffix(line.codepoints.begin() + static_cast<std::ptrdiff_t>(split),
                                              line.codepoints.end());
            line.codepoints.erase(line.codepoints.begin() + static_cast<std::ptrdiff_t>(split), line.codepoints.end());
            while (!suffix.empty() && suffix.front() == static_cast<std::uint32_t>(' ')) {
                suffix.erase(suffix.begin());
            }
            suffix.insert(suffix.end(), lines[lineIndex + 1U].codepoints.begin(),
                          lines[lineIndex + 1U].codepoints.end());
            lines[lineIndex + 1U].codepoints = std::move(suffix);
            line.width = MeasureCodepoints(entry, line.codepoints);
            lines[lineIndex + 1U].width = MeasureCodepoints(entry, lines[lineIndex + 1U].codepoints);
        }
    }

    const float lineHeight = std::max(entry.ascent - entry.descent + entry.lineGap, 1.0F);
    const float textHeight = lineHeight * static_cast<float>(lines.size());
    float top = item.rect.top;
    if (item.textStyle->verticalAlignment == kb::scene::UITextVerticalAlignment::Center) {
        top += (item.rect.Height() - textHeight) * 0.5F;
    } else if (item.textStyle->verticalAlignment == kb::scene::UITextVerticalAlignment::Bottom) {
        top += item.rect.Height() - textHeight;
    }

    run.glyphs.clear();
    run.glyphs.reserve(codepoints.size());
    for (std::size_t lineIndex = 0U; lineIndex < lines.size(); ++lineIndex) {
        const TextLine& line = lines[lineIndex];
        float x = item.rect.left;
        if (item.textStyle->horizontalAlignment == kb::scene::UITextHorizontalAlignment::Center) {
            x += (item.rect.Width() - line.width) * 0.5F;
        } else if (item.textStyle->horizontalAlignment == kb::scene::UITextHorizontalAlignment::Right) {
            x += item.rect.Width() - line.width;
        }
        const float baseline = top + static_cast<float>(lineIndex) * lineHeight + entry.ascent;
        for (const std::uint32_t codepoint : line.codepoints) {
            const Glyph* glyph = FindGlyph(entry, codepoint);
            if (glyph == nullptr) {
                continue;
            }
            run.glyphs.push_back(UserWidgetGlyphQuad{
                .left = x + glyph->x0,
                .top = baseline + glyph->y0,
                .right = x + glyph->x1,
                .bottom = baseline + glyph->y1,
                .u0 = glyph->u0,
                .v0 = glyph->v0,
                .u1 = glyph->u1,
                .v1 = glyph->v1,
            });
            x += glyph->advance;
        }
    }
}

} // namespace kb::render
