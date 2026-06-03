#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"
#include "kb/render/post/SceneDisplayCompositeRenderer.hpp"
#include "kb/render/post/ScenePostProcessRenderer.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace kb::render {

enum class PostProcessPassKind : std::uint8_t {
    IdentityCopy,
    Tonemap,
    Bloom,
    SelectionOutline,
};

enum class PostProcessColorSpace : std::uint8_t {
    SceneHdr,
    DisplayLdr,
};

[[nodiscard]] constexpr const char* PostProcessPassKindName(PostProcessPassKind kind) noexcept {
    switch (kind) {
    case PostProcessPassKind::IdentityCopy:
        return "IdentityCopy";
    case PostProcessPassKind::Tonemap:
        return "Tonemap";
    case PostProcessPassKind::Bloom:
        return "Bloom";
    case PostProcessPassKind::SelectionOutline:
        return "SelectionOutline";
    }

    return "Unknown";
}

struct PostProcessPass {
    PostProcessPassKind kind = PostProcessPassKind::IdentityCopy;
    bool enabled = true;
    ScenePostProcessSettings postProcessSettings{};
    SceneDisplayOutputTransform outputTransform{};
};

struct PostProcessChainDesc {
    std::vector<PostProcessPass> passes{};
};

struct PostProcessInput {
    bgfx::TextureHandle sceneColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle selectionMask = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle outputFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle outputColor = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    bgfx::ViewId viewId = 0;

    [[nodiscard]] bool IsValid() const noexcept;
};

struct PostProcessOutput {
    bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    PostProcessPassKind producer = PostProcessPassKind::IdentityCopy;
    PostProcessColorSpace colorSpace = PostProcessColorSpace::SceneHdr;
    ScenePostProcessSettings postProcessSettings{};
    SceneDisplayOutputTransform outputTransform{};
    std::uint32_t enabledPassCount = 0;
    bool passthrough = true;
    bool gpuSubmitted = false;
    bool sceneHdrPreserved = true;
    bool bloomEnabled = false;
    bool selectionOutlineEnabled = false;
    bool tonemapEnabled = false;

    [[nodiscard]] bool IsValid() const noexcept;
};

class PostProcessChain {
public:
    static constexpr PostProcessPass kDefaultIdentityPass{};

    [[nodiscard]] static PostProcessChainDesc DefaultSceneChainDesc();

    void Clear() noexcept;
    [[nodiscard]] bool Configure(const PostProcessChainDesc& desc);
    [[nodiscard]] bool AddPass(PostProcessPass pass);
    [[nodiscard]] bool InsertPass(std::uint32_t index, PostProcessPass pass);
    [[nodiscard]] bool RemovePass(PostProcessPassKind kind) noexcept;
    [[nodiscard]] bool SetPass(PostProcessPass pass);
    [[nodiscard]] bool SetPassEnabled(PostProcessPassKind kind, bool enabled) noexcept;
    [[nodiscard]] std::optional<PostProcessPass> FindPass(PostProcessPassKind kind) const noexcept;
    [[nodiscard]] std::span<const PostProcessPass> Passes() const noexcept;
    [[nodiscard]] bool HasEnabledPasses() const noexcept;
    [[nodiscard]] PostProcessOutput Evaluate(const PostProcessInput& input) const;

private:
    std::vector<PostProcessPass> passes_;
};

} // namespace kb::render
