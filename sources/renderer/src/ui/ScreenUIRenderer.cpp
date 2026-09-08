#include "private/ui/ScreenUIRenderer.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneUI.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <sstream>

namespace kb::render {
namespace {

template <typename Callback>
void ForEachImageAsset(const kb::scene::SceneUIFrameElement& element, Callback&& callback) {
    if (element.sprite.has_value() && element.sprite->spriteAssetId != 0U) {
        callback(element.sprite->spriteAssetId, RenderTextureColorSpace::Srgb, ScreenUITextureSource::ImageAssetSrgb);
    }
    if (element.image.has_value() && element.image->imageAssetId != 0U) {
        callback(element.image->imageAssetId, RenderTextureColorSpace::Srgb, ScreenUITextureSource::ImageAssetSrgb);
    }
    if (element.rawImage.has_value() && element.rawImage->imageAssetId != 0U) {
        callback(element.rawImage->imageAssetId, RenderTextureColorSpace::Linear, ScreenUITextureSource::ImageAssetLinear);
    }
}

[[nodiscard]] bool ValidPresentationExtent(const kb::scene::SceneUIFrame& frame) noexcept {
    return std::isfinite(frame.viewportSize.x) && std::isfinite(frame.viewportSize.y) &&
           frame.viewportSize.x > 0.0F && frame.viewportSize.y > 0.0F &&
           frame.viewportSize.x <= static_cast<float>(std::numeric_limits<std::uint32_t>::max()) &&
           frame.viewportSize.y <= static_cast<float>(std::numeric_limits<std::uint32_t>::max());
}

} // namespace

bool ScreenUISubmitDesc::IsValid() const noexcept {
    return sceneId != 0U && frame != nullptr && assets != nullptr && resources != nullptr &&
           resourceMap != nullptr && viewportPlan != nullptr && outputExtent.IsValid() &&
           ValidPresentationExtent(*frame);
}

bool ScreenUIRenderer::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    if (!composite_.Initialize() || !backgroundBlur_.Initialize()) {
        composite_.Shutdown();
        backgroundBlur_.Shutdown();
        return false;
    }
    return true;
}

void ScreenUIRenderer::Shutdown(RenderResourceRegistry& resources) noexcept {
    fontAtlas_.Shutdown(resources);
    backgroundBlur_.Shutdown();
    composite_.Shutdown();
    imageBindings_.clear();
    resolvedTextures_.clear();
}

void ScreenUIRenderer::MarkTextureReferences(std::uint64_t sceneId, const kb::scene::SceneUIFrame& frame,
                                             RuntimeFrameResourceReferences& references) {
    for (const kb::scene::SceneUIFrameElement& element : frame.elements) {
        ForEachImageAsset(element, [&](std::uint64_t assetId, RenderTextureColorSpace colorSpace,
                                       ScreenUITextureSource source) {
            static_cast<void>(source);
            references.MarkTexture(RuntimeTextureAssetKey{.sceneId = sceneId,
                                                           .assetId = assetId,
                                                           .colorSpace = colorSpace});
        });
    }
}

bool ScreenUIRenderer::Submit(const ScreenUISubmitDesc& desc) {
    if (!IsInitialized() || !desc.IsValid()) {
        WriteRendererDebugLog("screen_ui", "Submit rejected invalid renderer state or descriptor");
        return false;
    }
    if (desc.frame->elements.empty()) {
        return true;
    }
    resolvedTextures_.clear();
    imageBindings_.clear();
    lastTextFailure_ = {};
    ResolveImages(desc);
    const ScreenUIFontPreparation fonts = fontAtlas_.Prepare(desc.sceneId, *desc.assets, *desc.resources, *desc.frame);
    if (fonts.failureReason != nullptr) {
        lastTextFailure_ = TextFailure{.entityId = fonts.failedEntity, .reason = fonts.failureReason};
    }
    // Text that cannot be prepared is dropped element by element, not frame by frame: a font
    // still streaming in, or one bad markup string, used to erase every rectangle, image and
    // border in the scene's UI.
    ResolveFonts(desc, fonts.runs);
    if (lastTextFailure_.reason != nullptr) {
        WriteRendererDebugLog("screen_ui", "Submit dropped unpreparable text and kept the rest of the UI");
    }
    const ScreenUIDrawList& drawList = batchBuilder_.Build(*desc.frame, imageBindings_, drawableRuns_);
    if (drawList.requiresBackgroundBlur) {
        if (!bgfx::isValid(desc.backgroundSource) ||
            !backgroundBlur_.Submit(desc.viewportIndex, desc.viewportPlan->viewport.extent, desc.backgroundFormat,
                                    desc.backgroundSource, desc.viewportPlan->viewIds.screenUIBlurH,
                                    desc.viewportPlan->viewIds.screenUIBlurV, drawList.maximumBackgroundBlur)) {
            WriteRendererDebugLog("screen_ui", "Submit failed to render requested background blur");
            return false;
        }
        AppendResolvedTexture(ScreenUIResolvedTexture{
            .key = ScreenUITextureKey{.source = ScreenUITextureSource::BackgroundBlur},
            .texture = backgroundBlur_.Output(desc.viewportIndex),
            .width = static_cast<std::uint16_t>(desc.viewportPlan->viewport.extent.width),
            .height = static_cast<std::uint16_t>(desc.viewportPlan->viewport.extent.height),
        });
    }
    return composite_.Submit(ScreenUICompositeDesc{
        .viewId = desc.viewportPlan->viewIds.screenUIComposite,
        .frameBuffer = desc.outputFrameBuffer,
        .presentationExtent = RenderExtent{static_cast<std::uint32_t>(desc.frame->viewportSize.x),
                                           static_cast<std::uint32_t>(desc.frame->viewportSize.y)},
        .outputExtent = desc.outputExtent,
        .outputRect = desc.outputRect,
        .outputTransform = desc.outputTransform,
        .drawList = &drawList,
        .textures = resolvedTextures_,
    });
}

void ScreenUIRenderer::ReleaseScene(std::uint64_t sceneId, RenderResourceRegistry& resources) noexcept {
    fontAtlas_.ReleaseScene(sceneId, resources);
}

void ScreenUIRenderer::ReleaseAllScenes(RenderResourceRegistry& resources) noexcept {
    fontAtlas_.Shutdown(resources);
    imageBindings_.clear();
    resolvedTextures_.clear();
}

void ScreenUIRenderer::OnResize() noexcept {
    backgroundBlur_.InvalidateTargets();
}

bool ScreenUIRenderer::IsInitialized() const noexcept {
    return composite_.IsInitialized() && backgroundBlur_.IsInitialized();
}

void ScreenUIRenderer::ResolveImages(const ScreenUISubmitDesc& desc) {
    for (const kb::scene::SceneUIFrameElement& element : desc.frame->elements) {
        ForEachImageAsset(element, [&](std::uint64_t assetId, RenderTextureColorSpace colorSpace,
                                       ScreenUITextureSource source) {
            if (std::ranges::find_if(imageBindings_, [assetId, source](const ScreenUIImageBinding& image) {
                    return image.assetId == assetId && image.source == source;
                }) != imageBindings_.end()) {
                return;
            }
            const RenderTextureHandle handle = desc.resourceMap->ResolveTexture(assetId, colorSpace);
            const RenderTextureResource* texture = desc.resources->FindTexture(handle);
            if (texture == nullptr || !bgfx::isValid(texture->texture)) {
                std::ostringstream message;
                message << "Image asset pending or unavailable " << assetId;
                WriteRendererDebugLog("screen_ui", message.str());
                return;
            }
            imageBindings_.push_back(ScreenUIImageBinding{.assetId = assetId,
                                                          .source = source,
                                                          .width = texture->width,
                                                          .height = texture->height});
            AppendResolvedTexture(ScreenUIResolvedTexture{
                .key = ScreenUITextureKey{.source = source, .assetId = assetId},
                .texture = texture->texture,
                .width = texture->width,
                .height = texture->height,
            });
        });
    }
}

void ScreenUIRenderer::ResolveFonts(const ScreenUISubmitDesc& desc, std::span<const ScreenUITextRun> textRuns) {
    drawableRuns_.clear();
    drawableRuns_.reserve(textRuns.size());
    for (const ScreenUITextRun& run : textRuns) {
        const ScreenUITextureKey key{.source = ScreenUITextureSource::FontAtlas,
                                     .assetId = run.fontAssetId,
                                     .pixelSize = run.pixelSize};
        if (std::ranges::find(resolvedTextures_, key, &ScreenUIResolvedTexture::key) != resolvedTextures_.end()) {
            drawableRuns_.push_back(run);
            continue;
        }
        const bgfx::TextureHandle texture =
            fontAtlas_.Resolve(desc.sceneId, run.fontAssetId, run.pixelSize, *desc.resources);
        if (!bgfx::isValid(texture)) {
            if (lastTextFailure_.reason == nullptr) {
                lastTextFailure_ = TextFailure{.entityId = run.entity, .reason = "Font atlas texture is unavailable"};
            }
            continue;
        }
        AppendResolvedTexture(ScreenUIResolvedTexture{.key = key,
                                                       .texture = texture,
                                                       .width = run.atlasWidth,
                                                       .height = run.atlasHeight});
        drawableRuns_.push_back(run);
    }
}

const ScreenUIRenderer::TextFailure& ScreenUIRenderer::LastTextFailure() const noexcept {
    return lastTextFailure_;
}

void ScreenUIRenderer::AppendResolvedTexture(ScreenUIResolvedTexture texture) {
    if (std::ranges::find(resolvedTextures_, texture.key, &ScreenUIResolvedTexture::key) == resolvedTextures_.end()) {
        resolvedTextures_.push_back(texture);
    }
}

} // namespace kb::render
