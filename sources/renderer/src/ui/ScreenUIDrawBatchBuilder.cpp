#include "private/ui/ScreenUIDrawBatchBuilder.hpp"

#include "engine/scene/SceneUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <ranges>

namespace kb::render {
namespace {

[[nodiscard]] std::array<float, 4> TintedColor(const kb::scene::SceneUIFrameElement& element,
                                               const kb::math::Color& color) noexcept {
    return {
        color.r * element.interactionTint.r,
        color.g * element.interactionTint.g,
        color.b * element.interactionTint.b,
        color.a * element.interactionTint.a,
    };
}

[[nodiscard]] bool Visible(const std::array<float, 4>& color) noexcept {
    return color[3] > 0.0F;
}

[[nodiscard]] ScreenUIRect Rect(const kb::math::Rect& rect) noexcept {
    return ScreenUIRect{rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
}

[[nodiscard]] float Width(const ScreenUIRect& rect) noexcept {
    return rect.right - rect.left;
}

[[nodiscard]] float Height(const ScreenUIRect& rect) noexcept {
    return rect.bottom - rect.top;
}

[[nodiscard]] std::array<float, 4> Edges(const kb::scene::UIEdges& edges, float scale) noexcept {
    return {edges.left * scale, edges.top * scale, edges.right * scale, edges.bottom * scale};
}

[[nodiscard]] std::array<float, 4> Corners(const kb::math::Vec4& corners, float scale) noexcept {
    return {corners.x * scale, corners.y * scale, corners.z * scale, corners.w * scale};
}

[[nodiscard]] bool SameRect(const ScreenUIRect& lhs, const ScreenUIRect& rhs) noexcept {
    return lhs == rhs;
}

[[nodiscard]] const ScreenUIImageBinding* FindImage(std::span<const ScreenUIImageBinding> images, std::uint64_t assetId,
                                                    ScreenUITextureSource source) noexcept {
    const auto found = std::ranges::find_if(images, [assetId, source](const ScreenUIImageBinding& image) {
        return image.assetId == assetId && image.source == source;
    });
    return found == images.end() ? nullptr : &*found;
}

[[nodiscard]] const ScreenUITextRun* FindTextRun(std::span<const ScreenUITextRun> runs,
                                                 const kb::scene::SceneUIFrameElement& element) noexcept {
    const auto found = std::ranges::find(runs, element.entity.Id(), &ScreenUITextRun::entity);
    return found == runs.end() ? nullptr : &*found;
}

[[nodiscard]] bool HasBorder(const ScreenUIDrawStyle& style) noexcept {
    return Visible(style.borderColor) &&
           std::ranges::any_of(style.borderWidths, [](float width) { return width > 0.0F; });
}

[[nodiscard]] ScreenUIDrawStyle BorderStyle(const kb::scene::SceneUIFrameElement& element,
                                            const kb::scene::UIBorder& border) noexcept {
    const float scale = std::max(element.canvasScale, 0.0F);
    return ScreenUIDrawStyle{
        .fillColor = TintedColor(element, border.backgroundColor),
        .borderColor = TintedColor(element, border.borderColor),
        .borderWidths = Edges(border.borderWidth, scale),
        .cornerRadii = Corners(border.cornerRadius, scale),
        .width = std::max(element.rect.width, 0.0F),
        .height = std::max(element.rect.height, 0.0F),
        .opacity = std::clamp(border.opacity * element.effectiveOpacity, 0.0F, 1.0F),
        .fragmentKind = ScreenUIFragmentKind::Shape,
    };
}

[[nodiscard]] ScreenUIDrawStyle ImageStyle(const kb::scene::SceneUIFrameElement& element, const kb::math::Color& tint,
                                           const ScreenUIRect& destination) noexcept {
    ScreenUIDrawStyle style{};
    style.fillColor = TintedColor(element, tint);
    if (element.border.has_value()) {
        style.cornerRadii = Corners(element.border->cornerRadius, std::max(element.canvasScale, 0.0F));
    }
    style.width = Width(destination);
    style.height = Height(destination);
    style.opacity = std::clamp(element.effectiveOpacity, 0.0F, 1.0F);
    style.fragmentKind = ScreenUIFragmentKind::Image;
    return style;
}

} // namespace

const ScreenUIDrawList& ScreenUIDrawBatchBuilder::Build(const kb::scene::SceneUIFrame& frame,
                                                        std::span<const ScreenUIImageBinding> images,
                                                        std::span<const ScreenUITextRun> textRuns) {
    drawList_.Clear();
    viewportWidth_ = static_cast<std::uint32_t>(std::max(frame.viewportSize.x, 0.0F));
    viewportHeight_ = static_cast<std::uint32_t>(std::max(frame.viewportSize.y, 0.0F));
    drawList_.vertices.reserve(frame.elements.size() * 4U);
    drawList_.indices.reserve(frame.elements.size() * 6U);
    drawList_.batches.reserve(frame.elements.size());
    for (const kb::scene::SceneUIFrameElement& element : frame.elements) {
        if (element.rect.width > 0.0F && element.rect.height > 0.0F && element.clipRect.width > 0.0F &&
            element.clipRect.height > 0.0F && element.effectiveOpacity > 0.0F) {
            AppendElement(element, images, textRuns);
        }
    }
    if (drawList_.maximumBackgroundBlur > 0.0F) {
        for (ScreenUIDrawBatch& batch : drawList_.batches) {
            if (batch.style.fragmentKind == ScreenUIFragmentKind::BackgroundBlur) {
                batch.style.opacity *=
                    std::clamp(batch.style.blurStrength / drawList_.maximumBackgroundBlur, 0.0F, 1.0F);
            }
        }
    }
    return drawList_;
}

void ScreenUIDrawBatchBuilder::AppendElement(const kb::scene::SceneUIFrameElement& element,
                                             std::span<const ScreenUIImageBinding> images,
                                             std::span<const ScreenUITextRun> textRuns) {
    if (element.mask.has_value() && !element.mask->showGraphic) {
        return;
    }
    const ScreenUIRect elementRect = Rect(element.rect);
    const kb::scene::UIBorder border = element.border.value_or(kb::scene::UIBorder{});
    ScreenUIDrawStyle baseStyle = BorderStyle(element, border);
    const bool hasImage = element.sprite.has_value() || element.image.has_value() || element.rawImage.has_value();
    const bool hasSurface = Visible(baseStyle.fillColor) || HasBorder(baseStyle) || hasImage;

    if (hasSurface && element.shadow.has_value() && element.shadow->color.a > 0.0F) {
        const float scale = std::max(element.canvasScale, 0.0F);
        const float spread = std::max(element.shadow->blur * scale, 0.0F);
        const ScreenUIRect shadowRect{
            elementRect.left + element.shadow->offset.x * scale - spread,
            elementRect.top + element.shadow->offset.y * scale - spread,
            elementRect.right + element.shadow->offset.x * scale + spread,
            elementRect.bottom + element.shadow->offset.y * scale + spread,
        };
        ScreenUIDrawStyle shadowStyle = baseStyle;
        shadowStyle.fillColor = TintedColor(element, element.shadow->color);
        shadowStyle.borderColor = {};
        shadowStyle.borderWidths = {};
        shadowStyle.width = Width(shadowRect);
        shadowStyle.height = Height(shadowRect);
        shadowStyle.opacity = std::clamp(element.effectiveOpacity, 0.0F, 1.0F);
        shadowStyle.feather = spread;
        AppendShape(element, shadowRect, shadowStyle);
    }

    if (element.backgroundBlur.has_value() && element.backgroundBlur->radius > 0.0F) {
        ScreenUIDrawStyle blurStyle = baseStyle;
        blurStyle.fillColor = TintedColor(element, kb::math::Color{1.0F, 1.0F, 1.0F, 1.0F});
        blurStyle.borderColor = {};
        blurStyle.borderWidths = {};
        blurStyle.opacity = std::clamp(element.effectiveOpacity, 0.0F, 1.0F);
        blurStyle.blurStrength = element.backgroundBlur->radius * std::max(element.canvasScale, 0.0F);
        blurStyle.fragmentKind = ScreenUIFragmentKind::BackgroundBlur;
        const float inverseWidth = viewportWidth_ == 0U ? 0.0F : 1.0F / static_cast<float>(viewportWidth_);
        const float inverseHeight = viewportHeight_ == 0U ? 0.0F : 1.0F / static_cast<float>(viewportHeight_);
        AppendQuad(element, elementRect, elementRect, elementRect.left * inverseWidth, elementRect.top * inverseHeight,
                   elementRect.right * inverseWidth, elementRect.bottom * inverseHeight,
                   ScreenUITextureKey{.source = ScreenUITextureSource::BackgroundBlur}, blurStyle);
        drawList_.requiresBackgroundBlur = true;
        drawList_.maximumBackgroundBlur = std::max(drawList_.maximumBackgroundBlur, blurStyle.blurStrength);
    }

    if (Visible(baseStyle.fillColor)) {
        ScreenUIDrawStyle backgroundStyle = baseStyle;
        backgroundStyle.borderColor = {};
        backgroundStyle.borderWidths = {};
        AppendShape(element, elementRect, backgroundStyle);
    }

    const auto appendImageAsset = [&](std::uint64_t assetId, ImageKind kind) {
        if (assetId == 0U) {
            ScreenUIDrawStyle style = baseStyle;
            const auto color = kind == ImageKind::Sprite ? element.sprite->color
                : kind == ImageKind::Image ? element.image->color : element.rawImage->color;
            style.fillColor = TintedColor(element, color);
            style.borderColor = {};
            style.borderWidths = {};
            AppendShape(element, elementRect, style);
            return;
        }
        const ScreenUITextureSource source = kind == ImageKind::RawImage ? ScreenUITextureSource::ImageAssetLinear
                                                                         : ScreenUITextureSource::ImageAssetSrgb;
        if (const ScreenUIImageBinding* binding = FindImage(images, assetId, source); binding != nullptr) {
            AppendImage(element, *binding, assetId, kind);
        }
    };
    if (element.sprite.has_value()) {
        appendImageAsset(element.sprite->spriteAssetId, ImageKind::Sprite);
    }
    if (element.image.has_value()) {
        appendImageAsset(element.image->imageAssetId, ImageKind::Image);
    }
    if (element.rawImage.has_value()) {
        appendImageAsset(element.rawImage->imageAssetId, ImageKind::RawImage);
    }

    if (element.slider || element.scrollbar || element.progressBar || element.toggle) {
        const float inset = std::min(4.0F * element.canvasScale,
            std::min(Width(elementRect), Height(elementRect)) * 0.2F);
        ScreenUIRect indicator{elementRect.left + inset, elementRect.top + inset,
            elementRect.right - inset, elementRect.bottom - inset};
        ScreenUIDrawStyle style = baseStyle;
        style.fillColor = TintedColor(element, border.borderColor);
        style.borderColor = {};
        style.borderWidths = {};
        const auto drawIndicator = [&](const ScreenUIRect& rect) {
            auto partStyle = style;
            partStyle.width = Width(rect);
            partStyle.height = Height(rect);
            AppendShape(element, rect, partStyle);
        };
        if (element.toggle) {
            if (element.toggle->toggled) drawIndicator(indicator);
        } else {
            float fraction = 0.0F;
            auto direction = kb::scene::UIAxisDirection::LeftToRight;
            if (element.slider) {
                const auto& slider = *element.slider;
                fraction = slider.maximum > slider.minimum
                    ? (slider.value - slider.minimum) / (slider.maximum - slider.minimum) : 0.0F;
                direction = slider.direction;
            } else if (element.progressBar) {
                const auto& progress = *element.progressBar;
                fraction = progress.maximum > progress.minimum
                    ? (progress.value - progress.minimum) / (progress.maximum - progress.minimum) : 0.0F;
            } else {
                fraction = element.scrollbar->value;
                direction = element.scrollbar->direction;
            }
            fraction = std::clamp(fraction, 0.0F, 1.0F);
            const bool vertical = direction == kb::scene::UIAxisDirection::BottomToTop ||
                direction == kb::scene::UIAxisDirection::TopToBottom;
            const bool reverse = direction == kb::scene::UIAxisDirection::RightToLeft ||
                direction == kb::scene::UIAxisDirection::BottomToTop;
            float start = 0.0F;
            float end = fraction;
            if (element.scrollbar) {
                start = fraction * (1.0F - element.scrollbar->size);
                end = start + element.scrollbar->size;
            }
            if (reverse) { const float oldStart = start; start = 1.0F - end; end = 1.0F - oldStart; }
            auto filled = indicator;
            if (vertical) {
                filled.top = std::lerp(indicator.top, indicator.bottom, start);
                filled.bottom = std::lerp(indicator.top, indicator.bottom, end);
            } else {
                filled.left = std::lerp(indicator.left, indicator.right, start);
                filled.right = std::lerp(indicator.left, indicator.right, end);
            }
            drawIndicator(filled);
            if (element.slider) {
                auto thumb = indicator;
                const float position = reverse ? 1.0F - fraction : fraction;
                if (vertical) {
                    const float size = std::min(Width(indicator), Height(indicator));
                    thumb.top = std::lerp(indicator.top, indicator.bottom - size, position);
                    thumb.bottom = thumb.top + size;
                } else {
                    const float size = std::min(Width(indicator), Height(indicator));
                    thumb.left = std::lerp(indicator.left, indicator.right - size, position);
                    thumb.right = thumb.left + size;
                }
                style.fillColor = TintedColor(element, kb::math::Color{});
                drawIndicator(thumb);
            }
        }
    }

    if (HasBorder(baseStyle)) {
        ScreenUIDrawStyle borderStyle = baseStyle;
        borderStyle.fillColor = {};
        AppendShape(element, elementRect, borderStyle);
    }

    if (element.text.has_value() && !kb::scene::UITextContent(*element.text).empty()) {
        if (const ScreenUITextRun* run = FindTextRun(textRuns, element); run != nullptr) {
            if (element.shadow.has_value() && element.shadow->color.a > 0.0F) {
                const float scale = std::max(element.canvasScale, 0.0F);
                AppendTextLayer(element, *run, TintedColor(element, element.shadow->color),
                                TintedColor(element, element.shadow->color), element.shadow->offset.x * scale,
                                element.shadow->offset.y * scale, std::max(element.shadow->blur * scale, 0.0F), false);
            }
            AppendText(element, *run);
            AppendTextCaret(element, *run);
        }
    }

    if (hasSurface && element.outline.has_value() && element.outline->color.a > 0.0F && element.outline->width > 0.0F) {
        const float width = element.outline->width * std::max(element.canvasScale, 0.0F);
        const ScreenUIRect outlineRect{elementRect.left - width, elementRect.top - width, elementRect.right + width,
                                       elementRect.bottom + width};
        ScreenUIDrawStyle outlineStyle = baseStyle;
        outlineStyle.fillColor = {};
        outlineStyle.borderColor = TintedColor(element, element.outline->color);
        outlineStyle.borderWidths = {width, width, width, width};
        for (float& radius : outlineStyle.cornerRadii) {
            radius += width;
        }
        outlineStyle.width = Width(outlineRect);
        outlineStyle.height = Height(outlineRect);
        outlineStyle.opacity = std::clamp(element.effectiveOpacity, 0.0F, 1.0F);
        AppendShape(element, outlineRect, outlineStyle);
    }
}

void ScreenUIDrawBatchBuilder::AppendShape(const kb::scene::SceneUIFrameElement& element, const ScreenUIRect& rect,
                                           const ScreenUIDrawStyle& style) {
    AppendQuad(element, rect, rect, 0.0F, 0.0F, 1.0F, 1.0F, {}, style);
}

void ScreenUIDrawBatchBuilder::AppendImage(const kb::scene::SceneUIFrameElement& element,
                                           const ScreenUIImageBinding& binding, std::uint64_t assetId, ImageKind kind) {
    if (binding.width == 0U || binding.height == 0U) {
        return;
    }
    ScreenUIRect destination = Rect(element.rect);
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
    kb::math::Color tint{};
    kb::scene::UIImageScaleMode scaleMode = kb::scene::UIImageScaleMode::Stretch;
    bool preserveAspect = false;
    kb::scene::UIEdges nineSlice{};
    if (kind == ImageKind::Image) {
        const kb::scene::UIImage& image = *element.image;
        u0 = image.uvRect.x;
        v0 = image.uvRect.y;
        u1 = image.uvRect.x + image.uvRect.width;
        v1 = image.uvRect.y + image.uvRect.height;
        tint = image.color;
        scaleMode = image.scaleMode;
        preserveAspect = image.preserveAspect;
        nineSlice = image.nineSlice;
    } else if (kind == ImageKind::RawImage) {
        u0 = element.rawImage->uvRect.x;
        v0 = element.rawImage->uvRect.y;
        u1 = element.rawImage->uvRect.x + element.rawImage->uvRect.width;
        v1 = element.rawImage->uvRect.y + element.rawImage->uvRect.height;
        tint = element.rawImage->color;
    } else {
        tint = element.sprite->color;
        preserveAspect = element.sprite->preserveAspect;
    }

    const float sourceWidth = std::max((u1 - u0) * static_cast<float>(binding.width), 1.0F);
    const float sourceHeight = std::max((v1 - v0) * static_cast<float>(binding.height), 1.0F);
    const float sourceAspect = sourceWidth / sourceHeight;
    const float destinationAspect = Width(destination) / Height(destination);
    const bool contain = scaleMode == kb::scene::UIImageScaleMode::Contain ||
                         (scaleMode == kb::scene::UIImageScaleMode::Stretch && preserveAspect);
    if (contain) {
        if (destinationAspect > sourceAspect) {
            const float width = Height(destination) * sourceAspect;
            const float inset = (Width(destination) - width) * 0.5F;
            destination.left += inset;
            destination.right -= inset;
        } else {
            const float height = Width(destination) / sourceAspect;
            const float inset = (Height(destination) - height) * 0.5F;
            destination.top += inset;
            destination.bottom -= inset;
        }
    } else if (scaleMode == kb::scene::UIImageScaleMode::Cover) {
        if (destinationAspect > sourceAspect) {
            const float inset = (v1 - v0) * (1.0F - sourceAspect / destinationAspect) * 0.5F;
            v0 += inset;
            v1 -= inset;
        } else {
            const float inset = (u1 - u0) * (1.0F - destinationAspect / sourceAspect) * 0.5F;
            u0 += inset;
            u1 -= inset;
        }
    }

    const ScreenUIDrawStyle style = ImageStyle(element, tint, destination);
    const ScreenUITextureKey texture{.source = binding.source, .assetId = assetId};
    const float scale = std::max(element.canvasScale, 0.0F);
    const float left = std::clamp(nineSlice.left * scale, 0.0F, Width(destination) * 0.5F);
    const float top = std::clamp(nineSlice.top * scale, 0.0F, Height(destination) * 0.5F);
    const float right = std::clamp(nineSlice.right * scale, 0.0F, Width(destination) * 0.5F);
    const float bottom = std::clamp(nineSlice.bottom * scale, 0.0F, Height(destination) * 0.5F);
    if (left <= 0.0F && top <= 0.0F && right <= 0.0F && bottom <= 0.0F) {
        AppendQuad(element, destination, destination, u0, v0, u1, v1, texture, style);
        return;
    }

    const std::array<float, 4> xs{destination.left, destination.left + left, destination.right - right,
                                  destination.right};
    const std::array<float, 4> ys{destination.top, destination.top + top, destination.bottom - bottom,
                                  destination.bottom};
    const std::array<float, 4> us{u0, u0 + nineSlice.left / static_cast<float>(binding.width),
                                  u1 - nineSlice.right / static_cast<float>(binding.width), u1};
    const std::array<float, 4> vs{v0, v0 + nineSlice.top / static_cast<float>(binding.height),
                                  v1 - nineSlice.bottom / static_cast<float>(binding.height), v1};
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            const ScreenUIRect cell{xs[column], ys[row], xs[column + 1U], ys[row + 1U]};
            if (cell.IsValid()) {
                AppendQuad(element, destination, cell, us[column], vs[row], us[column + 1U], vs[row + 1U], texture,
                           style);
            }
        }
    }
}

void ScreenUIDrawBatchBuilder::AppendText(const kb::scene::SceneUIFrameElement& element, const ScreenUITextRun& run) {
    const float outlineWidth =
        element.outline.has_value() ? std::max(element.outline->width * element.canvasScale, 0.0F) : 0.0F;
    const kb::math::Color outlineColor = element.outline.has_value() ? element.outline->color : element.text->color;
    AppendTextLayer(element, run, TintedColor(element, element.text->color), TintedColor(element, outlineColor), 0.0F,
                    0.0F, outlineWidth, true);
}

void ScreenUIDrawBatchBuilder::AppendTextLayer(const kb::scene::SceneUIFrameElement& element,
                                               const ScreenUITextRun& run, const std::array<float, 4>& color,
                                               const std::array<float, 4>& outlineColor, float offsetX, float offsetY,
                                               float outlineWidth, bool applyMarkupColor) {
    ScreenUIDrawStyle style{};
    style.fillColor = color;
    style.borderColor = outlineColor;
    style.opacity = std::clamp(element.effectiveOpacity, 0.0F, 1.0F);
    style.fragmentKind = ScreenUIFragmentKind::Font;
    style.fontOutlineWidth = outlineWidth;
    const ScreenUITextureKey texture{
        .source = ScreenUITextureSource::FontAtlas, .assetId = run.fontAssetId, .pixelSize = run.pixelSize};
    const float atlasWidth = static_cast<float>(std::max(run.atlasWidth, static_cast<std::uint16_t>(1U)));
    const float atlasHeight = static_cast<float>(std::max(run.atlasHeight, static_cast<std::uint16_t>(1U)));
    const ScreenUIRect fullRect = Rect(element.rect);
    for (const ScreenUIGlyphQuad& glyph : run.glyphs) {
        ScreenUIDrawStyle glyphStyle = style;
        if (applyMarkupColor) {
            for (std::size_t channel = 0U; channel < glyphStyle.fillColor.size(); ++channel) {
                glyphStyle.fillColor[channel] *= glyph.color[channel];
            }
        }
        const ScreenUIRect rect{glyph.left + offsetX - outlineWidth, glyph.top + offsetY - outlineWidth,
                                glyph.right + offsetX + outlineWidth, glyph.bottom + offsetY + outlineWidth};
        if (rect.IsValid()) {
            AppendQuad(element, fullRect, rect, glyph.u0 - outlineWidth / atlasWidth,
                       glyph.v0 - outlineWidth / atlasHeight, glyph.u1 + outlineWidth / atlasWidth,
                       glyph.v1 + outlineWidth / atlasHeight, texture, glyphStyle);
        }
    }
}

// The insertion point of the focused input field. Editing already tracked a byte offset and
// moved it with the arrow keys, but nothing ever drew it, so a shipped text field looked inert
// no matter what the player typed.
void ScreenUIDrawBatchBuilder::AppendTextCaret(const kb::scene::SceneUIFrameElement& element,
                                               const ScreenUITextRun& run) {
    if (!element.inputField.has_value() || !element.textCaretVisible) {
        return;
    }
    // Glyphs are laid out in source order, so the first one at or past the caret offset owns
    // the column the caret sits in front of. Past the last glyph the caret follows the pen.
    const ScreenUIGlyphQuad* atCaret = nullptr;
    const ScreenUIGlyphQuad* last = nullptr;
    for (const ScreenUIGlyphQuad& glyph : run.glyphs) {
        if (atCaret == nullptr && glyph.sourceOffset >= element.textCaretByteOffset) {
            atCaret = &glyph;
        }
        last = &glyph;
    }
    if (last == nullptr) {
        return;
    }
    const float caretX = atCaret != nullptr ? atCaret->left : last->advanceRight;
    const float width = std::max(1.0F, std::round(element.canvasScale));
    const ScreenUIRect caret{caretX, atCaret != nullptr ? atCaret->lineTop : last->lineTop, caretX + width,
                             atCaret != nullptr ? atCaret->lineBottom : last->lineBottom};
    if (!caret.IsValid()) {
        return;
    }
    ScreenUIDrawStyle style{};
    style.fillColor = TintedColor(element, element.text.has_value() ? element.text->color : kb::math::Color{});
    style.opacity = std::clamp(element.effectiveOpacity, 0.0F, 1.0F);
    AppendShape(element, caret, style);
}

void ScreenUIDrawBatchBuilder::AppendQuad(const kb::scene::SceneUIFrameElement& element, const ScreenUIRect& fullRect,
                                          const ScreenUIRect& quadRect, float u0, float v0, float u1, float v1,
                                          const ScreenUITextureKey& texture, const ScreenUIDrawStyle& style) {
    if (!fullRect.IsValid() || !quadRect.IsValid() || element.rect.width <= 0.0F || element.rect.height <= 0.0F) {
        return;
    }
    const auto transform = [&](float x, float y) noexcept {
        const float horizontal = (x - element.rect.x) / element.rect.width;
        const float vertical = (y - element.rect.y) / element.rect.height;
        const kb::math::Vec2 top{
            element.corners[0].x + (element.corners[1].x - element.corners[0].x) * horizontal,
            element.corners[0].y + (element.corners[1].y - element.corners[0].y) * horizontal,
        };
        const kb::math::Vec2 bottom{
            element.corners[3].x + (element.corners[2].x - element.corners[3].x) * horizontal,
            element.corners[3].y + (element.corners[2].y - element.corners[3].y) * horizontal,
        };
        return std::array<float, 2>{top.x + (bottom.x - top.x) * vertical, top.y + (bottom.y - top.y) * vertical};
    };

    const std::array<float, 2> topLeft = transform(quadRect.left, quadRect.top);
    const std::array<float, 2> topRight = transform(quadRect.right, quadRect.top);
    const std::array<float, 2> bottomRight = transform(quadRect.right, quadRect.bottom);
    const std::array<float, 2> bottomLeft = transform(quadRect.left, quadRect.bottom);
    const float localLeft = (quadRect.left - fullRect.left) / Width(fullRect);
    const float localTop = (quadRect.top - fullRect.top) / Height(fullRect);
    const float localRight = (quadRect.right - fullRect.left) / Width(fullRect);
    const float localBottom = (quadRect.bottom - fullRect.top) / Height(fullRect);
    const std::uint32_t firstVertex = static_cast<std::uint32_t>(drawList_.vertices.size());
    drawList_.vertices.insert(drawList_.vertices.end(),
                              {
                                  ScreenUIVertex{topLeft[0], topLeft[1], u0, v0, localLeft, localTop},
                                  ScreenUIVertex{topRight[0], topRight[1], u1, v0, localRight, localTop},
                                  ScreenUIVertex{bottomRight[0], bottomRight[1], u1, v1, localRight, localBottom},
                                  ScreenUIVertex{bottomLeft[0], bottomLeft[1], u0, v1, localLeft, localBottom},
                              });
    const std::uint32_t firstIndex = static_cast<std::uint32_t>(drawList_.indices.size());
    drawList_.indices.insert(drawList_.indices.end(), {firstVertex, firstVertex + 1U, firstVertex + 2U, firstVertex,
                                                       firstVertex + 2U, firstVertex + 3U});
    const ScreenUIRect clipRect = Rect(element.clipRect);
    const bool canMerge = !drawList_.batches.empty() && drawList_.batches.back().texture == texture &&
                          SameRect(drawList_.batches.back().clipRect, clipRect) &&
                          drawList_.batches.back().style == style &&
                          drawList_.batches.back().firstIndex + drawList_.batches.back().indexCount == firstIndex;
    if (canMerge) {
        drawList_.batches.back().indexCount += 6U;
    } else {
        drawList_.batches.push_back(ScreenUIDrawBatch{
            .texture = texture, .clipRect = clipRect, .style = style, .firstIndex = firstIndex, .indexCount = 6U});
    }
}

} // namespace kb::render
