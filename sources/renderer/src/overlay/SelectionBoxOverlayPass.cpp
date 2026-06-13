#include "kb/render/overlay/SelectionBoxOverlayPass.hpp"

#include "kb/render/ShaderLoader.hpp"

#include <array>
#include <algorithm>
#include <cstring>

namespace kb::render {
namespace {

struct Vertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 0.0F;
    float nz = 1.0F;
    float r = 0.67F;
    float g = 0.70F;
    float b = 0.74F;
    float alpha = 0.48F;
    float unused = 0.0F;
};

[[nodiscard]] bgfx::VertexLayout Layout() noexcept {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return layout;
}

[[nodiscard]] std::array<float, 16> IdentityMatrix() noexcept {
    return std::array<float, 16>{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

[[nodiscard]] float NdcX(float x, float width) noexcept {
    return (x / std::max(width, 1.0F)) * 2.0F - 1.0F;
}

[[nodiscard]] float NdcY(float y, float height) noexcept {
    return 1.0F - (y / std::max(height, 1.0F)) * 2.0F;
}

} // namespace

SelectionBoxOverlayPass::~SelectionBoxOverlayPass() {
    Shutdown();
}

bool SelectionBoxOverlayPassDesc::IsValid() const noexcept {
    return visible && extent.IsValid() && width > 0.0F && height > 0.0F &&
        (!outputRect.extent.IsValid() || outputRect.IsValid());
}

bool SelectionBoxOverlayPass::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    program_ = ShaderLoader::LoadProgram("vs_editor_gizmo.sc", "fs_editor_gizmo.sc");
    layout_ = Layout();
    initialized_ = true;
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void SelectionBoxOverlayPass::Shutdown() noexcept {
    if (!initialized_) {
        return;
    }
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    layout_ = {};
    initialized_ = false;
}

bool SelectionBoxOverlayPass::Submit(const SelectionBoxOverlayPassDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return false;
    }

    const RenderViewportRect outputRect = desc.outputRect.extent.IsValid()
        ? desc.outputRect
        : RenderViewportRect{.extent = desc.extent};
    const std::array<float, 16> identity = IdentityMatrix();
    bgfx::setViewName(desc.viewId, "KB Editor Selection Box");
    bgfx::setViewFrameBuffer(desc.viewId, desc.frameBuffer);
    bgfx::setViewTransform(desc.viewId, identity.data(), identity.data());
    bgfx::setViewRect(
        desc.viewId,
        ClampToViewExtent(outputRect.x),
        ClampToViewExtent(outputRect.y),
        ClampToViewExtent(outputRect.extent.width),
        ClampToViewExtent(outputRect.extent.height));
    bgfx::setViewClear(desc.viewId, BGFX_CLEAR_NONE);
    bgfx::setViewMode(desc.viewId, bgfx::ViewMode::Sequential);
    bgfx::touch(desc.viewId);

    constexpr std::uint32_t vertexCount = 4U;
    constexpr std::uint32_t indexCount = 6U;
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, layout_) < vertexCount ||
        bgfx::getAvailTransientIndexBuffer(indexCount) < indexCount) {
        return false;
    }

    const float viewWidth = static_cast<float>(std::max(1U, outputRect.extent.width));
    const float viewHeight = static_cast<float>(std::max(1U, outputRect.extent.height));
    const float left = std::clamp(desc.x, 0.0F, viewWidth);
    const float top = std::clamp(desc.y, 0.0F, viewHeight);
    const float right = std::clamp(desc.x + desc.width, 0.0F, viewWidth);
    const float bottom = std::clamp(desc.y + desc.height, 0.0F, viewHeight);
    const std::array<Vertex, vertexCount> vertices{{
        Vertex{.x = NdcX(left, viewWidth), .y = NdcY(top, viewHeight)},
        Vertex{.x = NdcX(right, viewWidth), .y = NdcY(top, viewHeight)},
        Vertex{.x = NdcX(right, viewWidth), .y = NdcY(bottom, viewHeight)},
        Vertex{.x = NdcX(left, viewWidth), .y = NdcY(bottom, viewHeight)},
    }};
    constexpr std::array<std::uint16_t, indexCount> indices{{0U, 1U, 2U, 0U, 2U, 3U}};

    bgfx::TransientVertexBuffer vertexBuffer{};
    bgfx::TransientIndexBuffer indexBuffer{};
    bgfx::allocTransientVertexBuffer(&vertexBuffer, vertexCount, layout_);
    bgfx::allocTransientIndexBuffer(&indexBuffer, indexCount);
    std::memcpy(vertexBuffer.data, vertices.data(), sizeof(Vertex) * vertices.size());
    std::memcpy(indexBuffer.data, indices.data(), sizeof(std::uint16_t) * indices.size());

    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    bgfx::setVertexBuffer(0, &vertexBuffer);
    bgfx::setIndexBuffer(&indexBuffer);
    bgfx::submit(desc.viewId, program_);
    return true;
}

bool SelectionBoxOverlayPass::IsInitialized() const noexcept {
    return initialized_ && bgfx::isValid(program_) && layout_.getStride() == sizeof(Vertex);
}

} // namespace kb::render
