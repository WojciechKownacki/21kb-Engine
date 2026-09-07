#pragma once

#include "private/ui/UserWidgetBackgroundBlur.hpp"
#include "private/ui/UserWidgetComposite.hpp"
#include "private/ui/UserWidgetDrawBatchBuilder.hpp"
#include "private/ui/UserWidgetFontAtlasCache.hpp"

#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/runtime/RuntimeFrameResourceReferences.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <span>
#include <vector>

namespace kb::assets {
class AssetManager;
}

namespace kb::render {

class RenderResourceRegistry;
class SceneRenderResourceMap;

struct RuntimeUserWidgetSubmitDesc {
    std::uint64_t sceneId = 0U;
    std::uint32_t viewportIndex = 0U;
    const kb::scene::UIPresentationSnapshot* presentation = nullptr;
    kb::assets::AssetManager* assets = nullptr;
    RenderResourceRegistry* resources = nullptr;
    const SceneRenderResourceMap* resourceMap = nullptr;
    const RenderViewportPlan* viewportPlan = nullptr;
    bgfx::TextureHandle backgroundSource = BGFX_INVALID_HANDLE;
    bgfx::TextureFormat::Enum backgroundFormat = bgfx::TextureFormat::Count;
    bgfx::FrameBufferHandle outputFrameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent outputExtent{};
    RenderViewportRect outputRect{};
    FullscreenTextureOutputTransform outputTransform{};

    [[nodiscard]] bool IsValid() const noexcept;
};

class RuntimeUserWidgetRenderer {
  public:
    RuntimeUserWidgetRenderer() = default;
    ~RuntimeUserWidgetRenderer() = default;

    RuntimeUserWidgetRenderer(const RuntimeUserWidgetRenderer&) = delete;
    RuntimeUserWidgetRenderer& operator=(const RuntimeUserWidgetRenderer&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown(RenderResourceRegistry& resources) noexcept;
    static void MarkTextureReferences(std::uint64_t sceneId, const kb::scene::UIPresentationSnapshot& presentation,
                                      RuntimeFrameResourceReferences& references);
    [[nodiscard]] bool Submit(const RuntimeUserWidgetSubmitDesc& desc);
    void ReleaseScene(std::uint64_t sceneId, RenderResourceRegistry& resources) noexcept;
    void ReleaseAllScenes(RenderResourceRegistry& resources) noexcept;
    void OnResize() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

  private:
    void ResolveImages(const RuntimeUserWidgetSubmitDesc& desc);
    [[nodiscard]] bool ResolveFonts(const RuntimeUserWidgetSubmitDesc& desc,
                                    std::span<const UserWidgetTextRun> textRuns);
    void AppendResolvedTexture(UserWidgetResolvedTexture texture);

    UserWidgetDrawBatchBuilder batchBuilder_;
    UserWidgetFontAtlasCache fontAtlas_;
    UserWidgetComposite composite_;
    UserWidgetBackgroundBlur backgroundBlur_;
    std::vector<UserWidgetImageBinding> imageBindings_;
    std::vector<UserWidgetResolvedTexture> resolvedTextures_;
};

} // namespace kb::render
