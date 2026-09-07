#include "RendererTestSupport.hpp"

#include "kb/render/frame/EditorRenderPassSubmitter.hpp"
#include "private/ui/UserWidgetDrawBatchBuilder.hpp"
#include "private/ui/UserWidgetFontPayloadValidator.hpp"

#include <cmath>
#include <vector>

namespace kb::render::tests {
namespace {

[[nodiscard]] kb::scene::UIPresentationItem Shape(kb::scene::UIElementId id, float left, float top,
                                                  const kb::math::Color& color) {
    return kb::scene::UIPresentationItem{
        .owner = kb::scene::SceneEntity{1U},
        .elementId = id,
        .rect = kb::scene::UIPresentationRect{left, top, left + 100.0F, top + 40.0F},
        .clipRect = kb::scene::UIPresentationRect{0.0F, 0.0F, 400.0F, 300.0F},
        .paint =
            kb::scene::UIPaint{
                .backgroundColor = color,
                .cornerRadius = kb::math::Vec4{4.0F, 4.0F, 4.0F, 4.0F},
            },
    };
}

void AdjacentCompatibleDrawsBatchWithoutChangingPainterOrder() {
    kb::scene::UIPresentationSnapshot snapshot{
        .viewportWidth = 400U,
        .viewportHeight = 300U,
    };
    snapshot.items.push_back(Shape(1U, 10.0F, 10.0F, kb::math::Color{1.0F, 0.0F, 0.0F, 1.0F}));
    snapshot.items.push_back(Shape(2U, 10.0F, 60.0F, kb::math::Color{1.0F, 0.0F, 0.0F, 1.0F}));
    snapshot.items.push_back(Shape(3U, 10.0F, 110.0F, kb::math::Color{0.0F, 1.0F, 0.0F, 1.0F}));

    UserWidgetDrawBatchBuilder builder;
    const UserWidgetDrawList& list = builder.Build(snapshot, {}, {});
    Require(list.vertices.size() == 12U && list.indices.size() == 18U,
            "Runtime UI batcher did not emit three ordered quads");
    Require(list.batches.size() == 2U, "Runtime UI batcher did not merge only adjacent compatible shapes");
    Require(list.batches[0].firstIndex == 0U && list.batches[0].indexCount == 12U &&
                list.batches[1].firstIndex == 12U && list.batches[1].indexCount == 6U,
            "Runtime UI batcher changed painter order while merging shapes");
}

void NineSliceUsesOneTextureBatchAndNineQuads() {
    kb::scene::UIPresentationSnapshot snapshot{
        .viewportWidth = 400U,
        .viewportHeight = 300U,
    };
    kb::scene::UIPresentationItem item = Shape(1U, 20.0F, 30.0F, kb::math::Color{});
    item.paint.reset();
    item.image = kb::scene::UIImage{
        .imageAssetId = 77U,
        .nineSlice = kb::scene::UIEdges{8.0F, 8.0F, 8.0F, 8.0F},
    };
    snapshot.items.push_back(item);
    constexpr UserWidgetImageBinding image{.assetId = 77U, .width = 64U, .height = 64U};

    UserWidgetDrawBatchBuilder builder;
    const UserWidgetDrawList& list = builder.Build(snapshot, std::span{&image, 1U}, {});
    Require(list.vertices.size() == 36U && list.indices.size() == 54U, "Runtime UI nine-slice did not emit nine quads");
    Require(list.batches.size() == 1U && list.batches[0].texture.assetId == 77U && list.batches[0].indexCount == 54U,
            "Runtime UI nine-slice did not remain one adjacent image batch");
}

void TransformClipAndBlurRemainInCanonicalDrawData() {
    kb::scene::UIPresentationSnapshot snapshot{
        .viewportWidth = 200U,
        .viewportHeight = 100U,
    };
    kb::scene::UIPresentationItem item = Shape(1U, 20.0F, 20.0F, kb::math::Color{0.0F, 0.0F, 0.0F, 0.5F});
    item.clipRect = kb::scene::UIPresentationRect{25.0F, 25.0F, 80.0F, 55.0F};
    item.pivot = kb::math::Vec2{0.5F, 0.5F};
    item.scale = kb::math::Vec2{2.0F, 1.0F};
    item.rotationDegrees = 90.0F;
    item.effects = kb::scene::UIEffects{.backgroundBlur = 12.0F};
    snapshot.items.push_back(item);
    kb::scene::UIPresentationItem weaker = Shape(2U, 100.0F, 20.0F, kb::math::Color{});
    weaker.effects = kb::scene::UIEffects{.backgroundBlur = 6.0F};
    snapshot.items.push_back(weaker);

    UserWidgetDrawBatchBuilder builder;
    const UserWidgetDrawList& list = builder.Build(snapshot, {}, {});
    Require(list.requiresBackgroundBlur && list.maximumBackgroundBlur == 12.0F,
            "Runtime UI batcher dropped the authored background blur");
    Require(!list.batches.empty() && list.batches[0].texture.source == UserWidgetTextureSource::BackgroundBlur &&
                list.batches[0].clipRect.left == 25.0F && list.batches[0].clipRect.right == 80.0F,
            "Runtime UI batcher changed the shared layout clip rectangle");
    Require(std::abs(list.vertices[0].x - 90.0F) < 0.01F && std::abs(list.vertices[0].y + 60.0F) < 0.01F,
            "Runtime UI batcher did not apply pivoted scale and rotation to geometry");
    Require(std::abs(list.vertices[0].u - 0.1F) < 0.001F && std::abs(list.vertices[0].v - 0.2F) < 0.001F,
            "Runtime UI blur quad does not sample the matching viewport region");
    Require(list.batches.size() >= 3U && std::abs(list.batches[2U].style.opacity - 0.5F) < 0.001F,
            "Runtime UI blur strength is not preserved relative to the viewport blur radius");
}

void RuntimeControlStateProducesDefaultPrimitives() {
    kb::scene::UIPresentationSnapshot snapshot{
        .viewportWidth = 400U,
        .viewportHeight = 300U,
    };
    kb::scene::UIPresentationItem slider = Shape(1U, 10.0F, 10.0F, {});
    slider.paint.reset();
    slider.controlKind = kb::scene::UIControlKind::Slider;
    slider.value = 25.0F;
    slider.minimum = 0.0F;
    slider.maximum = 100.0F;
    snapshot.items.push_back(slider);

    kb::scene::UIPresentationItem toggle = Shape(2U, 10.0F, 60.0F, {});
    toggle.paint.reset();
    toggle.controlKind = kb::scene::UIControlKind::Toggle;
    toggle.toggleValue = true;
    snapshot.items.push_back(toggle);

    kb::scene::UIPresentationItem button = Shape(3U, 10.0F, 110.0F, {});
    button.paint.reset();
    button.controlKind = kb::scene::UIControlKind::Button;
    snapshot.items.push_back(button);

    UserWidgetDrawBatchBuilder builder;
    const UserWidgetDrawList& list = builder.Build(snapshot, {}, {});
    Require(list.vertices.size() == 20U && list.indices.size() == 30U,
            "Runtime UI controls did not emit slider track/fill, toggle box/check and button background");
    Require(std::abs(list.vertices[5U].x - 35.0F) < 0.001F,
            "Runtime UI slider fill does not represent its normalized runtime value");
}

void CorruptAndUnsupportedFontPayloadsAreRejectedBeforeRasterization() {
    kb::assets::ImportedAsset tooSmall{
        .category = kb::assets::AssetImportCategory::Font,
        .sourceExtension = ".ttf",
        .sourceSize = UserWidgetFontPayloadValidator::kMinimumPayloadBytes - 1U,
        .payload = std::vector<std::byte>(UserWidgetFontPayloadValidator::kMinimumPayloadBytes - 1U),
    };
    Require(!UserWidgetFontPayloadValidator::Validate(tooSmall),
            "Runtime UI accepted a truncated font payload before rasterization");

    kb::assets::ImportedAsset corruptDirectory{
        .category = kb::assets::AssetImportCategory::Font,
        .sourceExtension = ".otf",
        .sourceSize = 64U,
        .payload = std::vector<std::byte>(64U),
    };
    const auto put16 = [&](std::size_t offset, std::uint16_t value) {
        corruptDirectory.payload[offset] = static_cast<std::byte>(value >> 8U);
        corruptDirectory.payload[offset + 1U] = static_cast<std::byte>(value);
    };
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        corruptDirectory.payload[offset] = static_cast<std::byte>(value >> 24U);
        corruptDirectory.payload[offset + 1U] = static_cast<std::byte>(value >> 16U);
        corruptDirectory.payload[offset + 2U] = static_cast<std::byte>(value >> 8U);
        corruptDirectory.payload[offset + 3U] = static_cast<std::byte>(value);
    };
    put32(0U, 0x00010000U);
    put16(4U, 1U);
    put32(12U, 0x636D6170U);
    put32(20U, 0xFFFFFFF0U);
    put32(24U, 32U);
    Require(!UserWidgetFontPayloadValidator::Validate(corruptDirectory),
            "Runtime UI accepted a font table outside the bounded ImportedAsset payload");
    Require(!UserWidgetFontPayloadValidator::SupportsExtension(".woff2"),
            "Runtime UI advertised an unsupported font extension");
}

void SharedUiCompositeViewPreservesLetterboxedOutputRect() {
    RenderSceneSubmitDesc desc{};
    desc.finalComposite.enabled = true;
    desc.finalComposite.extent = RenderExtent{1920U, 1080U};
    desc.finalComposite.outputRect = RenderViewportRect{
        .x = 240U,
        .y = 135U,
        .extent = RenderExtent{1440U, 810U},
    };
    Require(EditorRenderPassSubmitter::ResolveUiCompositeViewRect(desc) == desc.finalComposite.outputRect,
            "Editor overlay replaced the shared runtime UI view with the full final-composite extent");

    desc.finalComposite.outputRect = {};
    Require(EditorRenderPassSubmitter::ResolveUiCompositeViewRect(desc) ==
                RenderViewportRect{.extent = desc.finalComposite.extent},
            "Shared UI composite view did not fall back to the final-composite extent");
}

} // namespace

void RunUserWidgetDrawBatchTests() {
    AdjacentCompatibleDrawsBatchWithoutChangingPainterOrder();
    NineSliceUsesOneTextureBatchAndNineQuads();
    TransformClipAndBlurRemainInCanonicalDrawData();
    RuntimeControlStateProducesDefaultPrimitives();
    CorruptAndUnsupportedFontPayloadsAreRejectedBeforeRasterization();
    SharedUiCompositeViewPreservesLetterboxedOutputRect();
}

} // namespace kb::render::tests
