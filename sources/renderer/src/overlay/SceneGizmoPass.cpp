#include "kb/render/overlay/SceneGizmoPass.hpp"

#include "kb/render/ShaderLoader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace kb::render {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kShaftStart = 0.094F;
constexpr float kShaftEnd = 0.840F;
constexpr float kAxisLength = 1.160F;
constexpr float kShaftRadius = 0.026F;
constexpr float kTipRadius = 0.086F;
constexpr float kHubRadius = 0.106F;
constexpr float kScaleTipHalfSize = 0.085F;
constexpr float kRotateRingRadius = 0.792F;
constexpr float kRotateRingTubeRadius = 0.014F;
constexpr float kTipLength = kAxisLength - kShaftEnd;
constexpr std::uint32_t kShaftSegments = 36U;
constexpr std::uint32_t kTipSegments = 48U;
constexpr std::uint32_t kRotateRingSegments = 96U;
constexpr std::uint32_t kRotateRingTubeSegments = 8U;
constexpr std::uint32_t kHubStacks = 16U;
constexpr std::uint32_t kHubSlices = 32U;

using Vertex = SceneGizmoPass::GizmoVertex;
using MeshRange = SceneGizmoPass::MeshRange;

enum class GizmoAxis {
    X = 0,
    Y = 1,
    Z = 2,
    None = -1,
};

struct AxisBasis {
    std::array<float, 3> c0{1.0F, 0.0F, 0.0F};
    std::array<float, 3> c1{0.0F, 1.0F, 0.0F};
    std::array<float, 3> c2{0.0F, 0.0F, 1.0F};
};

struct PassDesc {
    float scaleFactor = 1.0F;
    float tintR = 1.0F;
    float tintG = 1.0F;
    float tintB = 1.0F;
    float tintA = 1.0F;
};

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

[[nodiscard]] bgfx::VertexLayout GizmoLayout() noexcept {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return layout;
}

void ConfigureOverlayView(const SceneGizmoPassDesc& desc) {
    const RenderViewportRect outputRect = desc.outputRect.extent.IsValid()
        ? desc.outputRect
        : RenderViewportRect{.extent = desc.extent};
    bgfx::setViewName(desc.viewId, "KB Editor Gizmo");
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

void AppendCylinderShaft(std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices, MeshRange& range) {
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(vertices.size());
    const std::uint32_t baseIndex = static_cast<std::uint32_t>(indices.size());

    for (std::uint32_t i = 0; i <= kShaftSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kShaftSegments);
        const float a = t * 2.0F * kPi;
        const float cx = std::cos(a) * kShaftRadius;
        const float cy = std::sin(a) * kShaftRadius;
        const float nx = std::cos(a);
        const float ny = std::sin(a);
        vertices.push_back(Vertex{.x = cx, .y = cy, .z = kShaftStart, .nx = nx, .ny = ny, .nz = 0.0F});
        vertices.push_back(Vertex{.x = cx, .y = cy, .z = kShaftEnd, .nx = nx, .ny = ny, .nz = 0.0F});
    }

    for (std::uint32_t i = 0; i < kShaftSegments; ++i) {
        const std::uint32_t i0 = baseVertex + i * 2U;
        const std::uint32_t i1 = i0 + 1U;
        const std::uint32_t i2 = i0 + 2U;
        const std::uint32_t i3 = i0 + 3U;
        indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
        indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
    }

    range.indexStart = baseIndex;
    range.indexCount = static_cast<std::uint32_t>(indices.size()) - baseIndex;
}

void AppendConeTip(std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices, MeshRange& range) {
    const std::uint32_t baseIndex = static_cast<std::uint32_t>(indices.size());
    for (std::uint32_t i = 0; i < kTipSegments; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(kTipSegments);
        const float t1 = static_cast<float>(i + 1U) / static_cast<float>(kTipSegments);
        const float a0 = t0 * 2.0F * kPi;
        const float a1 = t1 * 2.0F * kPi;
        const float c0x = std::cos(a0) * kTipRadius;
        const float c0y = std::sin(a0) * kTipRadius;
        const float c1x = std::cos(a1) * kTipRadius;
        const float c1y = std::sin(a1) * kTipRadius;
        const float mid = (a0 + a1) * 0.5F;
        const float slant = std::sqrt(kTipRadius * kTipRadius + kTipLength * kTipLength);
        const float nx = std::cos(mid) * (kTipLength / slant);
        const float ny = std::sin(mid) * (kTipLength / slant);
        const float nz = kTipRadius / slant;
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(Vertex{.x = c0x, .y = c0y, .z = kShaftEnd, .nx = nx, .ny = ny, .nz = nz});
        vertices.push_back(Vertex{.x = c1x, .y = c1y, .z = kShaftEnd, .nx = nx, .ny = ny, .nz = nz});
        vertices.push_back(Vertex{.x = 0.0F, .y = 0.0F, .z = kAxisLength, .nx = nx, .ny = ny, .nz = nz});
        indices.push_back(base); indices.push_back(base + 1U); indices.push_back(base + 2U);
    }

    const std::uint32_t capCenter = static_cast<std::uint32_t>(vertices.size());
    vertices.push_back(Vertex{.x = 0.0F, .y = 0.0F, .z = kShaftEnd, .nx = 0.0F, .ny = 0.0F, .nz = -1.0F});
    for (std::uint32_t i = 0; i <= kTipSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kTipSegments);
        const float a = t * 2.0F * kPi;
        vertices.push_back(Vertex{.x = std::cos(a) * kTipRadius, .y = std::sin(a) * kTipRadius, .z = kShaftEnd, .nx = 0.0F, .ny = 0.0F, .nz = -1.0F});
    }
    for (std::uint32_t i = 0; i < kTipSegments; ++i) {
        indices.push_back(capCenter);
        indices.push_back(capCenter + 1U + i + 1U);
        indices.push_back(capCenter + 1U + i);
    }

    range.indexStart = baseIndex;
    range.indexCount = static_cast<std::uint32_t>(indices.size()) - baseIndex;
}

void AppendBoxTip(std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices, MeshRange& range) {
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(vertices.size());
    const std::uint32_t baseIndex = static_cast<std::uint32_t>(indices.size());
    const float min = -kScaleTipHalfSize;
    const float max = kScaleTipHalfSize;
    const float center = kShaftEnd + kScaleTipHalfSize;
    vertices.push_back(Vertex{.x = min, .y = min, .z = center - kScaleTipHalfSize, .nx = -1.0F, .ny = -1.0F, .nz = -1.0F});
    vertices.push_back(Vertex{.x = max, .y = min, .z = center - kScaleTipHalfSize, .nx = 1.0F, .ny = -1.0F, .nz = -1.0F});
    vertices.push_back(Vertex{.x = max, .y = max, .z = center - kScaleTipHalfSize, .nx = 1.0F, .ny = 1.0F, .nz = -1.0F});
    vertices.push_back(Vertex{.x = min, .y = max, .z = center - kScaleTipHalfSize, .nx = -1.0F, .ny = 1.0F, .nz = -1.0F});
    vertices.push_back(Vertex{.x = min, .y = min, .z = center + kScaleTipHalfSize, .nx = -1.0F, .ny = -1.0F, .nz = 1.0F});
    vertices.push_back(Vertex{.x = max, .y = min, .z = center + kScaleTipHalfSize, .nx = 1.0F, .ny = -1.0F, .nz = 1.0F});
    vertices.push_back(Vertex{.x = max, .y = max, .z = center + kScaleTipHalfSize, .nx = 1.0F, .ny = 1.0F, .nz = 1.0F});
    vertices.push_back(Vertex{.x = min, .y = max, .z = center + kScaleTipHalfSize, .nx = -1.0F, .ny = 1.0F, .nz = 1.0F});

    constexpr std::array<std::uint32_t, 36U> boxIndices{{
        0, 2, 1, 0, 3, 2,
        4, 5, 6, 4, 6, 7,
        0, 1, 5, 0, 5, 4,
        1, 2, 6, 1, 6, 5,
        2, 3, 7, 2, 7, 6,
        3, 0, 4, 3, 4, 7,
    }};
    for (const std::uint32_t index : boxIndices) {
        indices.push_back(baseVertex + index);
    }

    range.indexStart = baseIndex;
    range.indexCount = static_cast<std::uint32_t>(indices.size()) - baseIndex;
}

void AppendRotateRing(std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices, MeshRange& range) {
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(vertices.size());
    const std::uint32_t baseIndex = static_cast<std::uint32_t>(indices.size());

    for (std::uint32_t ring = 0; ring <= kRotateRingSegments; ++ring) {
        const float rt = static_cast<float>(ring) / static_cast<float>(kRotateRingSegments);
        const float ringAngle = rt * 2.0F * kPi;
        const float ringCos = std::cos(ringAngle);
        const float ringSin = std::sin(ringAngle);
        for (std::uint32_t tube = 0; tube <= kRotateRingTubeSegments; ++tube) {
            const float tt = static_cast<float>(tube) / static_cast<float>(kRotateRingTubeSegments);
            const float tubeAngle = tt * 2.0F * kPi;
            const float tubeCos = std::cos(tubeAngle);
            const float tubeSin = std::sin(tubeAngle);
            const float radius = kRotateRingRadius + kRotateRingTubeRadius * tubeCos;
            vertices.push_back(Vertex{
                .x = radius * ringCos,
                .y = radius * ringSin,
                .z = kRotateRingTubeRadius * tubeSin,
                .nx = tubeCos * ringCos,
                .ny = tubeCos * ringSin,
                .nz = tubeSin,
            });
        }
    }

    const std::uint32_t row = kRotateRingTubeSegments + 1U;
    for (std::uint32_t ring = 0; ring < kRotateRingSegments; ++ring) {
        for (std::uint32_t tube = 0; tube < kRotateRingTubeSegments; ++tube) {
            const std::uint32_t i0 = baseVertex + ring * row + tube;
            const std::uint32_t i1 = i0 + 1U;
            const std::uint32_t i2 = i0 + row;
            const std::uint32_t i3 = i2 + 1U;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }

    range.indexStart = baseIndex;
    range.indexCount = static_cast<std::uint32_t>(indices.size()) - baseIndex;
}

void AppendCenterSphere(std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices, MeshRange& range) {
    const std::uint32_t baseIndex = static_cast<std::uint32_t>(indices.size());
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(vertices.size());
    for (std::uint32_t stack = 0; stack <= kHubStacks; ++stack) {
        const float vt = static_cast<float>(stack) / static_cast<float>(kHubStacks);
        const float phi = vt * kPi;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (std::uint32_t slice = 0; slice <= kHubSlices; ++slice) {
            const float ut = static_cast<float>(slice) / static_cast<float>(kHubSlices);
            const float theta = ut * 2.0F * kPi;
            const float x = sinPhi * std::cos(theta);
            const float y = sinPhi * std::sin(theta);
            const float z = cosPhi;
            vertices.push_back(Vertex{.x = x * kHubRadius, .y = y * kHubRadius, .z = z * kHubRadius, .nx = x, .ny = y, .nz = z});
        }
    }

    const std::uint32_t row = kHubSlices + 1U;
    for (std::uint32_t stack = 0; stack < kHubStacks; ++stack) {
        for (std::uint32_t slice = 0; slice < kHubSlices; ++slice) {
            const std::uint32_t i0 = baseVertex + stack * row + slice;
            const std::uint32_t i1 = i0 + 1U;
            const std::uint32_t i2 = i0 + row;
            const std::uint32_t i3 = i2 + 1U;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }

    range.indexStart = baseIndex;
    range.indexCount = static_cast<std::uint32_t>(indices.size()) - baseIndex;
}

[[nodiscard]] AxisBasis AxisRotation(GizmoAxis axis) noexcept {
    switch (axis) {
    case GizmoAxis::X:
        return AxisBasis{.c0 = {0.0F, 0.0F, -1.0F}, .c1 = {0.0F, 1.0F, 0.0F}, .c2 = {1.0F, 0.0F, 0.0F}};
    case GizmoAxis::Y:
        return AxisBasis{.c0 = {1.0F, 0.0F, 0.0F}, .c1 = {0.0F, 0.0F, -1.0F}, .c2 = {0.0F, 1.0F, 0.0F}};
    case GizmoAxis::Z:
    case GizmoAxis::None:
    default:
        return {};
    }
}

[[nodiscard]] std::array<float, 3> ShaftColor(GizmoAxis axis) noexcept {
    switch (axis) {
    case GizmoAxis::X: return {1.000F, 0.259F, 0.184F};
    case GizmoAxis::Y: return {0.141F, 0.482F, 1.000F};
    case GizmoAxis::Z: return {0.353F, 0.847F, 0.224F};
    case GizmoAxis::None:
    default: return {1.0F, 1.0F, 1.0F};
    }
}

[[nodiscard]] std::array<float, 3> ConeColor(GizmoAxis axis) noexcept {
    switch (axis) {
    case GizmoAxis::X: return {0.824F, 0.173F, 0.133F};
    case GizmoAxis::Y: return {0.094F, 0.337F, 0.824F};
    case GizmoAxis::Z: return {0.267F, 0.698F, 0.165F};
    case GizmoAxis::None:
    default: return {1.0F, 1.0F, 1.0F};
    }
}

[[nodiscard]] Vertex TransformVertex(
    const Vertex& source,
    const AxisBasis& basis,
    const std::array<float, 3>& target,
    float scale,
    const std::array<float, 3>& color,
    float alpha) noexcept {
    auto transform = [&](float x, float y, float z, float w) -> std::array<float, 3> {
        return {
            basis.c0[0] * x + basis.c1[0] * y + basis.c2[0] * z + target[0] * w,
            basis.c0[1] * x + basis.c1[1] * y + basis.c2[1] * z + target[1] * w,
            basis.c0[2] * x + basis.c1[2] * y + basis.c2[2] * z + target[2] * w,
        };
    };

    const std::array<float, 3> p = transform(source.x * scale, source.y * scale, source.z * scale, 1.0F);
    const std::array<float, 3> n = transform(source.nx, source.ny, source.nz, 0.0F);
    return Vertex{
        .x = p[0], .y = p[1], .z = p[2],
        .nx = n[0], .ny = n[1], .nz = n[2],
        .r = color[0], .g = color[1], .b = color[2],
        .alpha = alpha,
    };
}

void AppendDraw(
    std::vector<Vertex>& output,
    const std::vector<Vertex>& sourceVertices,
    const std::vector<std::uint32_t>& sourceIndices,
    MeshRange range,
    const AxisBasis& basis,
    const std::array<float, 3>& target,
    float scale,
    std::array<float, 3> baseColor,
    const PassDesc& pass,
    float boost) {
    std::array<float, 3> color{
        std::min(1.0F, baseColor[0] * pass.tintR * boost),
        std::min(1.0F, baseColor[1] * pass.tintG * boost),
        std::min(1.0F, baseColor[2] * pass.tintB * boost),
    };

    const float activeScale = boost > 1.01F ? 1.065F : 1.0F;
    const float alpha = std::min(1.0F, pass.tintA * (boost > 1.01F ? 1.7F : 1.0F));
    const std::uint32_t end = range.indexStart + range.indexCount;
    for (std::uint32_t index = range.indexStart; index < end; ++index) {
        output.push_back(TransformVertex(sourceVertices[sourceIndices[index]], basis, target, scale * pass.scaleFactor * activeScale, color, alpha));
    }
}

[[nodiscard]] float AxisBoost(GizmoAxis axis, int hoveredAxis, int draggedAxis) noexcept {
    const int axisIndex = static_cast<int>(axis);
    if (axisIndex == draggedAxis) {
        return 1.75F;
    }
    if (axisIndex == hoveredAxis) {
        return 1.45F;
    }
    return 1.0F;
}

} // namespace

SceneGizmoPass::~SceneGizmoPass() {
    Shutdown();
}

bool SceneGizmoPassDesc::IsValid() const noexcept {
    return extent.IsValid() && (!outputRect.extent.IsValid() || outputRect.IsValid()) && camera != nullptr;
}

bool SceneGizmoPass::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    program_ = ShaderLoader::LoadProgram("vs_editor_gizmo.sc", "fs_editor_gizmo.sc");
    layout_ = GizmoLayout();
    vertices_.clear();
    indices_.clear();
    vertices_.reserve(768);
    indices_.reserve(12000);
    AppendCylinderShaft(vertices_, indices_, shaft_);
    AppendConeTip(vertices_, indices_, tip_);
    AppendBoxTip(vertices_, indices_, scaleTip_);
    AppendRotateRing(vertices_, indices_, rotateRing_);
    AppendCenterSphere(vertices_, indices_, hub_);
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
    layout_ = {};
    vertices_.clear();
    indices_.clear();
    shaft_ = {};
    tip_ = {};
    scaleTip_ = {};
    rotateRing_ = {};
    hub_ = {};
    initialized_ = false;
}

bool SceneGizmoPass::Submit(const SceneGizmoPassDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid() || !desc.visible || desc.worldScale <= 0.0F) {
        return false;
    }

    constexpr std::array<PassDesc, 2U> passes{{
        PassDesc{.scaleFactor = 1.020F, .tintR = 0.70F, .tintG = 0.70F, .tintB = 0.70F, .tintA = 0.24F},
        PassDesc{.scaleFactor = 1.000F, .tintR = 0.90F, .tintG = 0.90F, .tintB = 0.90F, .tintA = 1.00F},
    }};

    std::vector<Vertex> drawVertices;
    drawVertices.reserve(18000);
    for (const PassDesc& pass : passes) {
        for (const GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
            const AxisBasis basis = AxisRotation(axis);
            const float boost = AxisBoost(axis, desc.hoveredAxis, desc.draggedAxis);
            if (desc.mode == 1U) {
                AppendDraw(drawVertices, vertices_, indices_, rotateRing_, basis, desc.targetPosition, desc.worldScale, ShaftColor(axis), pass, boost);
            } else {
                AppendDraw(drawVertices, vertices_, indices_, shaft_, basis, desc.targetPosition, desc.worldScale, ShaftColor(axis), pass, boost);
                AppendDraw(
                    drawVertices,
                    vertices_,
                    indices_,
                    desc.mode == 2U ? scaleTip_ : tip_,
                    basis,
                    desc.targetPosition,
                    desc.worldScale,
                    ConeColor(axis),
                    pass,
                    boost);
            }
        }
        AppendDraw(drawVertices, vertices_, indices_, hub_, AxisRotation(GizmoAxis::None), desc.targetPosition, desc.worldScale, {1.0F, 0.957F, 0.941F}, pass, 1.0F);
    }

    const std::uint32_t vertexCount = static_cast<std::uint32_t>(drawVertices.size());
    if (vertexCount == 0U || bgfx::getAvailTransientVertexBuffer(vertexCount, layout_) < vertexCount) {
        return false;
    }

    ConfigureOverlayView(desc);
    bgfx::TransientVertexBuffer buffer{};
    bgfx::allocTransientVertexBuffer(&buffer, vertexCount, layout_);
    std::memcpy(buffer.data, drawVertices.data(), sizeof(Vertex) * drawVertices.size());

    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    bgfx::setVertexBuffer(0, &buffer);
    bgfx::submit(desc.viewId, program_);
    return true;
}

bool SceneGizmoPass::IsInitialized() const noexcept {
    return initialized_ && bgfx::isValid(program_) && layout_.getStride() == sizeof(GizmoVertex) &&
           !vertices_.empty() && !indices_.empty();
}

} // namespace kb::render
