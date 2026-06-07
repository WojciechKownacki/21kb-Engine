#include "kb/render/overlay/SceneGridPass.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/ShaderLoader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

struct GridCamera {
    float x = 0.0F;
    float z = 0.0F;
};

constexpr int kLineHalfCount = 96;
constexpr float kSpacing = 1.0F;
constexpr float kFadeStart = 52.0F;
constexpr float kFadeEnd = 96.0F;
constexpr float kSegmentLength = 4.0F;
constexpr int kSegmentsPerLine = static_cast<int>((static_cast<float>(kLineHalfCount * 2) * kSpacing) / kSegmentLength);
constexpr std::uint32_t kGridVertexCapacity = static_cast<std::uint32_t>((kLineHalfCount * 2 + 1) * 2 * kSegmentsPerLine * 2);

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

[[nodiscard]] float Saturate(float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float FadeFor(float x, float z, const GridCamera& camera) noexcept {
    const float dx = x - camera.x;
    const float dz = z - camera.z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    return 1.0F - Saturate((distance - kFadeStart) / (kFadeEnd - kFadeStart));
}

[[nodiscard]] LineVertex Vertex(float x, float z, std::array<float, 3> color, const GridCamera& camera) noexcept {
    const float fade = FadeFor(x, z, camera);
    return LineVertex{x, 0.0F, z, color[0] * fade, color[1] * fade, color[2] * fade};
}

[[nodiscard]] GridCamera CameraFromView(const SceneRenderCamera& camera) noexcept {
    float inverseView[16]{};
    bx::mtxInverse(inverseView, camera.view.data());
    return GridCamera{.x = inverseView[12], .z = inverseView[14]};
}

void AddLine(
    std::array<LineVertex, kGridVertexCapacity>& vertices,
    std::uint32_t& count,
    float x0,
    float z0,
    float x1,
    float z1,
    std::array<float, 3> color,
    const GridCamera& camera) noexcept {
    for (int segment = 0; segment < kSegmentsPerLine; ++segment) {
        if (count + 2U > vertices.size()) {
            return;
        }

        const float t0 = static_cast<float>(segment) / static_cast<float>(kSegmentsPerLine);
        const float t1 = static_cast<float>(segment + 1) / static_cast<float>(kSegmentsPerLine);
        const float sx0 = x0 + (x1 - x0) * t0;
        const float sz0 = z0 + (z1 - z0) * t0;
        const float sx1 = x0 + (x1 - x0) * t1;
        const float sz1 = z0 + (z1 - z0) * t1;
        vertices[count++] = Vertex(sx0, sz0, color, camera);
        vertices[count++] = Vertex(sx1, sz1, color, camera);
    }
}

[[nodiscard]] std::uint32_t BuildGrid(std::array<LineVertex, kGridVertexCapacity>& vertices, const GridCamera& camera) noexcept {
    constexpr std::array<float, 3> kMinor{0.20F, 0.23F, 0.27F};
    constexpr std::array<float, 3> kMajor{0.34F, 0.38F, 0.44F};
    constexpr std::array<float, 3> kAxisX{0.84F, 0.22F, 0.20F};
    constexpr std::array<float, 3> kAxisZ{0.22F, 0.46F, 0.88F};

    std::uint32_t count = 0U;
    const int centerX = static_cast<int>(std::floor(camera.x / kSpacing));
    const int centerZ = static_cast<int>(std::floor(camera.z / kSpacing));
    const float minX = static_cast<float>(centerX - kLineHalfCount) * kSpacing;
    const float maxX = static_cast<float>(centerX + kLineHalfCount) * kSpacing;
    const float minZ = static_cast<float>(centerZ - kLineHalfCount) * kSpacing;
    const float maxZ = static_cast<float>(centerZ + kLineHalfCount) * kSpacing;

    for (int offset = -kLineHalfCount; offset <= kLineHalfCount; ++offset) {
        const int zLine = centerZ + offset;
        const int xLine = centerX + offset;
        const float z = static_cast<float>(zLine) * kSpacing;
        const float x = static_cast<float>(xLine) * kSpacing;
        const bool zAxis = zLine == 0;
        const bool xAxis = xLine == 0;
        const bool zMajor = zLine % 5 == 0;
        const bool xMajor = xLine % 5 == 0;
        AddLine(vertices, count, minX, z, maxX, z, zAxis ? kAxisX : (zMajor ? kMajor : kMinor), camera);
        AddLine(vertices, count, x, minZ, x, maxZ, xAxis ? kAxisZ : (xMajor ? kMajor : kMinor), camera);
    }
    return count;
}

void ConfigureOverlayView(const SceneGridPassDesc& desc) {
    const RenderViewportRect outputRect = desc.outputRect.extent.IsValid()
        ? desc.outputRect
        : RenderViewportRect{.extent = desc.extent};
    bgfx::setViewName(desc.viewId, "KB Editor Grid");
    bgfx::setViewFrameBuffer(desc.viewId, desc.frameBuffer);
    bgfx::setViewTransform(desc.viewId, desc.camera->view.data(), desc.camera->projection.data());
    bgfx::setViewRect(
        desc.viewId,
        ClampToViewExtent(outputRect.x),
        ClampToViewExtent(outputRect.y),
        ClampToViewExtent(outputRect.extent.width),
        ClampToViewExtent(outputRect.extent.height));
    bgfx::setViewClear(desc.viewId, BGFX_CLEAR_NONE);
    bgfx::setViewMode(desc.viewId, bgfx::ViewMode::Sequential);
    bgfx::touch(desc.viewId);
}

} // namespace

SceneGridPass::~SceneGridPass() {
    Shutdown();
}

bool SceneGridPassDesc::IsValid() const noexcept {
    return extent.IsValid() && (!outputRect.extent.IsValid() || outputRect.IsValid()) && camera != nullptr;
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

    std::array<LineVertex, kGridVertexCapacity> vertices{};
    const std::uint32_t vertexCount = BuildGrid(vertices, CameraFromView(*desc.camera));
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
