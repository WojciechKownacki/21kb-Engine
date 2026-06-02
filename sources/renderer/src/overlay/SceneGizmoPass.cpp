#include "kb/render/overlay/SceneGizmoPass.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/ShaderLoader.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace kb::render {
namespace {

struct LineVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

[[nodiscard]] bgfx::VertexLayout LineLayout() noexcept {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 3, bgfx::AttribType::Float)
        .end();
    return layout;
}

void ConfigureOverlayView(const SceneGizmoPassDesc& desc) {
    bgfx::setViewName(desc.viewId, "KB Editor Gizmo");
    bgfx::setViewFrameBuffer(desc.viewId, desc.frameBuffer);
    bgfx::setViewTransform(desc.viewId, desc.camera->view.data(), desc.camera->projection.data());
    bgfx::setViewRect(desc.viewId, 0, 0, ClampToViewExtent(desc.extent.width), ClampToViewExtent(desc.extent.height));
    bgfx::setViewClear(desc.viewId, BGFX_CLEAR_NONE);
    bgfx::setViewMode(desc.viewId, bgfx::ViewMode::Sequential);
    bgfx::touch(desc.viewId);
}

} // namespace

SceneGizmoPass::~SceneGizmoPass() {
    Shutdown();
}

bool SceneGizmoPassDesc::IsValid() const noexcept {
    return extent.IsValid() && camera != nullptr;
}

bool SceneGizmoPass::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    program_ = ShaderLoader::LoadProgram("vs_editor_grid.sc", "fs_editor_gizmo.sc");
    lineLayout_ = LineLayout();
    initialized_ = true;
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void SceneGizmoPass::Shutdown() noexcept {
    if (!initialized_) {
        return;
    }
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    lineLayout_ = {};
    initialized_ = false;
}

bool SceneGizmoPass::Submit(const SceneGizmoPassDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return false;
    }

    constexpr std::array<LineVertex, 6U> vertices{
        LineVertex{0.0F, 0.02F, 0.0F, 0.92F, 0.18F, 0.16F},
        LineVertex{1.5F, 0.02F, 0.0F, 0.92F, 0.18F, 0.16F},
        LineVertex{0.0F, 0.02F, 0.0F, 0.26F, 0.78F, 0.28F},
        LineVertex{0.0F, 1.52F, 0.0F, 0.26F, 0.78F, 0.28F},
        LineVertex{0.0F, 0.02F, 0.0F, 0.22F, 0.48F, 0.95F},
        LineVertex{0.0F, 0.02F, 1.5F, 0.22F, 0.48F, 0.95F},
    };
    constexpr std::uint32_t vertexCount = static_cast<std::uint32_t>(vertices.size());
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, lineLayout_) < vertexCount) {
        return false;
    }

    ConfigureOverlayView(desc);
    bgfx::TransientVertexBuffer buffer{};
    bgfx::allocTransientVertexBuffer(&buffer, vertexCount, lineLayout_);
    std::memcpy(buffer.data, vertices.data(), sizeof(LineVertex) * vertices.size());

    bgfx::setState(SceneDepthPolicy::SceneOverlayState(false) | BGFX_STATE_PT_LINES);
    bgfx::setVertexBuffer(0, &buffer);
    bgfx::submit(desc.viewId, program_);
    return true;
}

bool SceneGizmoPass::IsInitialized() const noexcept {
    return initialized_ && bgfx::isValid(program_) && lineLayout_.getStride() == sizeof(LineVertex);
}

} // namespace kb::render
