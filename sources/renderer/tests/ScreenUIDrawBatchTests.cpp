#include "RendererTestSupport.hpp"

#include "private/ui/ScreenUIDrawBatchBuilder.hpp"
#include "private/ui/ScreenUIFontAtlasCache.hpp"
#include "private/ui/ScreenUIFontDistanceField.hpp"
#include "private/ui/ScreenUIFontPayloadValidator.hpp"
#include "private/ui/ScreenUITextMarkupParser.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"

#include "engine/assets/AssetImportCatalog.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/ui/visual/UIText.hpp"
#include "engine/ui/visual/UITextGraphemes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
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

void DistanceFieldOutlineTracksGlyphScale() {
    ScreenUIDrawBatchBuilder builder;
    for (const float scale : {0.75F, 1.0F, 1.5F, 2.0F}) {
        kb::scene::SceneUIFrame frame{.viewportSize = {640.0F, 360.0F}};
        auto element = Element(30U, {0.0F, 0.0F, 200.0F, 100.0F});
        element.text = kb::scene::UIText{.fontAssetId = 5U};
        Require(kb::scene::SetUITextContent(*element.text, "I"), "SDF outline fixture must contain text");
        element.canvasScale = scale;
        element.outline = kb::scene::UIOutline{.color = {1, 0, 0, 1}, .width = 2.0F};
        frame.elements = {element};
        ScreenUITextRun run{.entity = 30U, .fontAssetId = 5U, .pixelSize = 64U,
            .atlasWidth = 256U, .atlasHeight = 256U};
        run.distanceField = true;
        run.glyphs.push_back(ScreenUIGlyphQuad{.left = 0.0F, .top = 0.0F,
            .right = 32.0F * scale, .bottom = 64.0F * scale,
            .u0 = 0, .v0 = 0, .u1 = 0.125F, .v1 = 0.25F, .distanceField = true});
        const auto& draw = builder.Build(frame, {}, std::span{&run, 1U});
        const auto found = std::ranges::find_if(draw.batches, [](const auto& batch) {
            return batch.style.fragmentKind == ScreenUIFragmentKind::DistanceFieldFont;
        });
        Require(found != draw.batches.end() && NearlyEqual(found->style.fontOutlineWidth, 32.0F / 255.0F),
            "SDF outline must keep its authored distance when canvas pixels scale");
    }
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

void TableCellClipsGlyphAndUnderlineGeometry() {
    kb::scene::SceneUIFrame frame{.viewportSize = {200.0F, 100.0F}};
    auto element = Element(45U, {0.0F, 0.0F, 100.0F, 50.0F});
    element.text = kb::scene::UIText{.fontAssetId = 7U};
    Require(kb::scene::SetUITextContent(*element.text, "x"), "Table clipping fixture failed");
    frame.elements = {element};
    ScreenUITextRun run{.entity = 45U, .fontAssetId = 7U, .pixelSize = 16U, .atlasWidth = 64U, .atlasHeight = 64U};
    run.glyphs.push_back(ScreenUIGlyphQuad{.left = 40.0F, .top = 10.0F, .right = 60.0F, .bottom = 30.0F,
        .u0 = 0.0F, .v0 = 0.0F, .u1 = 1.0F, .v1 = 1.0F, .lineTop = 10.0F, .lineBottom = 30.0F,
        .advanceRight = 60.0F, .advanceLeft = 40.0F, .underline = true,
        .cellClip = true, .cellLeft = 0.0F, .cellRight = 50.0F});
    ScreenUIDrawBatchBuilder builder;
    const std::array<ScreenUITextRun, 1> runs{run};
    const auto& draw = builder.Build(frame, {}, runs);
    Require(draw.vertices.size() == 8U, "Clipped table glyph and underline must each produce a quad");
    Require(std::ranges::all_of(draw.vertices, [](const auto& vertex) { return vertex.x <= 50.01F; }),
            "Clipped table glyphs and underlines must stay inside the cell");
    Require(NearlyEqual(draw.vertices[1].u, 0.5F), "Clipped table glyphs must adjust their atlas UVs");
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

void FontPreparationShapesBidirectionalClustersAndKeepsSharedAtlasCoordinates() {
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    init.swapChain.width = 640U;
    init.swapChain.height = 360U;
    Require(bgfx::init(init), "Text layout test could not initialize the headless renderer");
    {
        auto font = std::make_shared<kb::assets::ImportedAsset>();
        font->category = kb::assets::AssetImportCategory::Font;
        font->sourceExtension = ".ttf";
        const auto path = std::filesystem::path{KB_RENDERER_TEST_SOURCE_ROOT} /
            "sources/editor/content/EditorShell/Fonts/DejaVuSans.ttf";
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        Require(stream.good() && stream.tellg() > 0, "Text layout font fixture is unavailable");
        font->payload.resize(static_cast<std::size_t>(stream.tellg()));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(font->payload.data()), static_cast<std::streamsize>(font->payload.size()));
        Require(stream.good(), "Text layout font fixture could not be read");
        font->sourceSize = font->payload.size();
        kb::assets::AssetManager assets;
        Require(assets.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Font loader registration failed");
        Require(assets.RegisterAsset({.id = kb::assets::AssetId{7U}, .type = "ImportedAsset", .name = "Text font",
                                      .virtualPath = "/Text/Font.kbasset", .runtimeLoadable = true}) &&
                assets.PublishRuntimeAsset(kb::assets::AssetId{7U}, font), "Font fixture publication failed");
        RenderResourceRegistry resources;
        ScreenUIFontAtlasCache fonts;
        kb::scene::SceneUIFrame frame{.viewportSize = {640.0F, 360.0F}};
        auto arabic = Element(70U, {10.0F, 10.0F, 500.0F, 50.0F});
        arabic.text = kb::scene::UIText{.fontAssetId = 7U, .fontSize = 32.0F};
        Require(kb::scene::SetUITextContent(*arabic.text, "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85"), "Arabic fixture failed");
        auto latin = Element(71U, {10.0F, 70.0F, 500.0F, 50.0F});
        latin.text = kb::scene::UIText{.fontAssetId = 7U, .fontSize = 32.0F};
        Require(kb::scene::SetUITextContent(*latin.text, "ABCDEFGHIJKLMNOPQRSTUVWXYZ ffi 0123456789"), "Latin fixture failed");
        frame.elements = {arabic, latin};
        const auto prepared = fonts.Prepare(1U, assets, resources, frame);
        Require(prepared.failureReason == nullptr && prepared.runs.size() == 2U, "Shaped labels failed preparation");
        const auto first = prepared.runs.front();
        Require(first.glyphs.size() == 3U && first.glyphs.front().sourceOffset == 6U &&
                first.glyphs.back().sourceOffset == 0U, "Arabic layout must use joined visual glyphs");
        const auto start = std::ranges::find_if(first.carets, [](const auto& caret) { return caret.sourceOffset == 0U; });
        const auto end = std::ranges::find_if(first.carets, [](const auto& caret) { return caret.sourceOffset == 8U; });
        Require(start != first.carets.end() && end != first.carets.end() && start->x > end->x,
                "Logical Arabic start must have its caret on the visual right");
        frame.elements = {arabic};
        const auto coldCache = fonts.TextCacheStats();
        const auto repeated = fonts.Prepare(1U, assets, resources, frame);
        Require(repeated.runs.size() == 1U, "Repeated shaping lost its text run");
        const auto warmCache = fonts.TextCacheStats();
        Require(warmCache.hits > coldCache.hits && warmCache.misses == coldCache.misses,
                "Unchanged text must reuse shaped runs without invoking the shaper again");
        for (std::size_t i = 0; i < first.glyphs.size(); ++i) {
            const auto& before = first.glyphs[i];
            const auto& after = repeated.runs.front().glyphs[i];
            Require(before.u0 == after.u0 && before.v0 == after.v0 && before.u1 == after.u1 && before.v1 == after.v1,
                    "A later label growing the shared atlas must not invalidate earlier label UVs");
        }
        latin.rect.width = 1.0F;
        latin.text->wrapMode = kb::scene::UITextWrapMode::Character;
        Require(kb::scene::SetUITextContent(*latin.text, "ffi"), "Ligature fixture failed");
        frame.elements = {latin};
        const auto narrow = fonts.Prepare(1U, assets, resources, frame);
        Require(narrow.failureReason == nullptr && narrow.runs.size() == 1U && narrow.runs[0].glyphs.size() == 1U &&
                narrow.runs[0].carets.size() == 4U, "Wrapping must keep a ligature intact while retaining insertion points");
        latin.rect.width = 500.0F;
        latin.inputField = kb::scene::UIInputField{};
        latin.textSelectionBegin = 1U;
        latin.textSelectionEnd = 2U;
        frame.elements = {latin};
        const auto selected = fonts.Prepare(1U, assets, resources, frame);
        Require(selected.failureReason == nullptr && selected.runs.size() == 1U, "Selection layout failed");
        ScreenUIDrawBatchBuilder builder;
        const auto& selectedDraw = builder.Build(frame, {}, selected.runs);
        const auto& selectedStops = selected.runs[0].carets;
        const auto begin = std::ranges::find_if(selectedStops, [](const auto& stop) { return stop.sourceOffset == 1U; });
        const auto finish = std::ranges::find_if(selectedStops, [](const auto& stop) { return stop.sourceOffset == 2U; });
        Require(begin != selectedStops.end() && finish != selectedStops.end() && selectedDraw.vertices.size() >= 8U,
                "A partially selected ligature must retain selection geometry and glyph geometry");
        Require(NearlyEqual(selectedDraw.vertices[0].x, begin->x) &&
                NearlyEqual(selectedDraw.vertices[1].x, finish->x),
                "A selection inside a ligature must highlight only the selected grapheme interval");
        latin.textSelectionBegin = latin.textSelectionEnd = 0U;
        Require(kb::scene::SetUITextContent(*latin.text, ""), "Empty input fixture failed");
        latin.textCaretVisible = true;
        frame.elements = {latin};
        const auto empty = fonts.Prepare(1U, assets, resources, frame);
        Require(empty.failureReason == nullptr && empty.runs.size() == 1U && empty.runs[0].carets.size() == 1U,
                "An empty input field must still provide a caret position");
        Require(!builder.Build(frame, {}, empty.runs).vertices.empty(), "An empty focused input field must draw its caret");
        latin.inputField.reset();
        latin.textCaretVisible = false;
        latin.text->richText = true;
        Require(kb::scene::SetUITextContent(*latin.text, "f<color=#FF0000>fi</color>"), "Styled ligature fixture failed");
        frame.elements = {latin};
        const auto styled = fonts.Prepare(1U, assets, resources, frame);
        Require(styled.failureReason == nullptr && styled.runs.size() == 1U && styled.runs[0].glyphs.size() == 2U &&
                styled.runs[0].glyphs[0].color[1] == 1.0F && styled.runs[0].glyphs[1].color[1] == 0.0F,
                "A ligature must not erase the color boundary inside rich text");
        Require(kb::scene::SetUITextContent(*latin.text, "a<color=#FF0000>\xCC\x81</color>"), "Styled accent fixture failed");
        frame.elements = {latin};
        const auto accent = fonts.Prepare(1U, assets, resources, frame);
        Require(accent.failureReason == nullptr && accent.runs.size() == 1U && accent.runs[0].glyphs.size() == 1U &&
                accent.runs[0].glyphs[0].color[1] == 1.0F,
                "A color boundary inside a grapheme must retain base styling and composed shaping");
        auto scalable = Element(76U, {0.0F, 0.0F, 500.0F, 100.0F});
        scalable.text = kb::scene::UIText{.fontAssetId = 7U, .fontSize = 32.0F};
        Require(kb::scene::SetUITextContent(*scalable.text, "SDF"), "Distance field fixture failed");
        frame.elements = {scalable};
        const auto bitmapPrepared = fonts.Prepare(1U, assets, resources, frame);
        Require(bitmapPrepared.failureReason == nullptr && bitmapPrepared.runs.size() == 1U, "Bitmap comparison failed");
        const auto bitmapRun = bitmapPrepared.runs[0];
        scalable.text->fontRenderMode = kb::scene::UITextFontRenderMode::SignedDistanceField;
        frame.elements = {scalable};
        const auto sdfPrepared = fonts.Prepare(1U, assets, resources, frame);
        Require(sdfPrepared.failureReason == nullptr && sdfPrepared.runs.size() == 1U &&
                sdfPrepared.runs[0].distanceField && sdfPrepared.runs[0].glyphs.size() == bitmapRun.glyphs.size() &&
                std::ranges::all_of(sdfPrepared.runs[0].glyphs, [](const auto& glyph) { return glyph.distanceField; }),
                "SDF text must use a distinct distance field atlas");
        for (std::size_t i = 0U; i < bitmapRun.glyphs.size(); ++i)
            Require(NearlyEqual(bitmapRun.glyphs[i].advanceLeft, sdfPrepared.runs[0].glyphs[i].advanceLeft) &&
                    NearlyEqual(bitmapRun.glyphs[i].advanceRight, sdfPrepared.runs[0].glyphs[i].advanceRight) &&
                    NearlyEqual(bitmapRun.glyphs[i].lineTop, sdfPrepared.runs[0].glyphs[i].lineTop),
                    "SDF and bitmap must retain identical shaped advances and line metrics");
        Require(fonts.Resolve(1U, 7U, 32U, {}, true, resources).idx != bgfx::kInvalidHandle &&
                fonts.Resolve(1U, 7U, 32U, {}, false, resources).idx != bgfx::kInvalidHandle,
                "SDF and bitmap atlas keys must coexist");
        const auto unavailable = fonts.Prepare(1U, assets, resources, frame, false);
        Require(unavailable.failureReason == nullptr && unavailable.runs.size() == 1U &&
                !unavailable.runs[0].distanceField && !unavailable.runs[0].glyphs.empty(),
                "An unavailable SDF shader must select bitmap before preparing text");
        scalable.outline = kb::scene::UIOutline{};
        scalable.outline->width = 12.0F;
        frame.elements = {scalable};
        const auto wideOutline = fonts.Prepare(1U, assets, resources, frame);
        Require(wideOutline.failureReason == nullptr && wideOutline.runs.size() == 1U &&
                !wideOutline.runs[0].distanceField && !wideOutline.runs[0].glyphs.empty(),
                "An outline wider than the field range must retain bitmap text");
        auto table = Element(75U, {0.0F, 0.0F, 100.0F, 200.0F});
        table.text = kb::scene::UIText{.fontAssetId = 7U, .fontSize = 24.0F};
        const std::string tableSource = "<table><tr><td>A</td><td><link=cell>BBBBBBBB</link></td></tr><tr><td>Z</td><td></td></tr></table>";
        Require(kb::scene::SetUITextContent(*table.text, tableSource), "Table fixture failed");
        frame.elements = {table};
        const auto tablePrepared = fonts.Prepare(1U, assets, resources, frame);
        Require(tablePrepared.failureReason == nullptr && tablePrepared.runs.size() == 1U &&
                tablePrepared.runs[0].links.size() == 1U, "Table layout must retain one run and its link");
        const auto& tableGlyphs = tablePrepared.runs[0].glyphs;
        const auto a = std::ranges::find_if(tableGlyphs, [&](const auto& glyph) { return glyph.sourceOffset == tableSource.find('A'); });
        const auto b = std::ranges::find_if(tableGlyphs, [&](const auto& glyph) { return glyph.sourceOffset == tableSource.find("BBBB"); });
        const auto z = std::ranges::find_if(tableGlyphs, [&](const auto& glyph) { return glyph.sourceOffset == tableSource.find('Z'); });
        Require(a != tableGlyphs.end() && b != tableGlyphs.end() && z != tableGlyphs.end() &&
                b->advanceLeft >= 50.0F && NearlyEqual(a->lineTop, b->lineTop) && z->lineTop > b->lineTop && b->link == 1U,
                "Table columns must share a first baseline and reserve the wrapped row height");
        table.text->maxLines = 1U;
        table.text->overflow = kb::scene::UITextOverflow::Truncate;
        frame.elements = {table};
        const auto truncatedTable = fonts.Prepare(1U, assets, resources, frame);
        Require(truncatedTable.failureReason == nullptr && truncatedTable.runs.size() == 1U &&
                std::ranges::none_of(truncatedTable.runs[0].glyphs, [&](const auto& glyph) { return glyph.sourceOffset == tableSource.find('Z'); }) &&
                std::ranges::all_of(truncatedTable.runs[0].glyphs, [](const auto& glyph) { return glyph.cellClip; }),
                "Table maxLines must truncate all columns together and clip their contents");
        table.text->maxLines = 0U;
        table.text->autoSize = true;
        table.text->minFontSize = 8.0F;
        table.rect.height = 35.0F;
        frame.elements = {table};
        const auto fittedTable = fonts.Prepare(1U, assets, resources, frame);
        Require(fittedTable.failureReason == nullptr && fittedTable.runs.size() == 1U && fittedTable.runs[0].pixelSize < 24U,
                "Table autosize must measure wrapped cell rows rather than flattened text");
        latin.text->richText = false;
        Require(kb::scene::SetUITextContent(*latin.text, "a\n"), "Empty final line fixture failed");
        frame.elements = {latin};
        const auto trailing = fonts.Prepare(1U, assets, resources, frame);
        Require(trailing.failureReason == nullptr && trailing.runs.size() == 1U, "Trailing line break shaping failed");
        const auto& stops = trailing.runs[0].carets;
        const auto lastStop = std::ranges::find_if(stops, [](const auto& caret) { return caret.sourceOffset == 2U; });
        Require(lastStop != stops.end() && lastStop->top > stops.front().top,
                "A caret after a final newline must belong to the empty next line");
        latin.text->wrapMode = kb::scene::UITextWrapMode::NoWrap;
        for (unsigned variant = 0U; variant < 100U; ++variant) {
            Require(kb::scene::SetUITextContent(*latin.text, std::string(500U, 'a') + std::to_string(variant)),
                    "Typography cache pressure fixture failed");
            frame.elements = {latin};
            Require(fonts.Prepare(1U, assets, resources, frame).failureReason == nullptr,
                    "Typography cache eviction must retain correct text preparation");
            const auto pressure = fonts.TextCacheStats();
            Require(pressure.entries <= 32U && pressure.payloadBytes <= 64U * 1024U,
                    "Per-font typography cache must stay within its entry and payload budgets");
        }
        const auto beforeReload = fonts.TextCacheStats();
        Require(assets.PublishRuntimeAsset(kb::assets::AssetId{7U}, std::make_shared<kb::assets::ImportedAsset>(*font)),
                "Font replacement fixture failed");
        Require(fonts.Prepare(1U, assets, resources, frame).failureReason == nullptr &&
                fonts.TextCacheStats().misses > beforeReload.misses,
                "A new font generation must invalidate cached shaping results");
        auto caretFont = std::make_shared<kb::assets::ImportedAsset>(*font);
        std::ifstream caretStream(std::filesystem::path{KB_RENDERER_TEST_SOURCE_ROOT} /
            "sources/engine/tests/fixtures/fonts/CaretTest.ttf", std::ios::binary | std::ios::ate);
        Require(caretStream.good() && caretStream.tellg() > 0, "GDEF font fixture missing");
        caretFont->payload.resize(static_cast<std::size_t>(caretStream.tellg()));
        caretStream.seekg(0);
        caretStream.read(reinterpret_cast<char*>(caretFont->payload.data()), static_cast<std::streamsize>(caretFont->payload.size()));
        caretFont->sourceSize = caretFont->payload.size();
        Require(assets.PublishRuntimeAsset(kb::assets::AssetId{7U}, caretFont), "GDEF font publication failed");
        Require(kb::scene::SetUITextContent(*latin.text, "fi"), "GDEF text fixture failed");
        frame.elements = {latin};
        const auto gdef = fonts.Prepare(1U, assets, resources, frame);
        Require(gdef.failureReason == nullptr && gdef.runs.size() == 1U && gdef.runs[0].carets.size() == 3U,
                "GDEF ligature must expose all insertion positions");
        const auto& gdefStops = gdef.runs[0].carets;
        Require(std::abs((gdefStops[1].x - gdefStops[0].x) /
                (gdefStops[2].x - gdefStops[0].x) - 0.2F) < 0.001F,
                "Rendered caret must use the font's GDEF position rather than equal-width division");
        auto complexFont = std::make_shared<kb::assets::ImportedAsset>(*font);
        std::ifstream complexStream(std::filesystem::path{KB_RENDERER_TEST_SOURCE_ROOT} /
            "sources/engine/tests/fixtures/fonts/NotoSansDevanagari-Regular.ttf", std::ios::binary | std::ios::ate);
        Require(complexStream.good() && complexStream.tellg() > 0, "Complex-script font fixture missing");
        complexFont->payload.resize(static_cast<std::size_t>(complexStream.tellg()));
        complexStream.seekg(0);
        complexStream.read(reinterpret_cast<char*>(complexFont->payload.data()), static_cast<std::streamsize>(complexFont->payload.size()));
        complexFont->sourceSize = complexFont->payload.size();
        Require(assets.PublishRuntimeAsset(kb::assets::AssetId{7U}, complexFont), "Complex-script font publication failed");
        const std::string devanagari = "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87";
        Require(kb::scene::SetUITextContent(*latin.text, devanagari), "Complex-script text fixture failed");
        frame.elements = {latin};
        const auto complex = fonts.Prepare(1U, assets, resources, frame);
        Require(complex.failureReason == nullptr && complex.runs.size() == 1U && complex.runs[0].glyphs.size() == 5U,
                "Complex-script rendering must keep contextual half-forms and vowel signs");
        for (const auto& caret : complex.runs[0].carets)
            Require(kb::scene::SnapToUITextGraphemeBoundary(devanagari, caret.sourceOffset) == caret.sourceOffset,
                    "A shaping subcluster must never expose a caret inside an Indic grapheme");
        const auto& complexStops = complex.runs[0].carets;
        Require(!complexStops.empty() && complexStops.front().sourceOffset == 0U &&
                complexStops.back().sourceOffset == devanagari.size() && complexStops.back().x > complexStops.front().x,
                "Complex-script caret endpoints must cover the complete shaped line");
        Require(assets.RegisterAsset({.id = kb::assets::AssetId{8U}, .type = "ImportedAsset", .name = "Fallback font",
                                      .virtualPath = "/Text/Fallback.kbasset", .runtimeLoadable = true}) &&
                assets.PublishRuntimeAsset(kb::assets::AssetId{8U}, font), "Fallback font fixture publication failed");
        latin.text->fallbackFontAssetId = 8U;
        latin.text->fontSize = 40.0F;
        Require(kb::scene::SetUITextContent(*latin.text, "ffi " + devanagari), "Mixed-font fixture failed");
        frame.elements = {latin};
        const auto mixed = fonts.Prepare(1U, assets, resources, frame);
        Require(mixed.failureReason == nullptr && mixed.runs.size() == 1U &&
                std::ranges::any_of(mixed.runs[0].glyphs, [](const auto& glyph) { return glyph.fallbackFont; }) &&
                std::ranges::any_of(mixed.runs[0].glyphs, [](const auto& glyph) { return !glyph.fallbackFont; }),
                "Mixed text must retain both primary and fallback glyphs in one run");
        Require(fonts.Resolve(1U, 7U, 40U, {}, false, resources).idx != bgfx::kInvalidHandle &&
                fonts.Resolve(1U, 8U, 40U, {}, false, resources).idx != bgfx::kInvalidHandle,
                "Both font atlases must resolve");
        Require(kb::scene::SetUITextContent(*latin.text, "\xE0\xA4\x95\xE0\xA4\xBFZ"), "Spacing fixture failed");
        latin.text->characterSpacing = 0.0F;
        frame.elements = {latin};
        const auto unspaced = fonts.Prepare(1U, assets, resources, frame);
        Require(unspaced.failureReason == nullptr && unspaced.runs.size() == 1U, "Unspaced mixed text failed");
        const auto unspacedGlyph = std::ranges::find_if(unspaced.runs[0].glyphs, [](const auto& glyph) { return glyph.fallbackFont; });
        Require(unspacedGlyph != unspaced.runs[0].glyphs.end(), "Spacing fixture did not select fallback");
        const float previousX = unspacedGlyph->advanceLeft;
        latin.text->characterSpacing = 5.0F;
        frame.elements = {latin};
        const auto spaced = fonts.Prepare(1U, assets, resources, frame);
        Require(spaced.failureReason == nullptr && spaced.runs.size() == 1U, "Spaced mixed text failed");
        const auto spacedGlyph = std::ranges::find_if(spaced.runs[0].glyphs, [](const auto& glyph) { return glyph.fallbackFont; });
        Require(spacedGlyph != spaced.runs[0].glyphs.end() && std::abs(spacedGlyph->advanceLeft - previousX - 5.0F) < 0.01F,
                "Character spacing must occur once after the full grapheme");
        latin.text->characterSpacing = 0.0F;
        Require(kb::scene::SetUITextContent(*latin.text, "a\r\nb"), "CRLF text fixture failed");
        frame.elements = {latin};
        const auto crlf = fonts.Prepare(1U, assets, resources, frame);
        Require(crlf.failureReason == nullptr && crlf.runs.size() == 1U && crlf.runs[0].glyphs.size() == 2U &&
                crlf.runs[0].glyphs[1].lineTop > crlf.runs[0].glyphs[0].lineTop,
                "CRLF must form one line break and must not draw control glyphs");
        frame.language = "en";
        Require(fonts.Prepare(1U, assets, resources, frame).failureReason == nullptr, "English text preparation failed");
        const auto beforeLanguageChange = fonts.TextCacheStats();
        frame.language = "tr";
        Require(fonts.Prepare(1U, assets, resources, frame).failureReason == nullptr &&
                fonts.TextCacheStats().misses > beforeLanguageChange.misses,
                "Changing language must re-shape cached text");
        const auto beforeReturn = fonts.TextCacheStats();
        frame.language = "en";
        Require(fonts.Prepare(1U, assets, resources, frame).failureReason == nullptr &&
                fonts.TextCacheStats().hits > beforeReturn.hits,
                "Returning to a language must reuse its matching cached shaping");
        latin.text->fontSize = 41.0F;
        frame.elements = {latin};
        Require(fonts.Prepare(1U, assets, resources, frame).failureReason == nullptr, "Temporary font size failed");
        latin.text->fontSize = 40.0F;
        frame.elements = {latin};
        for (unsigned i = 0; i < 120U; ++i)
            Require(fonts.Prepare(1U, assets, resources, frame).failureReason == nullptr, "Repeated font preparation failed");
        Require(fonts.Resolve(1U, 7U, 41U, {}, false, resources).idx == bgfx::kInvalidHandle,
                "Unused font-size atlas must be released after subsequent frames");
        latin.text->fontAssetId = 8U;
        latin.text->fallbackFontAssetId = 0U;
        Require(kb::scene::SetUITextContent(*latin.text, "\xD0\xB1"), "Locale-sensitive glyph fixture failed");
        frame.language = "sr";
        frame.elements = {latin};
        const auto serbian = fonts.Prepare(1U, assets, resources, frame);
        Require(serbian.failureReason == nullptr && serbian.runs.size() == 1U && serbian.runs[0].glyphs.size() == 1U,
                "Serbian glyph preparation failed");
        const auto serbianGlyph = serbian.runs[0].glyphs[0].glyphId;
        frame.language = "ru";
        const auto russian = fonts.Prepare(1U, assets, resources, frame);
        Require(russian.failureReason == nullptr && russian.runs.size() == 1U && russian.runs[0].glyphs.size() == 1U &&
                russian.runs[0].glyphs[0].glyphId != serbianGlyph,
                "Locale changes must select the font's language-specific glyph");
        latin.text->richText = false;
        std::string manyGlyphs;
        for (char c = 33; c <= 126; ++c) manyGlyphs.push_back(c);
        Require(kb::scene::SetUITextContent(*latin.text, manyGlyphs), "Atlas compaction fixture failed");
        frame.elements = {latin};
        const auto crowded = fonts.Prepare(1U, assets, resources, frame);
        Require(crowded.failureReason == nullptr && crowded.runs.size() == 1U, "Crowded atlas preparation failed");
        const auto crowdedWidth = crowded.runs[0].atlasWidth;
        Require(kb::scene::SetUITextContent(*latin.text, "a"), "Atlas compaction replacement failed");
        frame.elements = {latin};
        for (unsigned i = 0; i < 121U; ++i)
            Require(fonts.Prepare(1U, assets, resources, frame).failureReason == nullptr, "Atlas compaction lost active text");
        const auto compacted = fonts.Prepare(1U, assets, resources, frame);
        Require(compacted.failureReason == nullptr && compacted.runs.size() == 1U &&
                compacted.runs[0].atlasWidth < crowdedWidth, "Atlas compaction must discard unused historical glyphs");
        fonts.ReleaseScene(1U, resources);
        Require(fonts.TextCacheStats().entries == 0U && fonts.TextCacheStats().payloadBytes == 0U,
                "Unloading a scene must release its typography cache");
        fonts.Shutdown(resources);
        resources.Shutdown();
    }
    bgfx::shutdown();
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

void DistanceFieldHandlesUnrepresentableCoverage() {
    std::vector<std::uint8_t> result;
    const std::array<std::uint8_t, 1U> thin{127U};
    Require(!BuildFontDistanceField(thin, 1, 1, result) && result.empty(),
            "A contour with no interior must report fallback instead of producing invisible text");
    const std::array<std::uint8_t, 1U> solid{255U};
    Require(BuildFontDistanceField(solid, 1, 1, result) && result.size() == 17U * 17U && result[8U * 17U + 8U] > 128U,
            "An SDF must retain the inside of a visible pixel with padding");
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

// The markup contract: tags nest and scope colours, anything that is not a complete tag stays literal text and never
// fails, only broken UTF-8 fails (and then yields no glyphs), the work is bounded by the text, and every glyph points
// back at its own byte in the authored string, in order.
void RichTextParserKeepsItsContractOnNestedAndMalformedMarkup() {
    const auto text = [](const ScreenUITextMarkup& markup) {
        std::string result;
        for (const ScreenUITextMarkupGlyph& glyph : markup.glyphs) result.push_back(static_cast<char>(glyph.codepoint));
        return result;
    };
    const std::array<float, 4U> white{ 1.0F, 1.0F, 1.0F, 1.0F };
    const std::array<float, 4U> red{ 1.0F, 0.0F, 0.0F, 1.0F };
    const std::array<float, 4U> green{ 0.0F, 1.0F, 0.0F, 1.0F };

    const std::string nested = "<color=#FF0000><color=#00FF00>x</color>y</color>z";
    const ScreenUITextMarkup parsed = ScreenUITextMarkupParser{}.Parse(nested, true);
    Require(parsed.succeeded && text(parsed) == "xyz" && parsed.glyphs[0U].color == green && parsed.glyphs[1U].color == red &&
                parsed.glyphs[2U].color == white,
        "Nested colour tags must scope to the innermost open tag and restore the outer one");
    Require(nested[parsed.glyphs[0U].sourceOffset] == 'x' && nested[parsed.glyphs[1U].sourceOffset] == 'y' &&
                nested[parsed.glyphs[2U].sourceOffset] == 'z',
        "Every glyph must point at its own byte of the authored string");

    // Not a complete tag: literal, and parsing still succeeds.
    for (const std::string_view literal : { "<color=#12", "<color=#GGGGGG>x", "<color=#FF0000", "<br", "<>", "</color>", "a</color>b",
                                            "<color=#FF00>x", "<color=FF0000>x", "<COLOR=#FF0000>x", "<br />" }) {
        const ScreenUITextMarkup result = ScreenUITextMarkupParser{}.Parse(literal, true);
        Require(result.succeeded && text(result) == literal,
            (std::string{ "Text that is not a complete tag must stay literal: " } + std::string{ literal }).c_str());
    }
    // An unclosed colour tag colours the rest of the text and is not an error.
    const ScreenUITextMarkup unclosed = ScreenUITextMarkupParser{}.Parse("<color=#FF0000>abc", true);
    Require(unclosed.succeeded && text(unclosed) == "abc" && unclosed.glyphs[2U].color == red, "An unclosed colour tag must colour the rest of the text");
    // Eight digits carry alpha; a break tag inside a colour keeps the colour.
    const ScreenUITextMarkup alpha = ScreenUITextMarkupParser{}.Parse("<color=#FF000080>a<br/>b</color>", true);
    Require(alpha.succeeded && text(alpha) == "a\nb" && NearlyEqual(alpha.glyphs[1U].color[3U], 128.0F / 255.0F) && alpha.glyphs[1U].color == alpha.glyphs[0U].color,
        "A colour with alpha must apply to a line break inside it");

    // Broken UTF-8 is the only failure and it leaves no glyphs behind.
    for (const std::string_view broken : { std::string_view{ "\xC3" }, std::string_view{ "ok\xF0\x9F" }, std::string_view{ "<color=#FF0000>\xFF</color>" },
                                           std::string_view{ "\x80" } }) {
        const ScreenUITextMarkup result = ScreenUITextMarkupParser{}.Parse(broken, true);
        Require(!result.succeeded && result.glyphs.empty(), "Broken UTF-8 must fail without leaving glyphs");
    }

    // The longest text a label holds, made only of tags, is bounded work and produces nothing visible.
    std::string tags;
    while (tags.size() + 15U <= 1023U) tags += "<color=#FFFFFF>";
    const ScreenUITextMarkup onlyTags = ScreenUITextMarkupParser{}.Parse(tags, true);
    Require(onlyTags.succeeded && onlyTags.glyphs.empty(), "A text of nothing but tags must parse to nothing");
    std::string closes;
    while (closes.size() + 8U <= 1023U) closes += "</color>";
    Require(ScreenUITextMarkupParser{}.Parse(closes, true).succeeded, "Closing tags without an opening one must be literal text");

    // Random text from the characters of the markup itself: it never crashes, offsets only grow and every glyph
    // points inside the text.
    const std::string alphabet = "<>/=#colr bF0xyz\n\xC3\xA9";
    std::uint32_t state = 0xC0FFEEU;
    for (int round = 0; round < 2000; ++round) {
        std::string noise;
        const std::size_t length = static_cast<std::size_t>(round % 70);
        for (std::size_t index = 0U; index < length; ++index) {
            state = state * 1664525U + 1013904223U;
            noise.push_back(alphabet[(state >> 16U) % alphabet.size()]);
        }
        const ScreenUITextMarkup result = ScreenUITextMarkupParser{}.Parse(noise, true);
        if (!result.succeeded) {
            Require(result.glyphs.empty(), "A failed parse must leave no glyphs");
            continue;
        }
        std::uint32_t previous = 0U;
        for (std::size_t index = 0U; index < result.glyphs.size(); ++index) {
            Require(result.glyphs[index].sourceOffset < noise.size() && (index == 0U || result.glyphs[index].sourceOffset >= previous),
                "Glyph offsets must lie inside the text and never go backwards");
            previous = result.glyphs[index].sourceOffset;
        }
        Require(result.glyphs.size() <= noise.size(), "Markup can only remove characters, never add them");
    }
}

void RichTextParserScopesLinkIdentifiers() {
    const auto parsed = ScreenUITextMarkupParser{}.Parse(
        "a<link=first>b<color=#FF0000>c<link=second>d</link>e</color>f</link>g", true, true);
    Require(parsed.succeeded && parsed.links == std::vector<std::string>{"first", "second"} &&
            parsed.glyphs.size() == 7U, "Link targets and visible text must be retained");
    const std::array<std::uint32_t, 7U> expected{0U, 1U, 1U, 2U, 1U, 1U, 0U};
    for (std::size_t i = 0U; i < expected.size(); ++i)
        Require(parsed.glyphs[i].link == expected[i], "Nested link scope must be restored");
    const auto literal = ScreenUITextMarkupParser{}.Parse("<link=>a</link>", true, true);
    Require(literal.succeeded && literal.links.empty() && literal.glyphs.size() == 15U,
            "Malformed links must remain literal");
    const auto disabled = ScreenUITextMarkupParser{}.Parse("<link=x>a</link>", true);
    Require(disabled.links.empty() && disabled.glyphs.size() == 16U,
            "Link parsing must remain opt-in until event handling is available");
}

void RichTextParserScopesUnderline() {
    const auto result = ScreenUITextMarkupParser{}.Parse("a<u>b<u>c</u>d</u>e</u>", true);
    Require(result.succeeded && result.glyphs.size() == 9U &&
            !result.glyphs[0].underline && result.glyphs[1].underline &&
            result.glyphs[2].underline && result.glyphs[3].underline && !result.glyphs[4].underline,
            "Nested underline tags must restore their enclosing scope and leave stray closes literal");
}

void RichTextParserTablesAreOptInAndAtomic() {
    const std::string source = "a<color=#FF0000><link=same><table><tr><td><u>x</u></td><td>y<br>z</td></tr><tr><td></td><td><color=#00FF00>g</td></tr></table>r</link></color>b";
    const auto parsed = ScreenUITextMarkupParser{}.Parse(source, true, true, true);
    Require(parsed.succeeded && parsed.blocks.size() == 3U && !parsed.blocks[0].table &&
            parsed.blocks[1].table && !parsed.blocks[2].table && parsed.blocks[1].rows.size() == 2U &&
            parsed.blocks[1].rows[0].cells.size() == 2U && parsed.blocks[1].rows[1].cells.size() == 2U,
            "Tables must preserve ordered text blocks and rectangular cells");
    const auto& first = parsed.blocks[1].rows[0].cells[0];
    const auto& second = parsed.blocks[1].rows[0].cells[1];
    const auto& empty = parsed.blocks[1].rows[1].cells[0];
    const auto& green = parsed.blocks[1].rows[1].cells[1];
    Require(first.end - first.begin == 1U && second.end - second.begin == 3U && empty.begin == empty.end &&
            green.end - green.begin == 1U && parsed.glyphs[first.begin].underline &&
            !parsed.glyphs[second.begin].underline && parsed.glyphs[second.begin + 1U].codepoint == '\n' &&
            parsed.glyphs[first.begin].link == 1U && parsed.glyphs[second.begin].link == 1U &&
            parsed.glyphs[green.begin].color[1] == 1.0F && parsed.glyphs[green.begin].color[0] == 0.0F &&
            parsed.glyphs[parsed.blocks[2].text.begin].color[0] == 1.0F &&
            parsed.glyphs[parsed.blocks[2].text.begin].color[1] == 0.0F,
            "Cell styles and inherited link scopes must remain isolated");
    Require(source[parsed.glyphs[first.begin].sourceOffset] == 'x' &&
            source[parsed.glyphs[second.begin].sourceOffset] == 'y', "Cell glyph offsets must refer to source bytes");
    const auto scopes = ScreenUITextMarkupParser{}.Parse(
        "<u><link=same><table><tr><td></u></link>x</td><td><link=same>y</link>z</td></tr></table>q</link></u>", true, true, true);
    Require(scopes.succeeded && scopes.links == std::vector<std::string>{"same", "same"} &&
            scopes.glyphs.size() == 4U && !scopes.glyphs[0].underline && scopes.glyphs[0].link == 0U &&
            scopes.glyphs[1].underline && scopes.glyphs[1].link == 2U &&
            scopes.glyphs[2].underline && scopes.glyphs[2].link == 1U &&
            scopes.glyphs[3].underline && scopes.glyphs[3].link == 1U,
            "Cell closures must not escape, and separate link openings need distinct identities");
    const auto defaultMode = ScreenUITextMarkupParser{}.Parse("<table><tr><td>x</td></tr></table>", true);
    Require(defaultMode.blocks.empty() && defaultMode.glyphs.size() == std::string_view{"<table><tr><td>x</td></tr></table>"}.size(),
            "Table parsing must be disabled by default");
    for (const std::string_view bad : {"<table><tr><td><u>x</u></tr></table>",
                                       "<table><tr><td>x</td><td>y</td></tr><tr><td>z</td></tr></table>",
                                       "<table><tr><td><table><tr><td>x</td></tr></table></td></tr></table>",
                                       "<table><tr><td>x"}) {
        const auto literal = ScreenUITextMarkupParser{}.Parse(bad, true, true, true);
        Require(literal.succeeded && literal.blocks.size() == 1U && !literal.blocks[0].table &&
                literal.glyphs.size() == bad.size() && literal.links.empty(),
                "Malformed tables must remain wholly literal without partial styles");
        for (std::size_t i = 0U; i < bad.size(); ++i)
            Require(literal.glyphs[i].codepoint == static_cast<unsigned char>(bad[i]), "Malformed table lost source text");
    }
    const std::string broken = "<table><tr><td>\xFF</td></tr></table>";
    const auto invalidUtf8 = ScreenUITextMarkupParser{}.Parse(broken, true, true, true);
    Require(!invalidUtf8.succeeded && invalidUtf8.glyphs.empty() && invalidUtf8.links.empty() && invalidUtf8.blocks.empty(),
            "Malformed UTF-8 inside a table must fail atomically");
}

} // namespace

void RunScreenUIDrawBatchTests() {
    DefaultImagesAndControlsProduceVisibleGeometry();
    BatchBuilderPreservesFlattenedOrderCornersAndClip();
    BatchBuilderEmitsNineSliceTintAndEffects();
    BatchBuilderUsesTextRunAndHonorsHiddenMask();
    BatchBuilderAppliesInteractionTintToEveryVisualLayer();
    FocusedInputFieldDrawsItsCaretAtTheEditedOffset();
    DistanceFieldOutlineTracksGlyphScale();
    TableCellClipsGlyphAndUnderlineGeometry();
    FontPreparationDropsOnlyTheTextItCannotPrepare();
    FontPreparationShapesBidirectionalClustersAndKeepsSharedAtlasCoordinates();
    ImportCatalogOffersOnlyRasterizableFontFormats();
    FontPayloadValidatorRejectsUnsupportedAndTruncatedData();
    DistanceFieldHandlesUnrepresentableCoverage();
    RichTextParserAppliesColorAndLineBreakWithoutChangingLiteralText();
    RichTextParserKeepsItsContractOnNestedAndMalformedMarkup();
    RichTextParserScopesLinkIdentifiers();
    RichTextParserScopesUnderline();
    RichTextParserTablesAreOptInAndAtomic();
}


} // namespace kb::render::tests
