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
constexpr float kFadeStart = 28.0F;
constexpr float kFadeEnd = 72.0F;
constexpr float kHorizontalFadeStart = 14.0F;
constexpr float kHorizontalFadeEnd = 34.0F;
constexpr float kHorizontalDetailEnd = 16.0F;
constexpr float kHorizontalMajorEnd = 26.0F;
constexpr std::uint32_t kMaxSegmentsPerGridLine = 3U;
constexpr std::uint32_t kGridVertexCapacity = static_cast<std::uint32_t>((kLineHalfCount * 2 + 1) * 2) * kMaxSegmentsPerGridLine * 2U;

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
    const float t = Saturate((distance - kFadeStart) / (kFadeEnd - kFadeStart));
    const float smooth = t * t * (3.0F - 2.0F * t);
    const float fade = 1.0F - smooth;
    return fade * fade;
}

[[nodiscard]] LineVertex Vertex(float x, float z, std::array<float, 3> color, const GridCamera& camera) noexcept {
    const float fade = FadeFor(x, z, camera);
    return LineVertex{x, 0.0F, z, color[0] * fade, color[1] * fade, color[2] * fade};
}

[[nodiscard]] LineVertex VertexWithFade(float x, float z, std::array<float, 3> color, float fade) noexcept {
    return LineVertex{x, 0.0F, z, color[0] * fade, color[1] * fade, color[2] * fade};
}

[[nodiscard]] GridCamera CameraFromView(const SceneRenderCamera& camera) noexcept {
    float inverseView[16]{};
    bx::mtxInverse(inverseView, camera.view.data());
    return GridCamera{.x = inverseView[12], .z = inverseView[14]};
}

void AddSegment(
    std::array<LineVertex, kGridVertexCapacity>& vertices,
    std::uint32_t& count,
    float x0,
    float z0,
    float x1,
    float z1,
    std::array<float, 3> color,
    const GridCamera& camera) noexcept {
    if (count + 2U > vertices.size()) {
        return;
    }
    vertices[count++] = Vertex(x0, z0, color, camera);
    vertices[count++] = Vertex(x1, z1, color, camera);
}

void AddHorizontalLine(
    std::array<LineVertex, kGridVertexCapacity>& vertices,
    std::uint32_t& count,
    float z,
    std::array<float, 3> color,
    const GridCamera& camera) noexcept {
    const float dz = std::abs(z - camera.z);
    if (dz >= kHorizontalFadeEnd) {
        return;
    }

    const float t = Saturate((dz - kHorizontalFadeStart) / (kHorizontalFadeEnd - kHorizontalFadeStart));
    const float smooth = t * t * (3.0F - 2.0F * t);
    const float lineFade = (1.0F - smooth) * (1.0F - smooth);
    const float halfLength = std::sqrt(std::max(0.0F, kFadeEnd * kFadeEnd - dz * dz));
    const float x0 = camera.x - halfLength;
    const float x1 = camera.x + halfLength;
    if (count + 2U > vertices.size()) {
        return;
    }
    vertices[count++] = VertexWithFade(x0, z, color, lineFade);
    vertices[count++] = VertexWithFade(x1, z, color, lineFade);
}

void AddVerticalLine(
    std::array<LineVertex, kGridVertexCapacity>& vertices,
    std::uint32_t& count,
    float x,
    std::array<float, 3> color,
    const GridCamera& camera) noexcept {
    const float dx = std::abs(x - camera.x);
    if (dx >= kFadeEnd) {
        return;
    }

    const float outer = std::sqrt(std::max(0.0F, kFadeEnd * kFadeEnd - dx * dx));
    const float z0 = camera.z - outer;
    const float z1 = camera.z + outer;
    if (dx >= kFadeStart) {
        AddSegment(vertices, count, x, z0, x, camera.z, color, camera);
        AddSegment(vertices, count, x, camera.z, x, z1, color, camera);
        return;
    }

    const float inner = std::sqrt(std::max(0.0F, kFadeStart * kFadeStart - dx * dx));
    const float nearInner = camera.z - inner;
    const float farInner = camera.z + inner;
    AddSegment(vertices, count, x, z0, x, nearInner, color, camera);
    AddSegment(vertices, count, x, nearInner, x, farInner, color, camera);
    AddSegment(vertices, count, x, farInner, x, z1, color, camera);
}

[[nodiscard]] std::uint32_t BuildGrid(std::array<LineVertex, kGridVertexCapacity>& vertices, const GridCamera& camera) noexcept {
    constexpr std::array<float, 3> kMinor{0.20F, 0.23F, 0.27F};
    constexpr std::array<float, 3> kMajor{0.34F, 0.38F, 0.44F};
    constexpr std::array<float, 3> kAxisX{0.84F, 0.22F, 0.20F};
    constexpr std::array<float, 3> kAxisZ{0.22F, 0.46F, 0.88F};

    std::uint32_t count = 0U;
    const int centerX = static_cast<int>(std::floor(camera.x / kSpacing));
    const int centerZ = static_cast<int>(std::floor(camera.z / kSpacing));

    for (int offset = -kLineHalfCount; offset <= kLineHalfCount; ++offset) {
        const int zLine = centerZ + offset;
        const int xLine = centerX + offset;
        const float z = static_cast<float>(zLine) * kSpacing;
        const float x = static_cast<float>(xLine) * kSpacing;
        const bool zAxis = zLine == 0;
        const bool xAxis = xLine == 0;
        const bool zMajor = zLine % 5 == 0;
        const bool xMajor = xLine % 5 == 0;
        const float horizontalDistance = std::abs(z - camera.z);
        const bool drawHorizontal =
            horizontalDistance < kHorizontalDetailEnd ||
            zAxis ||
            (horizontalDistance < kHorizontalMajorEnd && zMajor);
        if (drawHorizontal) {
            AddHorizontalLine(vertices, count, z, zAxis ? kAxisX : (zMajor ? kMajor : kMinor), camera);
        }
        AddVerticalLine(vertices, count, x, xAxis ? kAxisZ : (xMajor ? kMajor : kMinor), camera);
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
