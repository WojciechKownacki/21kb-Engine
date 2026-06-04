#pragma once

#include "kb/render/scene/RenderScene.hpp"

namespace kb::render {

class RenderSceneProxyDirtyTracker {
public:
    [[nodiscard]] static RenderProxyDirtyFlag DirtyForMeshChange(const MeshRenderProxyDesc& current, const MeshRenderProxyDesc& next) noexcept;
    [[nodiscard]] static RenderProxyDirtyFlag DirtyForCameraChange(const CameraRenderProxyDesc& current, const CameraRenderProxyDesc& next) noexcept;
    [[nodiscard]] static RenderProxyDirtyFlag DirtyForLightChange(const LightRenderProxyDesc& current, const LightRenderProxyDesc& next) noexcept;
};

} // namespace kb::render
