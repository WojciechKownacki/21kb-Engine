#include "private/ui/RuntimeUserWidgetRenderer.hpp"

#include "engine/assets/AssetManager.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <algorithm>
#include <sstream>

namespace kb::render {

bool RuntimeUserWidgetSubmitDesc::IsValid() const noexcept {
    return sceneId != 0U && presentation != nullptr && assets != nullptr && resources != nullptr &&
           resourceMap != nullptr && viewportPlan != nullptr && outputExtent.IsValid() &&
           presentation->viewportWidth > 0U && presentation->viewportHeight > 0U;
}

bool RuntimeUserWidgetRenderer::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    if (!composite_.Initialize() || !backgroundBlur_.Initialize()) {
        return false;
    }
    return true;
}

void RuntimeUserWidgetRenderer::Shutdown(RenderResourceRegistry& resources) noexcept {
    fontAtlas_.Shutdown(resources);
    backgroundBlur_.Shutdown();
    composite_.Shutdown();
    imageBindings_.clear();
    resolvedTextures_.clear();
}

void RuntimeUserWidgetRenderer::MarkTextureReferences(std::uint64_t sceneId,
                                                      const kb::scene::UIPresentationSnapshot& presentation,
                                                      RuntimeFrameResourceReferences& references) {
    for (const kb::scene::UIPresentationItem& item : presentation.items) {
        if (item.image.has_value() && item.image->imageAssetId != 0U) {
            references.MarkTexture(RuntimeTextureAssetKey{
                .sceneId = sceneId,
                .assetId = item.image->imageAssetId,
                .colorSpace = RenderTextureColorSpace::Srgb,
            });
        }
    }
}

bool RuntimeUserWidgetRenderer::Submit(const RuntimeUserWidgetSubmitDesc& desc) {
    if (!IsInitialized() || !desc.IsValid()) {
        WriteRendererDebugLog("runtime_ui", "Submit rejected invalid renderer state or descriptor");
        return false;
    }
    if (desc.presentation->items.empty()) {
        return true;
    }
    resolvedTextures_.clear();
    imageBindings_.clear();
    ResolveImages(desc);
    const std::span<const UserWidgetTextRun> textRuns =
        fontAtlas_.Prepare(desc.sceneId, *desc.assets, *desc.resources, *desc.presentation);
    const std::size_t expectedTextRuns = static_cast<std::size_t>(
        std::ranges::count_if(desc.presentation->items, [](const kb::scene::UIPresentationItem& item) {
            return item.textStyle.has_value() && item.textStyle->fontAssetId != 0U && !item.text.empty();
        }));
    if (textRuns.size() != expectedTextRuns || !ResolveFonts(desc, textRuns)) {
        WriteRendererDebugLog("runtime_ui", "Submit failed to prepare an authored font asset");
        return false;
    }
    const UserWidgetDrawList& drawList = batchBuilder_.Build(*desc.presentation, imageBindings_, textRuns);
    if (drawList.requiresBackgroundBlur) {
        if (!bgfx::isValid(desc.backgroundSource) ||
            !backgroundBlur_.Submit(desc.viewportIndex, desc.viewportPlan->viewport.extent, desc.backgroundFormat,
                                    desc.backgroundSource, desc.viewportPlan->viewIds.runtimeUiBlurH,
                                    desc.viewportPlan->viewIds.runtimeUiBlurV, drawList.maximumBackgroundBlur)) {
            WriteRendererDebugLog("runtime_ui", "Submit failed to render requested background blur");
            return false;
        }
        AppendResolvedTexture(UserWidgetResolvedTexture{
            .key = UserWidgetTextureKey{.source = UserWidgetTextureSource::BackgroundBlur},
            .texture = backgroundBlur_.Output(desc.viewportIndex),
            .width = static_cast<std::uint16_t>(desc.viewportPlan->viewport.extent.width),
            .height = static_cast<std::uint16_t>(desc.viewportPlan->viewport.extent.height),
        });
    }
    return composite_.Submit(UserWidgetCompositeDesc{
        .viewId = desc.viewportPlan->viewIds.editorUiComposite,
        .frameBuffer = desc.outputFrameBuffer,
        .presentationExtent = RenderExtent{desc.presentation->viewportWidth, desc.presentation->viewportHeight},
        .outputExtent = desc.outputExtent,
        .outputRect = desc.outputRect,
        .outputTransform = desc.outputTransform,
        .drawList = &drawList,
        .textures = resolvedTextures_,
    });
}

void RuntimeUserWidgetRenderer::ReleaseScene(std::uint64_t sceneId, RenderResourceRegistry& resources) noexcept {
    fontAtlas_.ReleaseScene(sceneId, resources);
}

void RuntimeUserWidgetRenderer::ReleaseAllScenes(RenderResourceRegistry& resources) noexcept {
    fontAtlas_.Shutdown(resources);
    imageBindings_.clear();
    resolvedTextures_.clear();
}

void RuntimeUserWidgetRenderer::OnResize() noexcept {
    backgroundBlur_.InvalidateTargets();
}

bool RuntimeUserWidgetRenderer::IsInitialized() const noexcept {
    return composite_.IsInitialized() && backgroundBlur_.IsInitialized();
}

void RuntimeUserWidgetRenderer::ResolveImages(const RuntimeUserWidgetSubmitDesc& desc) {
    for (const kb::scene::UIPresentationItem& item : desc.presentation->items) {
        if (!item.image.has_value() || item.image->imageAssetId == 0U) {
            continue;
        }
        const std::uint64_t assetId = item.image->imageAssetId;
        if (std::ranges::find(imageBindings_, assetId, &UserWidgetImageBinding::assetId) != imageBindings_.end()) {
            continue;
        }
        const RenderTextureHandle handle = desc.resourceMap->ResolveTexture(assetId, RenderTextureColorSpace::Srgb);
        const RenderTextureResource* texture = desc.resources->FindTexture(handle);
        if (texture == nullptr || !bgfx::isValid(texture->texture)) {
            std::ostringstream message;
            message << "Image asset pending or unavailable " << assetId;
            WriteRendererDebugLog("runtime_ui", message.str());
            continue;
        }
        imageBindings_.push_back(UserWidgetImageBinding{
            .assetId = assetId,
            .width = texture->width,
            .height = texture->height,
        });
        AppendResolvedTexture(UserWidgetResolvedTexture{
            .key =
                UserWidgetTextureKey{
                    .source = UserWidgetTextureSource::ImageAsset,
                    .assetId = assetId,
                },
            .texture = texture->texture,
            .width = texture->width,
            .height = texture->height,
        });
    }
}

bool RuntimeUserWidgetRenderer::ResolveFonts(const RuntimeUserWidgetSubmitDesc& desc,
                                             std::span<const UserWidgetTextRun> textRuns) {
    for (const UserWidgetTextRun& run : textRuns) {
        const UserWidgetTextureKey key{
            .source = UserWidgetTextureSource::FontAtlas,
            .assetId = run.fontAssetId,
            .pixelSize = run.pixelSize,
        };
        const auto existing = std::ranges::find(resolvedTextures_, key, &UserWidgetResolvedTexture::key);
        if (existing != resolvedTextures_.end()) {
            continue;
        }
        const bgfx::TextureHandle texture =
            fontAtlas_.Resolve(desc.sceneId, run.fontAssetId, run.pixelSize, *desc.resources);
        if (!bgfx::isValid(texture)) {
            return false;
        }
        AppendResolvedTexture(UserWidgetResolvedTexture{
            .key = key,
            .texture = texture,
            .width = run.atlasWidth,
            .height = run.atlasHeight,
        });
    }
    return true;
}

void RuntimeUserWidgetRenderer::AppendResolvedTexture(UserWidgetResolvedTexture texture) {
    if (std::ranges::find(resolvedTextures_, texture.key, &UserWidgetResolvedTexture::key) == resolvedTextures_.end()) {
        resolvedTextures_.push_back(texture);
    }
}

} // namespace kb::render
