#pragma once

#include "kb/render/overlay/EditorCameraWireframe.hpp"
#include "kb/render/overlay/EditorLightWireframe.hpp"
#include "kb/render/overlay/PhysicsDebugLine.hpp"
#include "kb/render/frame/RenderTargetDesc.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

struct SceneGizmoPassDesc {
    bgfx::ViewId viewId = 0;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    RenderViewportRect outputRect{};
    const SceneRenderCamera* camera = nullptr;
    std::array<float, 3> targetPosition{0.0F, 0.0F, 0.0F};
    float worldScale = 1.0F;
    int hoveredAxis = -1;
    int draggedAxis = -1;
    std::uint8_t mode = 0U;
    std::span<const EditorCameraWireframeDesc> cameraWireframes{};
    std::span<const EditorLightWireframeDesc> lightWireframes{};
    std::span<const PhysicsDebugLine> physicsDebugLines{};
    bool visible = false;

    [[nodiscard]] bool IsValid() const noexcept;
};

class SceneGizmoPass {
public:
    SceneGizmoPass() = default;
    ~SceneGizmoPass();

    SceneGizmoPass(const SceneGizmoPass&) = delete;
    SceneGizmoPass& operator=(const SceneGizmoPass&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const SceneGizmoPassDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

    struct GizmoVertex {
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
        float nx = 0.0F;
        float ny = 1.0F;
        float nz = 0.0F;
        float r = 1.0F;
        float g = 1.0F;
        float b = 1.0F;
        float alpha = 1.0F;
        float unused = 0.0F;
    };
    struct MeshRange {
        std::uint32_t indexStart = 0U;
        std::uint32_t indexCount = 0U;
    };

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_{};
    std::vector<GizmoVertex> vertices_{};
    std::vector<std::uint32_t> indices_{};
    MeshRange shaft_{};
    MeshRange tip_{};
    MeshRange scaleTip_{};
    MeshRange rotateRing_{};
    MeshRange hub_{};
    bool initialized_ = false;
};

} // namespace kb::render
