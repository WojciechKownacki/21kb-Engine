#include "kb/render/overlay/SceneGridPass.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/ShaderLoader.hpp"

#include <bx/math.h>

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
    float y = 0.0F;
    float z = 0.0F;
    float forwardY = 1.0F;
};

struct GridBuildParams {
    float smallStep = 1.0F;
    float largeStep = 8.0F;
    float divisionDecimals = 0.0F;
    float shaderFadeSize = 192.0F;
    float centerX = 0.0F;
    float centerZ = 0.0F;
};

constexpr int kGridSize = 200;
constexpr int kPrimaryGridSteps = 8;
constexpr int kDivisionLevelMin = 0;
constexpr int kDivisionLevelMax = 2;
constexpr float kDivisionLevelBias = -0.2F;
constexpr std::uint32_t kGridVertexCapacity = static_cast<std::uint32_t>((kGridSize * 2 + 1) * 4);

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

[[nodiscard]] float SmoothStep(float edge0, float edge1, float value) noexcept {
    const float t = Saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] std::array<float, 3> LerpColor(std::array<float, 3> a, std::array<float, 3> b, float t) noexcept {
    return std::array<float, 3>{
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
    };
}

[[nodiscard]] std::array<float, 3> ScaleColor(std::array<float, 3> color, float scale) noexcept {
    return std::array<float, 3>{ color[0] * scale, color[1] * scale, color[2] * scale };
}

[[nodiscard]] GridCamera CameraFromView(const SceneRenderCamera& camera) noexcept {
    float inverseView[16]{};
    bx::mtxInverse(inverseView, camera.view.data());
    return GridCamera{
        .x = inverseView[12],
        .y = inverseView[13],
        .z = inverseView[14],
        .forwardY = inverseView[9],
    };
}

[[nodiscard]] GridBuildParams BuildParams(const GridCamera& camera) noexcept {
    const float cameraDistance = std::max(std::abs(camera.y), 0.0001F);
    const float primarySteps = static_cast<float>(kPrimaryGridSteps);
    const float divisionLevel = (std::log(cameraDistance) / std::log(primarySteps)) + kDivisionLevelBias;
    const float clampedLevel = std::clamp(divisionLevel, static_cast<float>(kDivisionLevelMin), static_cast<float>(kDivisionLevelMax));
    const float flooredLevel = std::floor(clampedLevel);
    const float divisionDecimals = clampedLevel - flooredLevel;
    const float smallStep = std::pow(primarySteps, flooredLevel);
    const float largeStep = smallStep * primarySteps;
    const float centerX = largeStep * static_cast<float>(static_cast<int>(camera.x / largeStep));
    const float centerZ = largeStep * static_cast<float>(static_cast<int>(camera.z / largeStep));

    float fadeStep = std::pow(primarySteps, divisionLevel - 1.0F);
    const float minFadeStep = std::pow(primarySteps, static_cast<float>(kDivisionLevelMin));
    const float maxFadeStep = std::pow(primarySteps, static_cast<float>(kDivisionLevelMax));
    fadeStep = std::clamp(fadeStep, minFadeStep, maxFadeStep);

    return GridBuildParams{
        .smallStep = smallStep,
        .largeStep = largeStep,
        .divisionDecimals = divisionDecimals,
        .shaderFadeSize = static_cast<float>(kGridSize - kPrimaryGridSteps) * fadeStep,
        .centerX = centerX,
        .centerZ = centerZ,
    };
}

void AddLine(
    std::array<LineVertex, kGridVertexCapacity>& vertices,
    std::uint32_t& count,
    float x0,
    float z0,
    float x1,
    float z1,
    std::array<float, 3> color) noexcept {
    if (count + 2U > vertices.size()) {
        return;
    }
    vertices[count++] = LineVertex{ x0, 0.0F, z0, color[0], color[1], color[2] };
    vertices[count++] = LineVertex{ x1, 0.0F, z1, color[0], color[1], color[2] };
}

[[nodiscard]] std::uint32_t BuildGrid(std::array<LineVertex, kGridVertexCapacity>& vertices, const GridBuildParams& params) noexcept {
    constexpr std::array<float, 3> kSecondary{ 0.20F, 0.23F, 0.27F };
    constexpr std::array<float, 3> kPrimary{ 0.34F, 0.38F, 0.44F };
    constexpr std::array<float, 3> kAxisX{ 0.84F, 0.22F, 0.20F };
    constexpr std::array<float, 3> kAxisZ{ 0.22F, 0.46F, 0.88F };

    std::uint32_t count = 0U;
    const float beginX = params.centerX - static_cast<float>(kGridSize) * params.smallStep;
    const float endX = params.centerX + static_cast<float>(kGridSize) * params.smallStep;
    const float beginZ = params.centerZ - static_cast<float>(kGridSize) * params.smallStep;
    const float endZ = params.centerZ + static_cast<float>(kGridSize) * params.smallStep;

    for (int i = -kGridSize; i <= kGridSize; ++i) {
        const bool primary = i % kPrimaryGridSteps == 0;
        const std::array<float, 3> color = primary
            ? LerpColor(kPrimary, kSecondary, params.divisionDecimals)
            : ScaleColor(kSecondary, 1.0F - params.divisionDecimals);
        const float x = params.centerX + static_cast<float>(i) * params.smallStep;
        const float z = params.centerZ + static_cast<float>(i) * params.smallStep;

        AddLine(vertices, count, x, beginZ, x, endZ, x == 0.0F ? kAxisZ : color);
        AddLine(vertices, count, beginX, z, endX, z, z == 0.0F ? kAxisX : color);
    }
    return count;
}

void ConfigureOverlayView(const SceneGridPassDesc& desc) {
    const RenderViewportRect outputRect = desc.outputRect.extent.IsValid()
        ? desc.outputRect
        : RenderViewportRect{ .extent = desc.extent };
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
    gridParamsUniform_ = bgfx::createUniform("u_editorGridParams", bgfx::UniformType::Vec4);
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
    if (bgfx::isValid(gridParamsUniform_)) {
        bgfx::destroy(gridParamsUniform_);
        gridParamsUniform_ = BGFX_INVALID_HANDLE;
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

    const GridCamera camera = CameraFromView(*desc.camera);
    const GridBuildParams params = BuildParams(camera);
    std::array<LineVertex, kGridVertexCapacity> vertices{};
    const std::uint32_t vertexCount = BuildGrid(vertices, params);
    if (vertexCount == 0U || bgfx::getAvailTransientVertexBuffer(vertexCount, lineLayout_) < vertexCount) {
        return false;
    }

    ConfigureOverlayView(desc);
    bgfx::TransientVertexBuffer buffer{};
    bgfx::allocTransientVertexBuffer(&buffer, vertexCount, lineLayout_);
    std::memcpy(buffer.data, vertices.data(), sizeof(LineVertex) * vertexCount);

    const float angleFade = SmoothStep(0.05F, 0.2F, std::abs(camera.forwardY));
    const std::array<float, 4> gridParams{ camera.x, camera.z, params.shaderFadeSize, angleFade * 0.72F };
    bgfx::setUniform(gridParamsUniform_, gridParams.data());
    bgfx::setState(SceneDepthPolicy::SceneOverlayState(true) | BGFX_STATE_PT_LINES | BGFX_STATE_BLEND_ALPHA);
    bgfx::setVertexBuffer(0, &buffer);
    bgfx::submit(desc.viewId, program_);
    return true;
}

bool SceneGridPass::IsInitialized() const noexcept {
    return initialized_ && bgfx::isValid(program_) && bgfx::isValid(gridParamsUniform_) && lineLayout_.getStride() == sizeof(LineVertex);
}

} // namespace kb::render
