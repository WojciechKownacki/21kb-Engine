#pragma once

#include "kb/render/scene/RenderScene.hpp"

namespace kb::render {

class RenderSceneCameraBuilder {
public:
    [[nodiscard]] static SceneRenderCamera Build(const CameraRenderProxyDesc& camera, std::uint32_t viewportWidth, std::uint32_t viewportHeight);
};

class RenderSceneMeshInstanceBuilder {
public:
    [[nodiscard]] static SceneRenderMeshInstance Build(const MeshRenderProxyDesc& mesh) noexcept;
};

class RenderSceneLightBuilder {
public:
    [[nodiscard]] static SceneRenderLight Build(const LightRenderProxyDesc& light) noexcept;
};

} // namespace kb::render
