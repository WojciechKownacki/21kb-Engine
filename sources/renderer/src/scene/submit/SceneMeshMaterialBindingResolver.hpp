#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <array>

namespace kb::render {

struct SceneMeshMaterialBinding {
    bgfx::TextureHandle albedoTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normalTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle metallicRoughnessTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle occlusionTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle emissiveTexture = BGFX_INVALID_HANDLE;
    std::array<float, 4> params{};
    std::array<float, 4> emissive{};
    std::array<float, 4> flags{};
};

struct SceneMeshShadowMaterialBinding {
    bgfx::TextureHandle albedoTexture = BGFX_INVALID_HANDLE;
    std::array<float, 4> params{};
    std::array<float, 4> flags{};
};

struct SceneMeshMaterialBindingFallbacks {
    bgfx::TextureHandle whiteTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normalTexture = BGFX_INVALID_HANDLE;
};

class SceneMeshMaterialBindingResolver {
public:
    SceneMeshMaterialBindingResolver() = delete;

    [[nodiscard]] static SceneMeshMaterialBinding Resolve(
        const RenderMaterialResource* material,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        SceneMeshMaterialBindingFallbacks fallbacks) noexcept;
    [[nodiscard]] static SceneMeshShadowMaterialBinding ResolveShadow(
        const RenderMaterialResource* material,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        SceneMeshMaterialBindingFallbacks fallbacks) noexcept;
};

} // namespace kb::render
