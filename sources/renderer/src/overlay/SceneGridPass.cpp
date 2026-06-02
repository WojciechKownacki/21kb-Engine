#include "kb/render/overlay/SceneGridPass.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/ShaderLoader.hpp"

#include <algorithm>
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

void AddLine(std::array<LineVertex, 164U>& vertices, std::uint32_t& count, float x0, float z0, float x1, float z1, std::array<float, 3> color) noexcept {
    if (count + 2U > vertices.size()) {
        return;
    }
    vertices[count++] = LineVertex{x0, 0.0F, z0, color[0], color[1], color[2]};
    vertices[count++] = LineVertex{x1, 0.0F, z1, color[0], color[1], color[2]};
}

[[nodiscard]] std::uint32_t BuildGrid(std::array<LineVertex, 164U>& vertices) noexcept {
    constexpr int kLineHalfCount = 20;
    constexpr float kSpacing = 1.0F;
    constexpr float kExtent = static_cast<float>(kLineHalfCount) * kSpacing;
    constexpr std::array<float, 3> kMinor{0.20F, 0.23F, 0.27F};
    constexpr std::array<float, 3> kMajor{0.34F, 0.38F, 0.44F};
    constexpr std::array<float, 3> kAxisX{0.84F, 0.22F, 0.20F};
    constexpr std::array<float, 3> kAxisZ{0.22F, 0.46F, 0.88F};

    std::uint32_t count = 0U;
    for (int line = -kLineHalfCount; line <= kLineHalfCount; ++line) {
        const float position = static_cast<float>(line) * kSpacing;
        const bool axis = line == 0;
        const bool major = line % 5 == 0;
        AddLine(vertices, count, -kExtent, position, kExtent, position, axis ? kAxisX : (major ? kMajor : kMinor));
        AddLine(vertices, count, position, -kExtent, position, kExtent, axis ? kAxisZ : (major ? kMajor : kMinor));
    }
    return count;
}

void ConfigureOverlayView(const SceneGridPassDesc& desc) {
    bgfx::setViewName(desc.viewId, "KB Editor Grid");
    bgfx::setViewFrameBuffer(desc.viewId, desc.frameBuffer);
    bgfx::setViewTransform(desc.viewId, desc.camera->view.data(), desc.camera->projection.data());
    bgfx::setViewRect(desc.viewId, 0, 0, ClampToViewExtent(desc.extent.width), ClampToViewExtent(desc.extent.height));
    bgfx::setViewClear(desc.viewId, BGFX_CLEAR_NONE);
    bgfx::setViewMode(desc.viewId, bgfx::ViewMode::Sequential);
    bgfx::touch(desc.viewId);
}

} // namespace

SceneGridPass::~SceneGridPass() {
    Shutdown();
}

bool SceneGridPassDesc::IsValid() const noexcept {
    return extent.IsValid() && camera != nullptr;
}

bool SceneGridPass::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    program_ = ShaderLoader::LoadProgram("vs_editor_grid.sc", "fs_editor_grid.sc");
    lineLayout_ = LineLayout();
    initialized_ = true;
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void SceneGridPass::Shutdown() noexcept {
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

bool SceneGridPass::Submit(const SceneGridPassDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return false;
    }

    std::array<LineVertex, 164U> vertices{};
    const std::uint32_t vertexCount = BuildGrid(vertices);
    if (vertexCount == 0U || bgfx::getAvailTransientVertexBuffer(vertexCount, lineLayout_) < vertexCount) {
        return false;
    }

    ConfigureOverlayView(desc);
    bgfx::TransientVertexBuffer buffer{};
    bgfx::allocTransientVertexBuffer(&buffer, vertexCount, lineLayout_);
    std::memcpy(buffer.data, vertices.data(), sizeof(LineVertex) * vertexCount);

    bgfx::setState(SceneDepthPolicy::SceneOverlayState(true) | BGFX_STATE_PT_LINES);
    bgfx::setVertexBuffer(0, &buffer);
    bgfx::submit(desc.viewId, program_);
    return true;
}

bool SceneGridPass::IsInitialized() const noexcept {
    return initialized_ && bgfx::isValid(program_) && lineLayout_.getStride() == sizeof(LineVertex);
}

} // namespace kb::render
