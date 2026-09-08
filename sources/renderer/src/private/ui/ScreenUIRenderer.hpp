#pragma once

#include "private/ui/ScreenUIBackgroundBlur.hpp"
#include "private/ui/ScreenUIComposite.hpp"
#include "private/ui/ScreenUIDrawBatchBuilder.hpp"
#include "private/ui/ScreenUIFontAtlasCache.hpp"

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

namespace kb::scene {
struct SceneUIFrame;
}

namespace kb::render {

class RenderResourceRegistry;
class SceneRenderResourceMap;

struct ScreenUISubmitDesc {
    std::uint64_t sceneId = 0U;
    std::uint32_t viewportIndex = 0U;
    const kb::scene::SceneUIFrame* frame = nullptr;
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

class ScreenUIRenderer {
  public:
    ScreenUIRenderer() = default;
    ~ScreenUIRenderer() = default;

    ScreenUIRenderer(const ScreenUIRenderer&) = delete;
    ScreenUIRenderer& operator=(const ScreenUIRenderer&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown(RenderResourceRegistry& resources) noexcept;
    static void MarkTextureReferences(std::uint64_t sceneId, const kb::scene::SceneUIFrame& frame,
                                      RuntimeFrameResourceReferences& references);
    [[nodiscard]] bool Submit(const ScreenUISubmitDesc& desc);
    void ReleaseScene(std::uint64_t sceneId, RenderResourceRegistry& resources) noexcept;
    void ReleaseAllScenes(RenderResourceRegistry& resources) noexcept;
    void OnResize() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    // The first text element the last Submit could not draw, or a null reason when all of
    // them drew. The caller publishes this: a widget silently losing its label is the kind
    // of fault that otherwise only shows up in a screenshot.
    struct TextFailure {
        std::uint64_t entityId = 0U;
        const char* reason = nullptr;
    };
    [[nodiscard]] const TextFailure& LastTextFailure() const noexcept;

  private:
    void ResolveImages(const ScreenUISubmitDesc& desc);
    // Keeps the runs whose atlas texture resolved, dropping the rest into `lastTextFailure_`.
    void ResolveFonts(const ScreenUISubmitDesc& desc, std::span<const ScreenUITextRun> textRuns);
    void AppendResolvedTexture(ScreenUIResolvedTexture texture);

    ScreenUIDrawBatchBuilder batchBuilder_;
    ScreenUIFontAtlasCache fontAtlas_;
    ScreenUIComposite composite_;
    ScreenUIBackgroundBlur backgroundBlur_;
    std::vector<ScreenUIImageBinding> imageBindings_;
    std::vector<ScreenUIResolvedTexture> resolvedTextures_;
    std::vector<ScreenUITextRun> drawableRuns_;
    TextFailure lastTextFailure_{};
};

} // namespace kb::render
