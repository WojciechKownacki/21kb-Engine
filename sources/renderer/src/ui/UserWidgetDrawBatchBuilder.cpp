#include "private/ui/UserWidgetDrawBatchBuilder.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] std::array<float, 4> Color(const kb::math::Color& color) noexcept {
    return {color.r, color.g, color.b, color.a};
}

[[nodiscard]] bool Visible(const std::array<float, 4>& color) noexcept {
    return color[3] > 0.0F;
}

[[nodiscard]] std::array<float, 4> Edges(const kb::scene::UIEdges& edges, float scale) noexcept {
    return {edges.left * scale, edges.top * scale, edges.right * scale, edges.bottom * scale};
}

[[nodiscard]] std::array<float, 4> Corners(const kb::math::Vec4& corners, float scale) noexcept {
    return {corners.x * scale, corners.y * scale, corners.z * scale, corners.w * scale};
}

[[nodiscard]] bool SameRect(const kb::scene::UIPresentationRect& lhs,
                            const kb::scene::UIPresentationRect& rhs) noexcept {
    return lhs.left == rhs.left && lhs.top == rhs.top && lhs.right == rhs.right && lhs.bottom == rhs.bottom;
}

[[nodiscard]] const UserWidgetImageBinding* FindImage(std::span<const UserWidgetImageBinding> images,
                                                      std::uint64_t assetId) noexcept {
    const auto found = std::ranges::find(images, assetId, &UserWidgetImageBinding::assetId);
    return found == images.end() ? nullptr : &*found;
}

[[nodiscard]] const UserWidgetTextRun* FindTextRun(std::span<const UserWidgetTextRun> runs,
                                                   const kb::scene::UIPresentationItem& item) noexcept {
    const auto found = std::ranges::find_if(runs, [&item](const UserWidgetTextRun& run) {
        return run.ownerId == item.owner.Id() && run.elementId == item.elementId;
    });
    return found == runs.end() ? nullptr : &*found;
}

[[nodiscard]] UserWidgetDrawStyle ShapeStyle(const kb::scene::UIPresentationItem& item,
                                             const kb::scene::UIPaint& paint) noexcept {
    return UserWidgetDrawStyle{
        .fillColor = Color(paint.backgroundColor),
        .borderColor = Color(paint.borderColor),
        .borderWidths = Edges(paint.borderWidth, item.canvasScale),
        .cornerRadii = Corners(paint.cornerRadius, item.canvasScale),
        .width = item.rect.Width() * std::abs(item.scale.x),
        .height = item.rect.Height() * std::abs(item.scale.y),
        .opacity = std::clamp(paint.opacity, 0.0F, 1.0F),
        .fragmentKind = UserWidgetFragmentKind::Shape,
    };
}

[[nodiscard]] bool HasBorder(const UserWidgetDrawStyle& style) noexcept {
    return Visible(style.borderColor) &&
           std::ranges::any_of(style.borderWidths, [](float width) { return width > 0.0F; });
}

[[nodiscard]] UserWidgetDrawStyle ControlStyle(const kb::scene::UIPresentationItem& item,
                                               const kb::scene::UIPresentationRect& rect, std::array<float, 4> fill,
                                               float radius) noexcept {
    UserWidgetDrawStyle style{};
    style.fillColor = fill;
    style.cornerRadii = {radius, radius, radius, radius};
    style.width = rect.Width() * std::abs(item.scale.x);
    style.height = rect.Height() * std::abs(item.scale.y);
    style.fragmentKind = UserWidgetFragmentKind::Shape;
    return style;
}

constexpr std::array<float, 4> kControlBackground{0.12F, 0.14F, 0.18F, 1.0F};
constexpr std::array<float, 4> kControlTrack{0.18F, 0.21F, 0.27F, 1.0F};
constexpr std::array<float, 4> kControlAccent{0.18F, 0.52F, 0.94F, 1.0F};

[[nodiscard]] bool HasDefaultSurface(kb::scene::UIControlKind kind) noexcept {
    return kind == kb::scene::UIControlKind::Button || kind == kb::scene::UIControlKind::Toggle ||
           kind == kb::scene::UIControlKind::Slider || kind == kb::scene::UIControlKind::ProgressBar ||
           kind == kb::scene::UIControlKind::InputField || kind == kb::scene::UIControlKind::Dropdown ||
           kind == kb::scene::UIControlKind::Scrollbar;
}

} // namespace

const UserWidgetDrawList& UserWidgetDrawBatchBuilder::Build(const kb::scene::UIPresentationSnapshot& snapshot,
                                                            std::span<const UserWidgetImageBinding> images,
                                                            std::span<const UserWidgetTextRun> textRuns) {
    drawList_.Clear();
    viewportWidth_ = snapshot.viewportWidth;
    viewportHeight_ = snapshot.viewportHeight;
    drawList_.vertices.reserve(snapshot.items.size() * 4U);
    drawList_.indices.reserve(snapshot.items.size() * 6U);
    drawList_.batches.reserve(snapshot.items.size());
    for (const kb::scene::UIPresentationItem& item : snapshot.items) {
        if (item.rect.IsValid() && item.clipRect.IsValid()) {
            AppendItem(item, images, textRuns);
        }
    }
    if (drawList_.maximumBackgroundBlur > 0.0F) {
        for (UserWidgetDrawBatch& batch : drawList_.batches) {
            if (batch.style.fragmentKind == UserWidgetFragmentKind::BackgroundBlur) {
                batch.style.opacity =
                    std::clamp(batch.style.blurStrength / drawList_.maximumBackgroundBlur, 0.0F, 1.0F);
            }
        }
    }
    return drawList_;
}

void UserWidgetDrawBatchBuilder::AppendItem(const kb::scene::UIPresentationItem& item,
                                            std::span<const UserWidgetImageBinding> images,
                                            std::span<const UserWidgetTextRun> textRuns) {
    const kb::scene::UIEffects effects = item.effects.value_or(kb::scene::UIEffects{});
    const kb::scene::UIPaint paint = item.paint.value_or(kb::scene::UIPaint{});
    UserWidgetDrawStyle baseStyle = ShapeStyle(item, paint);

    const bool hasSurface = Visible(baseStyle.fillColor) || HasBorder(baseStyle) || item.image.has_value() ||
                            HasDefaultSurface(item.controlKind);
    if (hasSurface && effects.shadowEnabled && effects.shadowColor.a > 0.0F) {
        const float spread = std::max(effects.shadowBlur * item.canvasScale, 0.0F);
        kb::scene::UIPresentationRect shadowRect{
            .left = item.rect.left + effects.shadowOffset.x * item.canvasScale - spread,
            .top = item.rect.top + effects.shadowOffset.y * item.canvasScale - spread,
            .right = item.rect.right + effects.shadowOffset.x * item.canvasScale + spread,
            .bottom = item.rect.bottom + effects.shadowOffset.y * item.canvasScale + spread,
        };
        UserWidgetDrawStyle shadowStyle = baseStyle;
        shadowStyle.fillColor = Color(effects.shadowColor);
        shadowStyle.borderColor = {};
        shadowStyle.borderWidths = {};
        shadowStyle.width = shadowRect.Width() * std::abs(item.scale.x);
        shadowStyle.height = shadowRect.Height() * std::abs(item.scale.y);
        shadowStyle.feather = spread;
        AppendShape(item, shadowRect, shadowStyle);
    }

    if (effects.backgroundBlur > 0.0F) {
        UserWidgetDrawStyle blurStyle = baseStyle;
        blurStyle.fillColor = {1.0F, 1.0F, 1.0F, 1.0F};
        blurStyle.borderColor = {};
        blurStyle.borderWidths = {};
        blurStyle.opacity = 1.0F;
        blurStyle.blurStrength = effects.backgroundBlur;
        blurStyle.fragmentKind = UserWidgetFragmentKind::BackgroundBlur;
        const float inverseWidth = viewportWidth_ == 0U ? 0.0F : 1.0F / static_cast<float>(viewportWidth_);
        const float inverseHeight = viewportHeight_ == 0U ? 0.0F : 1.0F / static_cast<float>(viewportHeight_);
        AppendQuad(item, item.rect, item.rect, item.rect.left * inverseWidth, item.rect.top * inverseHeight,
                   item.rect.right * inverseWidth, item.rect.bottom * inverseHeight,
                   UserWidgetTextureKey{.source = UserWidgetTextureSource::BackgroundBlur}, blurStyle);
        drawList_.requiresBackgroundBlur = true;
        drawList_.maximumBackgroundBlur = std::max(drawList_.maximumBackgroundBlur, effects.backgroundBlur);
    }

    if (Visible(baseStyle.fillColor) || HasBorder(baseStyle)) {
        AppendShape(item, item.rect, baseStyle);
    }

    AppendControlPrimitives(item, item.paint.has_value());

    if (item.image.has_value() && item.image->imageAssetId != 0U) {
        if (const UserWidgetImageBinding* image = FindImage(images, item.image->imageAssetId); image != nullptr) {
            AppendImage(item, *image);
        }
    }

    if (item.textStyle.has_value() && !item.text.empty()) {
        if (const UserWidgetTextRun* run = FindTextRun(textRuns, item); run != nullptr) {
            if (effects.shadowEnabled && effects.shadowColor.a > 0.0F) {
                const float shadowBlur = std::max(effects.shadowBlur * item.canvasScale, 0.0F);
                AppendTextLayer(item, *run, Color(effects.shadowColor), Color(effects.shadowColor),
                                effects.shadowOffset.x * item.canvasScale, effects.shadowOffset.y * item.canvasScale,
                                shadowBlur);
            }
            AppendText(item, *run);
        }
    }

    if (hasSurface && effects.outlineEnabled && effects.outlineColor.a > 0.0F && effects.outlineWidth > 0.0F) {
        const float width = effects.outlineWidth * item.canvasScale;
        kb::scene::UIPresentationRect outlineRect{
            .left = item.rect.left - width,
            .top = item.rect.top - width,
            .right = item.rect.right + width,
            .bottom = item.rect.bottom + width,
        };
        UserWidgetDrawStyle outlineStyle = baseStyle;
        outlineStyle.fillColor = {};
        outlineStyle.borderColor = Color(effects.outlineColor);
        outlineStyle.borderWidths = {width, width, width, width};
        outlineStyle.cornerRadii[0] += width;
        outlineStyle.cornerRadii[1] += width;
        outlineStyle.cornerRadii[2] += width;
        outlineStyle.cornerRadii[3] += width;
        outlineStyle.width = outlineRect.Width() * std::abs(item.scale.x);
        outlineStyle.height = outlineRect.Height() * std::abs(item.scale.y);
        AppendShape(item, outlineRect, outlineStyle);
    }
}

void UserWidgetDrawBatchBuilder::AppendShape(const kb::scene::UIPresentationItem& item,
                                             const kb::scene::UIPresentationRect& rect,
                                             const UserWidgetDrawStyle& style) {
    AppendQuad(item, rect, rect, 0.0F, 0.0F, 1.0F, 1.0F, {}, style);
}

void UserWidgetDrawBatchBuilder::AppendControlPrimitives(const kb::scene::UIPresentationItem& item,
                                                         bool hasAuthoredPaint) {
    const float radius = std::max(4.0F * item.canvasScale, 0.0F);
    switch (item.controlKind) {
    case kb::scene::UIControlKind::Button:
    case kb::scene::UIControlKind::InputField:
    case kb::scene::UIControlKind::Dropdown:
        if (!hasAuthoredPaint) {
            UserWidgetDrawStyle style = ControlStyle(item, item.rect, kControlBackground, radius);
            const float borderWidth = std::max(item.canvasScale, 1.0F);
            style.borderColor = {0.32F, 0.36F, 0.44F, 1.0F};
            style.borderWidths = {borderWidth, borderWidth, borderWidth, borderWidth};
            AppendShape(item, item.rect, style);
        }
        return;
    case kb::scene::UIControlKind::Toggle: {
        const float side = std::min(item.rect.Width(), item.rect.Height());
        const kb::scene::UIPresentationRect box{
            .left = item.rect.left,
            .top = item.rect.top + (item.rect.Height() - side) * 0.5F,
            .right = item.rect.left + side,
            .bottom = item.rect.top + (item.rect.Height() + side) * 0.5F,
        };
        if (!hasAuthoredPaint) {
            AppendShape(item, box, ControlStyle(item, box, kControlTrack, radius));
        }
        if (item.toggleValue) {
            const float inset = std::min(side * 0.22F, 6.0F * std::max(item.canvasScale, 0.01F));
            const kb::scene::UIPresentationRect check{
                .left = box.left + inset,
                .top = box.top + inset,
                .right = box.right - inset,
                .bottom = box.bottom - inset,
            };
            if (check.IsValid()) {
                AppendShape(item, check, ControlStyle(item, check, kControlAccent, radius * 0.5F));
            }
        }
        return;
    }
    case kb::scene::UIControlKind::Slider:
    case kb::scene::UIControlKind::ProgressBar:
    case kb::scene::UIControlKind::Scrollbar: {
        if (!hasAuthoredPaint) {
            AppendShape(item, item.rect, ControlStyle(item, item.rect, kControlTrack, radius));
        }
        const float range = item.maximum - item.minimum;
        const float ratio = range > 0.0F ? std::clamp((item.value - item.minimum) / range, 0.0F, 1.0F) : 0.0F;
        kb::scene::UIPresentationRect fill = item.rect;
        fill.right = fill.left + fill.Width() * ratio;
        if (fill.IsValid()) {
            AppendShape(item, fill, ControlStyle(item, fill, kControlAccent, radius));
        }
        return;
    }
    case kb::scene::UIControlKind::Container:
    case kb::scene::UIControlKind::Text:
    case kb::scene::UIControlKind::Image:
    case kb::scene::UIControlKind::List:
    case kb::scene::UIControlKind::ScrollView:
    case kb::scene::UIControlKind::ModalDialog:
    case kb::scene::UIControlKind::Canvas:
    case kb::scene::UIControlKind::Border:
    case kb::scene::UIControlKind::Overlay:
    case kb::scene::UIControlKind::HorizontalBox:
    case kb::scene::UIControlKind::VerticalBox:
    case kb::scene::UIControlKind::Grid:
    case kb::scene::UIControlKind::Wrap:
    case kb::scene::UIControlKind::Spacer:
    case kb::scene::UIControlKind::SizeBox:
    case kb::scene::UIControlKind::ScaleBox:
    case kb::scene::UIControlKind::WidgetSwitcher:
        return;
    }
}

void UserWidgetDrawBatchBuilder::AppendImage(const kb::scene::UIPresentationItem& item,
                                             const UserWidgetImageBinding& binding) {
    const kb::scene::UIImage& image = *item.image;
    if (binding.width == 0U || binding.height == 0U) {
        return;
    }

    kb::scene::UIPresentationRect destination = item.rect;
    float u0 = image.uvRect.x;
    float v0 = image.uvRect.y;
    float u1 = image.uvRect.x + image.uvRect.width;
    float v1 = image.uvRect.y + image.uvRect.height;
    const float sourceWidth = std::max(image.uvRect.width * static_cast<float>(binding.width), 1.0F);
    const float sourceHeight = std::max(image.uvRect.height * static_cast<float>(binding.height), 1.0F);
    const float sourceAspect = sourceWidth / sourceHeight;
    const float destinationAspect = destination.Width() / destination.Height();
    const bool contain = image.scaleMode == kb::scene::UIImageScaleMode::Contain ||
                         (image.scaleMode == kb::scene::UIImageScaleMode::Stretch && image.preserveAspect);
    if (contain) {
        if (destinationAspect > sourceAspect) {
            const float width = destination.Height() * sourceAspect;
            const float inset = (destination.Width() - width) * 0.5F;
            destination.left += inset;
            destination.right -= inset;
        } else {
            const float height = destination.Width() / sourceAspect;
            const float inset = (destination.Height() - height) * 0.5F;
            destination.top += inset;
            destination.bottom -= inset;
        }
    } else if (image.scaleMode == kb::scene::UIImageScaleMode::Cover) {
        if (destinationAspect > sourceAspect) {
            const float visibleFraction = sourceAspect / destinationAspect;
            const float inset = (v1 - v0) * (1.0F - visibleFraction) * 0.5F;
            v0 += inset;
            v1 -= inset;
        } else {
            const float visibleFraction = destinationAspect / sourceAspect;
            const float inset = (u1 - u0) * (1.0F - visibleFraction) * 0.5F;
            u0 += inset;
            u1 -= inset;
        }
    }

    UserWidgetDrawStyle style{};
    style.fillColor = {1.0F, 1.0F, 1.0F, 1.0F};
    style.cornerRadii =
        item.paint.has_value() ? Corners(item.paint->cornerRadius, item.canvasScale) : std::array<float, 4>{};
    style.width = destination.Width() * std::abs(item.scale.x);
    style.height = destination.Height() * std::abs(item.scale.y);
    style.opacity = item.paint.has_value() ? std::clamp(item.paint->opacity, 0.0F, 1.0F) : 1.0F;
    style.fragmentKind = UserWidgetFragmentKind::Image;
    const UserWidgetTextureKey texture{
        .source = UserWidgetTextureSource::ImageAsset,
        .assetId = image.imageAssetId,
    };

    const float left = std::clamp(image.nineSlice.left * item.canvasScale, 0.0F, destination.Width() * 0.5F);
    const float top = std::clamp(image.nineSlice.top * item.canvasScale, 0.0F, destination.Height() * 0.5F);
    const float right = std::clamp(image.nineSlice.right * item.canvasScale, 0.0F, destination.Width() * 0.5F);
    const float bottom = std::clamp(image.nineSlice.bottom * item.canvasScale, 0.0F, destination.Height() * 0.5F);
    if (left <= 0.0F && top <= 0.0F && right <= 0.0F && bottom <= 0.0F) {
        AppendQuad(item, destination, destination, u0, v0, u1, v1, texture, style);
        return;
    }

    const std::array<float, 4> xs{destination.left, destination.left + left, destination.right - right,
                                  destination.right};
    const std::array<float, 4> ys{destination.top, destination.top + top, destination.bottom - bottom,
                                  destination.bottom};
    const std::array<float, 4> us{
        u0,
        u0 + image.nineSlice.left / static_cast<float>(binding.width),
        u1 - image.nineSlice.right / static_cast<float>(binding.width),
        u1,
    };
    const std::array<float, 4> vs{
        v0,
        v0 + image.nineSlice.top / static_cast<float>(binding.height),
        v1 - image.nineSlice.bottom / static_cast<float>(binding.height),
        v1,
    };
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            const kb::scene::UIPresentationRect cell{
                .left = xs[column],
                .top = ys[row],
                .right = xs[column + 1U],
                .bottom = ys[row + 1U],
            };
            if (cell.IsValid()) {
                AppendQuad(item, destination, cell, us[column], vs[row], us[column + 1U], vs[row + 1U], texture, style);
            }
        }
    }
}

void UserWidgetDrawBatchBuilder::AppendText(const kb::scene::UIPresentationItem& item, const UserWidgetTextRun& run) {
    const kb::scene::UIEffects effects = item.effects.value_or(kb::scene::UIEffects{});
    const float outlineWidth = effects.outlineEnabled ? std::max(effects.outlineWidth * item.canvasScale, 0.0F) : 0.0F;
    AppendTextLayer(item, run, Color(item.textStyle->color), Color(effects.outlineColor), 0.0F, 0.0F, outlineWidth);
}

void UserWidgetDrawBatchBuilder::AppendTextLayer(const kb::scene::UIPresentationItem& item,
                                                 const UserWidgetTextRun& run, const std::array<float, 4>& color,
                                                 const std::array<float, 4>& outlineColor, float offsetX, float offsetY,
                                                 float outlineWidth) {
    UserWidgetDrawStyle style{};
    style.fillColor = color;
    style.borderColor = outlineColor;
    style.opacity = item.paint.has_value() ? std::clamp(item.paint->opacity, 0.0F, 1.0F) : 1.0F;
    style.fragmentKind = UserWidgetFragmentKind::Font;
    style.fontOutlineWidth = outlineWidth;
    const UserWidgetTextureKey texture{
        .source = UserWidgetTextureSource::FontAtlas,
        .assetId = run.fontAssetId,
        .pixelSize = run.pixelSize,
    };
    const float atlasWidth = static_cast<float>(std::max(run.atlasWidth, static_cast<std::uint16_t>(1U)));
    const float atlasHeight = static_cast<float>(std::max(run.atlasHeight, static_cast<std::uint16_t>(1U)));
    for (const UserWidgetGlyphQuad& glyph : run.glyphs) {
        const kb::scene::UIPresentationRect rect{
            .left = glyph.left + offsetX - outlineWidth,
            .top = glyph.top + offsetY - outlineWidth,
            .right = glyph.right + offsetX + outlineWidth,
            .bottom = glyph.bottom + offsetY + outlineWidth,
        };
        if (rect.IsValid()) {
            AppendQuad(item, item.rect, rect, glyph.u0 - outlineWidth / atlasWidth,
                       glyph.v0 - outlineWidth / atlasHeight, glyph.u1 + outlineWidth / atlasWidth,
                       glyph.v1 + outlineWidth / atlasHeight, texture, style);
        }
    }
}

void UserWidgetDrawBatchBuilder::AppendQuad(const kb::scene::UIPresentationItem& item,
                                            const kb::scene::UIPresentationRect& fullRect,
                                            const kb::scene::UIPresentationRect& quadRect, float u0, float v0, float u1,
                                            float v1, const UserWidgetTextureKey& texture,
                                            const UserWidgetDrawStyle& style) {
    if (!fullRect.IsValid() || !quadRect.IsValid()) {
        return;
    }
    const float pivotX = item.rect.left + item.rect.Width() * item.pivot.x;
    const float pivotY = item.rect.top + item.rect.Height() * item.pivot.y;
    const float radians = item.rotationDegrees * kb::math::kPi / 180.0F;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const auto transform = [&](float x, float y) noexcept {
        const float scaledX = (x - pivotX) * item.scale.x;
        const float scaledY = (y - pivotY) * item.scale.y;
        return std::array<float, 2>{
            pivotX + scaledX * cosine - scaledY * sine,
            pivotY + scaledX * sine + scaledY * cosine,
        };
    };

    const std::array<float, 2> topLeft = transform(quadRect.left, quadRect.top);
    const std::array<float, 2> topRight = transform(quadRect.right, quadRect.top);
    const std::array<float, 2> bottomRight = transform(quadRect.right, quadRect.bottom);
    const std::array<float, 2> bottomLeft = transform(quadRect.left, quadRect.bottom);
    const float inverseWidth = 1.0F / fullRect.Width();
    const float inverseHeight = 1.0F / fullRect.Height();
    const float localLeft = (quadRect.left - fullRect.left) * inverseWidth;
    const float localTop = (quadRect.top - fullRect.top) * inverseHeight;
    const float localRight = (quadRect.right - fullRect.left) * inverseWidth;
    const float localBottom = (quadRect.bottom - fullRect.top) * inverseHeight;
    const std::uint32_t firstVertex = static_cast<std::uint32_t>(drawList_.vertices.size());
    drawList_.vertices.insert(drawList_.vertices.end(),
                              {
                                  UserWidgetVertex{topLeft[0], topLeft[1], u0, v0, localLeft, localTop},
                                  UserWidgetVertex{topRight[0], topRight[1], u1, v0, localRight, localTop},
                                  UserWidgetVertex{bottomRight[0], bottomRight[1], u1, v1, localRight, localBottom},
                                  UserWidgetVertex{bottomLeft[0], bottomLeft[1], u0, v1, localLeft, localBottom},
                              });
    const std::uint32_t firstIndex = static_cast<std::uint32_t>(drawList_.indices.size());
    drawList_.indices.insert(drawList_.indices.end(), {
                                                          firstVertex,
                                                          firstVertex + 1U,
                                                          firstVertex + 2U,
                                                          firstVertex,
                                                          firstVertex + 2U,
                                                          firstVertex + 3U,
                                                      });

    const bool canMerge = !drawList_.batches.empty() && drawList_.batches.back().texture == texture &&
                          SameRect(drawList_.batches.back().clipRect, item.clipRect) &&
                          drawList_.batches.back().style == style &&
                          drawList_.batches.back().firstIndex + drawList_.batches.back().indexCount == firstIndex;
    if (canMerge) {
        drawList_.batches.back().indexCount += 6U;
    } else {
        drawList_.batches.push_back(UserWidgetDrawBatch{
            .texture = texture,
            .clipRect = item.clipRect,
            .style = style,
            .firstIndex = firstIndex,
            .indexCount = 6U,
        });
    }
}

} // namespace kb::render
