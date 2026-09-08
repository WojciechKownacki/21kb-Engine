#include "RendererTestSupport.hpp"

#include "private/ui/ScreenUIDrawBatchBuilder.hpp"
#include "private/ui/ScreenUIFontAtlasCache.hpp"
#include "private/ui/ScreenUIFontPayloadValidator.hpp"
#include "private/ui/ScreenUITextMarkupParser.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"

#include "engine/assets/AssetImportCatalog.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/ui/visual/UIText.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>

namespace kb::render::tests {
namespace {

[[nodiscard]] kb::scene::SceneUIFrameElement Element(std::uint64_t entity, kb::math::Rect rect) {
    return kb::scene::SceneUIFrameElement{
        .entity = kb::scene::SceneEntity{entity},
        .canvas = kb::scene::SceneEntity{1U},
        .rect = rect,
        .corners =
            {
                kb::math::Vec2{rect.x, rect.y},
                kb::math::Vec2{rect.x + rect.width, rect.y},
                kb::math::Vec2{rect.x + rect.width, rect.y + rect.height},
                kb::math::Vec2{rect.x, rect.y + rect.height},
            },
        .clipRect = kb::math::Rect{0.0F, 0.0F, 640.0F, 360.0F},
    };
}

void BatchBuilderPreservesFlattenedOrderCornersAndClip() {
    kb::scene::SceneUIFrame frame{.viewportSize = {640.0F, 360.0F}};
    kb::scene::SceneUIFrameElement first = Element(10U, {10.0F, 20.0F, 100.0F, 40.0F});
    first.corners = {kb::math::Vec2{15.0F, 10.0F}, kb::math::Vec2{115.0F, 20.0F}, kb::math::Vec2{110.0F, 60.0F},
                     kb::math::Vec2{10.0F, 50.0F}};
    first.clipRect = {5.0F, 6.0F, 150.0F, 80.0F};
    first.border = kb::scene::UIBorder{.backgroundColor = {1.0F, 0.0F, 0.0F, 1.0F}};
    kb::scene::SceneUIFrameElement second = Element(11U, {140.0F, 20.0F, 80.0F, 40.0F});
    second.border = kb::scene::UIBorder{.backgroundColor = {0.0F, 0.0F, 1.0F, 1.0F}};
    frame.elements = {first, second};

    ScreenUIDrawBatchBuilder builder;
    const ScreenUIDrawList& draw = builder.Build(frame, {}, {});
    Require(draw.batches.size() == 2U, "Screen UI batcher changed the flattened frame draw order");
    Require(draw.batches[0].style.fillColor[0] == 1.0F && draw.batches[1].style.fillColor[2] == 1.0F,
            "Screen UI batcher reordered frame colors");
    Require(draw.batches[0].clipRect == ScreenUIRect{5.0F, 6.0F, 155.0F, 86.0F},
            "Screen UI batcher did not preserve the resolved clip rectangle");
    Require(draw.vertices[0].x == 15.0F && draw.vertices[0].y == 10.0F && draw.vertices[2].x == 110.0F &&
                draw.vertices[2].y == 60.0F,
            "Screen UI batcher reconstructed transform geometry instead of using frame corners");
}

void DefaultImagesAndControlsProduceVisibleGeometry() {
    ScreenUIDrawBatchBuilder builder;
    for (int kind = 0; kind < 3; ++kind) {
        auto element = Element(90U, {10.0F, 20.0F, 160.0F, 40.0F});
        if (kind == 0) element.image.emplace();
        if (kind == 1) element.rawImage.emplace();
        if (kind == 2) element.sprite.emplace();
        kb::scene::SceneUIFrame frame{.viewportSize = {640.0F, 360.0F}, .elements = {element}};
        const auto& draw = builder.Build(frame, {}, {});
        Require(draw.vertices.size() == 4U && draw.batches.size() == 1U &&
                draw.batches.front().style.fillColor[3] == 1.0F,
            "New UI image without an assigned asset must render its color rectangle");
    }
    auto element = Element(91U, {10.0F, 20.0F, 160.0F, 40.0F});
    element.border = kb::scene::UIBorder{.borderColor = {0.3F, 0.8F, 1.0F, 1.0F}};
    element.slider = kb::scene::UISlider{.value = 0.25F};
    kb::scene::SceneUIFrame frame{.viewportSize = {640.0F, 360.0F}, .elements = {element}};
    const auto first = builder.Build(frame, {}, {}).vertices;
    frame.elements.front().slider->value = 0.75F;
    const auto second = builder.Build(frame, {}, {}).vertices;
    Require(first.size() == 8U && second.size() == 8U && second[4].x > first[4].x,
        "Changing slider value must move its visible thumb");
    frame.elements.front().slider.reset();
    frame.elements.front().toggle.emplace();
    Require(builder.Build(frame, {}, {}).vertices.empty(), "Unchecked toggle must hide its mark");
    frame.elements.front().toggle->toggled = true;
    Require(builder.Build(frame, {}, {}).vertices.size() == 4U, "Checked toggle must show its mark");
}

void BatchBuilderEmitsNineSliceTintAndEffects() {
    kb::scene::SceneUIFrame frame{.viewportSize = {640.0F, 360.0F}};
    kb::scene::SceneUIFrameElement image = Element(20U, {20.0F, 30.0F, 120.0F, 80.0F});
    image.canvasScale = 2.0F;
    image.image = kb::scene::UIImage{
        .imageAssetId = 77U, .nineSlice = {4.0F, 5.0F, 6.0F, 7.0F}, .color = {0.25F, 0.5F, 0.75F, 0.8F}};
    image.backgroundBlur = kb::scene::UIBackgroundBlur{.radius = 6.0F};
    image.shadow = kb::scene::UIShadow{.offset = {2.0F, 3.0F}, .color = {0.0F, 0.0F, 0.0F, 0.5F}, .blur = 2.0F};
    image.outline = kb::scene::UIOutline{.color = {1.0F, 1.0F, 0.0F, 1.0F}, .width = 1.5F};
    frame.elements.push_back(image);

    constexpr std::array<ScreenUIImageBinding, 1U> images{{ScreenUIImageBinding{
        .assetId = 77U,
        .source = ScreenUITextureSource::ImageAssetSrgb,
        .width = 64U,
        .height = 32U,
    }}};
    ScreenUIDrawBatchBuilder builder;
    const ScreenUIDrawList& draw = builder.Build(frame, images, {});
    Require(draw.requiresBackgroundBlur && NearlyEqual(draw.maximumBackgroundBlur, 12.0F),
            "Screen UI batcher did not scale the requested background blur");
    const auto imageBatch = std::ranges::find_if(draw.batches, [](const ScreenUIDrawBatch& batch) {
        return batch.style.fragmentKind == ScreenUIFragmentKind::Image;
    });
    Require(imageBatch != draw.batches.end() && imageBatch->indexCount == 54U,
            "Screen UI batcher did not emit all nine image slices");
    Require(imageBatch->style.fillColor == std::array<float, 4>{0.25F, 0.5F, 0.75F, 0.8F},
            "Screen UI image tint was not preserved");
    Require(std::ranges::any_of(draw.batches,
                                [](const ScreenUIDrawBatch& batch) {
                                    return batch.style.fragmentKind == ScreenUIFragmentKind::BackgroundBlur;
                                }),
            "Screen UI batcher omitted the background blur primitive");
}

void BatchBuilderUsesTextRunAndHonorsHiddenMask() {
    kb::scene::SceneUIFrame frame{.viewportSize = {640.0F, 360.0F}};
    kb::scene::SceneUIFrameElement text = Element(30U, {10.0F, 10.0F, 200.0F, 50.0F});
    text.text = kb::scene::UIText{.fontAssetId = 91U, .fontSize = 20.0F, .color = {0.2F, 0.8F, 0.3F, 1.0F}};
    Require(kb::scene::SetUITextContent(*text.text, "A"), "Could not create screen UI text fixture");
    text.shadow = kb::scene::UIShadow{};
    text.outline = kb::scene::UIOutline{.color = {1.0F, 0.0F, 0.0F, 1.0F}, .width = 2.0F};
    kb::scene::SceneUIFrameElement hidden = Element(31U, {220.0F, 10.0F, 100.0F, 50.0F});
    hidden.border = kb::scene::UIBorder{.backgroundColor = {1.0F, 1.0F, 1.0F, 1.0F}};
    hidden.mask = kb::scene::UIMask{.showGraphic = false};
    frame.elements = {text, hidden};
    ScreenUITextRun run{.entity = 30U, .fontAssetId = 91U, .pixelSize = 20U, .atlasWidth = 256U, .atlasHeight = 256U};
    run.glyphs.push_back(ScreenUIGlyphQuad{.left = 10.0F,
                                           .top = 10.0F,
                                           .right = 20.0F,
                                           .bottom = 30.0F,
                                           .u1 = 0.1F,
                                           .v1 = 0.2F,
                                           .color = {0.5F, 0.25F, 1.0F, 0.5F}});

    ScreenUIDrawBatchBuilder builder;
    const ScreenUIDrawList& draw = builder.Build(frame, {}, std::span<const ScreenUITextRun>{&run, 1U});
    Require(draw.batches.size() == 2U && std::ranges::all_of(draw.batches,
                                                             [](const ScreenUIDrawBatch& batch) {
                                                                 return batch.style.fragmentKind ==
                                                                        ScreenUIFragmentKind::Font;
                                                             }),
            "Screen UI text layers or hidden-mask behavior are wrong");
    Require(NearlyEqual(draw.batches[1U].style.fillColor[0U], 0.1F) &&
                NearlyEqual(draw.batches[1U].style.fillColor[1U], 0.2F) &&
                NearlyEqual(draw.batches[1U].style.fillColor[2U], 0.3F) &&
                NearlyEqual(draw.batches[1U].style.fillColor[3U], 0.5F),
            "Screen UI text layer did not apply per-glyph rich text color");
}

void BatchBuilderAppliesInteractionTintToEveryVisualLayer() {
    constexpr std::array<float, 4U> expectedTint{0.25F, 0.5F, 0.75F, 0.4F};
    kb::scene::SceneUIFrame frame{.viewportSize = {640.0F, 360.0F}};
    kb::scene::SceneUIFrameElement element = Element(40U, {20.0F, 20.0F, 160.0F, 60.0F});
    element.interactionTint = {expectedTint[0], expectedTint[1], expectedTint[2], expectedTint[3]};
    element.border = kb::scene::UIBorder{
        .backgroundColor = {1.0F, 1.0F, 1.0F, 1.0F},
        .borderColor = {1.0F, 1.0F, 1.0F, 1.0F},
        .borderWidth = {1.0F, 1.0F, 1.0F, 1.0F},
    };
    element.image = kb::scene::UIImage{.imageAssetId = 77U, .color = {1.0F, 1.0F, 1.0F, 1.0F}};
    element.text = kb::scene::UIText{.fontAssetId = 91U, .fontSize = 20.0F, .color = {1.0F, 1.0F, 1.0F, 1.0F}};
    Require(kb::scene::SetUITextContent(*element.text, "A"), "Could not create interaction-tint text fixture");
    element.shadow = kb::scene::UIShadow{.offset = {2.0F, 2.0F}, .color = {1.0F, 1.0F, 1.0F, 1.0F}};
    element.outline = kb::scene::UIOutline{.color = {1.0F, 1.0F, 1.0F, 1.0F}, .width = 1.0F};
    element.backgroundBlur = kb::scene::UIBackgroundBlur{.radius = 2.0F};
    frame.elements.push_back(element);

    constexpr std::array<ScreenUIImageBinding, 1U> images{{ScreenUIImageBinding{
        .assetId = 77U,
        .source = ScreenUITextureSource::ImageAssetSrgb,
        .width = 32U,
        .height = 32U,
    }}};
    ScreenUITextRun run{.entity = 40U, .fontAssetId = 91U, .pixelSize = 20U, .atlasWidth = 256U, .atlasHeight = 256U};
    run.glyphs.push_back(ScreenUIGlyphQuad{20.0F, 20.0F, 30.0F, 40.0F, 0.0F, 0.0F, 0.1F, 0.2F});

    ScreenUIDrawBatchBuilder builder;
    const ScreenUIDrawList& draw = builder.Build(frame, images, std::span<const ScreenUITextRun>{&run, 1U});
    Require(draw.batches.size() == 8U, "Screen UI interaction-tint fixture omitted a visual layer");
    std::size_t tintedFillCount = 0U;
    std::size_t tintedBorderCount = 0U;
    for (const ScreenUIDrawBatch& batch : draw.batches) {
        if (batch.style.fillColor[3] > 0.0F) {
            Require(batch.style.fillColor == expectedTint,
                    "Screen UI interaction tint was not applied to a visual fill layer");
            ++tintedFillCount;
        }
        if (batch.style.borderColor[3] > 0.0F) {
            Require(batch.style.borderColor == expectedTint,
                    "Screen UI interaction tint was not applied to a visual border or text-effect layer");
            ++tintedBorderCount;
        }
    }
    Require(tintedFillCount == 6U && tintedBorderCount == 4U,
            "Screen UI interaction tint did not cover every visual layer");
}

void FocusedInputFieldDrawsItsCaretAtTheEditedOffset() {
    // The engine tracked a caret byte offset and moved it with the arrow keys, but nothing ever
    // drew it. A shipped text field therefore looked inert no matter what the player typed.
    const auto frameWithCaret = [](bool caretVisible, std::uint32_t caretOffset) {
        kb::scene::SceneUIFrame frame{.viewportSize = {640.0F, 360.0F}};
        kb::scene::SceneUIFrameElement field = Element(30U, {0.0F, 0.0F, 200.0F, 40.0F});
        kb::scene::UIText text{.fontAssetId = 5U};
        Require(kb::scene::SetUITextContent(text, "ab"), "Caret fixture could not author its text");
        field.text = text;
        field.inputField = kb::scene::UIInputField{};
        field.textCaretVisible = caretVisible;
        field.textCaretByteOffset = caretOffset;
        frame.elements = {field};
        return frame;
    };

    // Two glyphs at known columns, as the font layout would produce them.
    ScreenUITextRun run{.entity = 30U, .fontAssetId = 5U, .pixelSize = 16U, .atlasWidth = 64U, .atlasHeight = 64U};
    run.glyphs.push_back(ScreenUIGlyphQuad{.left = 10.0F, .top = 12.0F, .right = 18.0F, .bottom = 26.0F,
                                           .sourceOffset = 0U, .lineTop = 8.0F, .lineBottom = 30.0F,
                                           .advanceRight = 20.0F});
    run.glyphs.push_back(ScreenUIGlyphQuad{.left = 20.0F, .top = 12.0F, .right = 28.0F, .bottom = 26.0F,
                                           .sourceOffset = 1U, .lineTop = 8.0F, .lineBottom = 30.0F,
                                           .advanceRight = 30.0F});
    const std::array<ScreenUITextRun, 1> runs{run};

    ScreenUIDrawBatchBuilder builder;
    const std::size_t withoutCaret = builder.Build(frameWithCaret(false, 1U), {}, runs).batches.size();
    const ScreenUIDrawList& withCaret = builder.Build(frameWithCaret(true, 1U), {}, runs);
    Require(withCaret.batches.size() == withoutCaret + 1U,
            "A focused input field must draw exactly one caret batch on top of its text");

    // The caret belongs in front of the character at its byte offset, not at the end of the run.
    bool foundAtSecondGlyph = false;
    for (const ScreenUIVertex& vertex : withCaret.vertices) {
        // The corner-basis transform reintroduces float error, so compare with tolerance.
        if (NearlyEqual(vertex.x, 20.0F) && NearlyEqual(vertex.y, 8.0F)) {
            foundAtSecondGlyph = true;
        }
    }
    Require(foundAtSecondGlyph, "The caret must sit at the left edge of the glyph its byte offset points at");

    // Past the last character the caret follows the pen, not the final glyph's ink edge.
    const ScreenUIDrawList& atEnd = builder.Build(frameWithCaret(true, 2U), {}, runs);
    bool foundAtPen = false;
    for (const ScreenUIVertex& vertex : atEnd.vertices) {
        if (NearlyEqual(vertex.x, 30.0F) && NearlyEqual(vertex.y, 8.0F)) {
            foundAtPen = true;
        }
    }
    Require(foundAtPen, "A caret past the last character must sit at the pen position, not the glyph edge");
}

void FontPreparationDropsOnlyTheTextItCannotPrepare() {
    // One unpreparable text element used to clear every run and make ScreenUIRenderer::Submit
    // return false, which erased the whole UI - panels, images and borders included - for a
    // font that was still streaming in or a single malformed markup string. The failure has to
    // cost that one label and name it.
    kb::scene::SceneUIFrame frame{.viewportSize = {640.0F, 360.0F}};
    kb::scene::SceneUIFrameElement panel = Element(20U, {0.0F, 0.0F, 200.0F, 80.0F});
    panel.border = kb::scene::UIBorder{.backgroundColor = {0.0F, 1.0F, 0.0F, 1.0F}};

    kb::scene::SceneUIFrameElement label = Element(21U, {10.0F, 10.0F, 180.0F, 40.0F});
    kb::scene::UIText text{.fontAssetId = 7U, .richText = true};
    Require(kb::scene::SetUITextContent(text, "<color=not-a-color>broken"),
            "Font preparation test could not author its label content");
    label.text = text;
    frame.elements = {panel, label};

    kb::assets::AssetManager assets;
    RenderResourceRegistry resources;
    ScreenUIFontAtlasCache fonts;
    const ScreenUIFontPreparation prepared = fonts.Prepare(1U, assets, resources, frame);

    Require(prepared.failureReason != nullptr, "Unpreparable text must report why it could not be prepared");
    Require(prepared.failedEntity == 21U, "An unpreparable text failure must name the element that caused it");
    Require(prepared.runs.empty(), "The unpreparable text must not produce a draw run");

    ScreenUIDrawBatchBuilder builder;
    const ScreenUIDrawList& draw = builder.Build(frame, {}, prepared.runs);
    Require(!draw.batches.empty(),
            "A text element that cannot be prepared must not erase the rest of the UI");
    Require(draw.batches[0].style.fillColor[1] == 1.0F,
            "The panel behind an unpreparable label must still be drawn");
}

void ImportCatalogOffersOnlyRasterizableFontFormats() {
    // Every font extension the editor offers to import must be one the UI can rasterize.
    // Without this, adding a format to the import catalog is a silent promise the renderer
    // cannot keep, and the failure only shows up as a shipped game with no text.
    for (const std::string& extension : kb::assets::AssetImportCatalog::SupportedSourceExtensions()) {
        if (kb::assets::AssetImportCatalog::ClassifyExtension(std::filesystem::path{extension}) !=
            kb::assets::AssetImportCategory::Font) {
            continue;
        }
        Require(ScreenUIFontPayloadValidator::SupportsExtension(extension),
                "The import catalog offers a font format the screen UI cannot rasterize");
    }
    Require(kb::assets::AssetImportCatalog::ClassifyExtension(std::filesystem::path{".ttf"}) ==
                kb::assets::AssetImportCategory::Font,
            "TrueType must stay importable as a font");
}

void FontPayloadValidatorRejectsUnsupportedAndTruncatedData() {
    Require(!ScreenUIFontPayloadValidator::SupportsExtension(".woff"),
            "Screen UI font contract accepted an unsupported extension");
    kb::assets::ImportedAsset font{};
    font.category = kb::assets::AssetImportCategory::Font;
    font.sourceExtension = ".ttf";
    font.payload.resize(ScreenUIFontPayloadValidator::kMinimumPayloadBytes);
    font.sourceSize = font.payload.size();
    Require(!ScreenUIFontPayloadValidator::Validate(font),
            "Screen UI font safety gate accepted a truncated SFNT payload");
}

void RichTextParserAppliesColorAndLineBreakWithoutChangingLiteralText() {
    const ScreenUITextMarkup parsed = ScreenUITextMarkupParser{}.Parse("A<color=#80402080>B<br>C</color>D", true);
    Require(parsed.succeeded && parsed.glyphs.size() == 5U,
            "Screen UI rich text parser did not emit the expected visible glyphs");
    Require(parsed.glyphs[0U].codepoint == 'A' && parsed.glyphs[1U].codepoint == 'B' &&
                parsed.glyphs[2U].codepoint == '\n' && parsed.glyphs[3U].codepoint == 'C' &&
                parsed.glyphs[4U].codepoint == 'D',
            "Screen UI rich text parser changed visible content or line breaks");
    Require(NearlyEqual(parsed.glyphs[1U].color[0U], 128.0F / 255.0F) &&
                NearlyEqual(parsed.glyphs[1U].color[1U], 64.0F / 255.0F) &&
                NearlyEqual(parsed.glyphs[1U].color[2U], 32.0F / 255.0F) &&
                NearlyEqual(parsed.glyphs[1U].color[3U], 128.0F / 255.0F) &&
                parsed.glyphs[3U].color == parsed.glyphs[1U].color &&
                parsed.glyphs[4U].color == std::array<float, 4U>{1.0F, 1.0F, 1.0F, 1.0F},
            "Screen UI rich text parser did not scope the color tag");

    const ScreenUITextMarkup literal = ScreenUITextMarkupParser{}.Parse("<br>", false);
    Require(literal.succeeded && literal.glyphs.size() == 4U && literal.glyphs[0U].codepoint == '<',
            "Screen UI disabled rich text did not preserve markup literally");
}

} // namespace

void RunScreenUIDrawBatchTests() {
    DefaultImagesAndControlsProduceVisibleGeometry();
    BatchBuilderPreservesFlattenedOrderCornersAndClip();
    BatchBuilderEmitsNineSliceTintAndEffects();
    BatchBuilderUsesTextRunAndHonorsHiddenMask();
    BatchBuilderAppliesInteractionTintToEveryVisualLayer();
    FocusedInputFieldDrawsItsCaretAtTheEditedOffset();
    FontPreparationDropsOnlyTheTextItCannotPrepare();
    ImportCatalogOffersOnlyRasterizableFontFormats();
    FontPayloadValidatorRejectsUnsupportedAndTruncatedData();
    RichTextParserAppliesColorAndLineBreakWithoutChangingLiteralText();
}

} // namespace kb::render::tests
