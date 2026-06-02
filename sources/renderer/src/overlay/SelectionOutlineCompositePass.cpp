#include "kb/render/overlay/SelectionOutlineCompositePass.hpp"

#include "kb/render/ShaderLoader.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace kb::render {
namespace {

struct PosTexVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
};

[[nodiscard]] bgfx::VertexLayout FullscreenLayout() noexcept {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
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

} // namespace

SelectionOutlineCompositePass::~SelectionOutlineCompositePass() {
    Shutdown();
}

bool SelectionOutlineCompositePassDesc::IsValid() const noexcept {
    return bgfx::isValid(selectionMask) && extent.IsValid();
}

bool SelectionOutlineCompositePass::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    program_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_editor_selection_outline.sc");
    selectionMaskSampler_ = bgfx::createUniform("s_selectionMask", bgfx::UniformType::Sampler);
    outlineParams_ = bgfx::createUniform("u_outlineParams", bgfx::UniformType::Vec4);
    fullscreenLayout_ = FullscreenLayout();
    initialized_ = true;
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void SelectionOutlineCompositePass::Shutdown() noexcept {
    if (!initialized_) {
        return;
    }
    if (bgfx::isValid(outlineParams_)) {
        bgfx::destroy(outlineParams_);
        outlineParams_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(selectionMaskSampler_)) {
        bgfx::destroy(selectionMaskSampler_);
        selectionMaskSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    fullscreenLayout_ = {};
    initialized_ = false;
}

bool SelectionOutlineCompositePass::Submit(const SelectionOutlineCompositePassDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return false;
    }

    constexpr std::array<PosTexVertex, 3U> triangle{
        PosTexVertex{-1.0F, 1.0F, 0.0F, 0.0F, 0.0F},
        PosTexVertex{3.0F, 1.0F, 0.0F, 2.0F, 0.0F},
        PosTexVertex{-1.0F, -3.0F, 0.0F, 0.0F, 2.0F},
    };
    constexpr std::uint32_t vertexCount = static_cast<std::uint32_t>(triangle.size());
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, fullscreenLayout_) < vertexCount) {
        return false;
    }

    const std::array<float, 16> identity = IdentityMatrix();
    bgfx::setViewName(desc.viewId, "KB Editor Selection Outline");
    bgfx::setViewFrameBuffer(desc.viewId, desc.frameBuffer);
    bgfx::setViewRect(desc.viewId, 0, 0, ClampToViewExtent(desc.extent.width), ClampToViewExtent(desc.extent.height));
    bgfx::setViewTransform(desc.viewId, identity.data(), identity.data());
    bgfx::setViewClear(desc.viewId, BGFX_CLEAR_NONE);
    bgfx::setViewMode(desc.viewId, bgfx::ViewMode::Sequential);
    bgfx::touch(desc.viewId);

    bgfx::TransientVertexBuffer vertices{};
    bgfx::allocTransientVertexBuffer(&vertices, vertexCount, fullscreenLayout_);
    std::memcpy(vertices.data, triangle.data(), sizeof(PosTexVertex) * triangle.size());

    const float width = static_cast<float>(std::max(1U, desc.extent.width));
    const float height = static_cast<float>(std::max(1U, desc.extent.height));
    const float outlineParams[4] = {1.0F / width, 1.0F / height, 1.5F, 0.0F};
    bgfx::setUniform(outlineParams_, outlineParams);
    bgfx::setTexture(0, selectionMaskSampler_, desc.selectionMask);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::submit(desc.viewId, program_);
    return true;
}

bool SelectionOutlineCompositePass::IsInitialized() const noexcept {
    return initialized_ && bgfx::isValid(program_) && bgfx::isValid(selectionMaskSampler_) &&
           bgfx::isValid(outlineParams_) && fullscreenLayout_.getStride() == sizeof(PosTexVertex);
}

} // namespace kb::render
