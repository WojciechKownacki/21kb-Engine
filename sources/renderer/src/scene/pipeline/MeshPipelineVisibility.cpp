#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace kb::render {
namespace {

[[nodiscard]] float Length3(float x, float y, float z) noexcept {
    return std::sqrt(x * x + y * y + z * z);
}

[[nodiscard]] MeshPipelineFrustumPlane NormalizePlane(MeshPipelineFrustumPlane plane) noexcept {
    const float length = Length3(plane.x, plane.y, plane.z);
    if (length <= 0.00001F) {
        return {};
    }
    const float invLength = 1.0F / length;
    return MeshPipelineFrustumPlane{
        .x = plane.x * invLength,
        .y = plane.y * invLength,
        .z = plane.z * invLength,
        .w = plane.w * invLength,
    };
}

[[nodiscard]] std::array<float, 16> MultiplyColumnMajor(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs) noexcept {
    std::array<float, 16> result{};
    for (std::uint32_t column = 0U; column < 4U; ++column) {
        for (std::uint32_t row = 0U; row < 4U; ++row) {
            float value = 0.0F;
            for (std::uint32_t k = 0U; k < 4U; ++k) {
                value += lhs[k * 4U + row] * rhs[column * 4U + k];
            }
            result[column * 4U + row] = value;
        }
    }
    return result;
}

[[nodiscard]] float MaxAxisScale(const std::array<float, 16>& model) noexcept {
    const float scaleX = Length3(model[0], model[1], model[2]);
    const float scaleY = Length3(model[4], model[5], model[6]);
    const float scaleZ = Length3(model[8], model[9], model[10]);
    return std::max(scaleX, std::max(scaleY, scaleZ));
}

[[nodiscard]] float MinAxisScale(const std::array<float, 16>& model) noexcept {
    const float scaleX = Length3(model[0], model[1], model[2]);
    const float scaleY = Length3(model[4], model[5], model[6]);
    const float scaleZ = Length3(model[8], model[9], model[10]);
    return std::min(scaleX, std::min(scaleY, scaleZ));
}

[[nodiscard]] std::array<float, 3> TransformPoint(const std::array<float, 16>& model, const std::array<float, 3>& point) noexcept {
    return { model[0] * point[0] + model[4] * point[1] + model[8] * point[2] + model[12],
             model[1] * point[0] + model[5] * point[1] + model[9] * point[2] + model[13],
             model[2] * point[0] + model[6] * point[1] + model[10] * point[2] + model[14] };
}

[[nodiscard]] std::array<float, 3> ViewPoint(const SceneRenderCamera& camera, const std::array<float, 3>& point) noexcept {
    const std::array<float, 16>& view = camera.view;
    return { view[0] * point[0] + view[4] * point[1] + view[8] * point[2] + view[12],
             view[1] * point[0] + view[5] * point[1] + view[9] * point[2] + view[13],
             view[2] * point[0] + view[6] * point[1] + view[10] * point[2] + view[14] };
}

[[nodiscard]] float ScreenCoverageEstimate(const SceneRenderCamera* camera, const RenderBoundsSphere& worldBounds) noexcept {
    if (camera == nullptr || !worldBounds.IsValid()) {
        return 1.0F;
    }
    const float depth = std::max(std::abs(MeshPipelineVisibility::ViewDepth(camera, worldBounds)), worldBounds.radius);
    return std::clamp(worldBounds.radius / std::max(depth, 0.0001F), 0.0F, 1.0F);
}

} // namespace

MeshPipelineFrustum MeshPipelineVisibility::BuildFrustum(const SceneRenderCamera* camera) noexcept {
    if (camera == nullptr) {
        return {};
    }

    const std::array<float, 16> clip = MultiplyColumnMajor(camera->projection, camera->view);
    const std::array<float, 4> row0{ clip[0], clip[4], clip[8], clip[12] };
    const std::array<float, 4> row1{ clip[1], clip[5], clip[9], clip[13] };
    const std::array<float, 4> row2{ clip[2], clip[6], clip[10], clip[14] };
    const std::array<float, 4> row3{ clip[3], clip[7], clip[11], clip[15] };

    return MeshPipelineFrustum{
        .planes = {
            NormalizePlane(MeshPipelineFrustumPlane{ row3[0] + row0[0], row3[1] + row0[1], row3[2] + row0[2], row3[3] + row0[3] }),
            NormalizePlane(MeshPipelineFrustumPlane{ row3[0] - row0[0], row3[1] - row0[1], row3[2] - row0[2], row3[3] - row0[3] }),
            NormalizePlane(MeshPipelineFrustumPlane{ row3[0] + row1[0], row3[1] + row1[1], row3[2] + row1[2], row3[3] + row1[3] }),
            NormalizePlane(MeshPipelineFrustumPlane{ row3[0] - row1[0], row3[1] - row1[1], row3[2] - row1[2], row3[3] - row1[3] }),
            NormalizePlane(MeshPipelineFrustumPlane{ row3[0] + row2[0], row3[1] + row2[1], row3[2] + row2[2], row3[3] + row2[3] }),
            NormalizePlane(MeshPipelineFrustumPlane{ row3[0] - row2[0], row3[1] - row2[1], row3[2] - row2[2], row3[3] - row2[3] }),
        },
        .valid = true,
    };
}

bool MeshPipelineVisibility::IsInsideFrustum(const MeshPipelineFrustum& frustum, const RenderBoundsSphere& bounds) noexcept {
    if (!frustum.valid || !bounds.IsValid()) {
        return true;
    }

    for (const MeshPipelineFrustumPlane& plane : frustum.planes) {
        const float distance = plane.x * bounds.center[0] + plane.y * bounds.center[1] + plane.z * bounds.center[2] + plane.w;
        if (distance < -bounds.radius) {
            return false;
        }
    }
    return true;
}

RenderBoundsSphere MeshPipelineVisibility::TransformBounds(const RenderBoundsSphere& localBounds, const std::array<float, 16>& model) noexcept {
    if (!localBounds.IsValid()) {
        return {};
    }

    const float x = localBounds.center[0];
    const float y = localBounds.center[1];
    const float z = localBounds.center[2];
    return RenderBoundsSphere{
        .center = {
            model[0] * x + model[4] * y + model[8] * z + model[12],
            model[1] * x + model[5] * y + model[9] * z + model[13],
            model[2] * x + model[6] * y + model[10] * z + model[14],
        },
        .radius = localBounds.radius * MaxAxisScale(model),
    };
}

float MeshPipelineVisibility::ViewDepth(const SceneRenderCamera* camera, const RenderBoundsSphere& bounds) noexcept {
    if (camera == nullptr || !bounds.IsValid()) {
        return 0.0F;
    }
    const std::array<float, 16>& view = camera->view;
    return view[2] * bounds.center[0] + view[6] * bounds.center[1] + view[10] * bounds.center[2] + view[14];
}

bool MeshPipelineVisibility::IsOccludedByVisibilityBlockers(
    const SceneRenderCamera* camera,
    const RenderBoundsSphere& bounds,
    std::span<const SceneRenderVisibilityBlocker> blockers) noexcept {
    if (camera == nullptr || !bounds.IsValid()) return false;
    const std::array<float, 3> candidateView = ViewPoint(*camera, bounds.center);
    const float candidateDepth = std::abs(candidateView[2]);
    if (candidateDepth <= bounds.radius || candidateDepth <= 0.0001F) return false;
    const float candidateAngularRadius = bounds.radius / candidateDepth;
    for (const SceneRenderVisibilityBlocker& blocker : blockers) {
        const float smallestSize = std::min(blocker.size[0], std::min(blocker.size[1], blocker.size[2]));
        const float radius = 0.5F * smallestSize * MinAxisScale(blocker.model);
        if (!(radius > 0.0F) || !std::isfinite(radius)) continue;
        const std::array<float, 3> blockerView = ViewPoint(*camera, TransformPoint(blocker.model, blocker.localCenter));
        const float blockerDepth = std::abs(blockerView[2]);
        // The inscribed sphere is entirely within the authored box. Rejection
        // therefore remains conservative even under a simplified proxy.
        if (blockerDepth <= radius || candidateDepth <= blockerDepth + bounds.radius) continue;
        const float blockerAngularRadius = radius / (blockerDepth + radius);
        const float dx = candidateView[0] / candidateDepth - blockerView[0] / blockerDepth;
        const float dy = candidateView[1] / candidateDepth - blockerView[1] / blockerDepth;
        if (std::sqrt(dx * dx + dy * dy) + candidateAngularRadius <= blockerAngularRadius) return true;
    }
    return false;
}

std::uint16_t MeshPipelineVisibility::DepthBucket(float depth) noexcept {
    const float shifted = std::clamp((std::abs(depth) * 16.0F), 0.0F, 65535.0F);
    return static_cast<std::uint16_t>(shifted);
}

std::uint8_t MeshPipelineVisibility::SelectLodLevel(
    const RenderMeshResource* mesh,
    const SceneRenderMeshInstance& instance,
    const SceneRenderCamera* camera) noexcept {
    if (mesh == nullptr || mesh->lods.empty()) {
        return 0U;
    }

    const RenderBoundsSphere worldBounds = TransformBounds(mesh->bounds, instance.model);
    const float coverage = ScreenCoverageEstimate(camera, worldBounds);
    std::uint8_t selected = static_cast<std::uint8_t>(std::min<std::size_t>(mesh->lods.size() - 1U, UINT8_MAX));
    for (std::size_t lodIndex = 0U; lodIndex < mesh->lods.size(); ++lodIndex) {
        const RenderMeshLodDesc& lod = mesh->lods[lodIndex];
        if (coverage >= lod.minScreenCoverage) {
            selected = static_cast<std::uint8_t>(std::min<std::size_t>(lodIndex, UINT8_MAX));
            break;
        }
    }
    return selected;
}

std::pair<std::uint32_t, std::uint32_t> MeshPipelineVisibility::MeshletRangeForSection(const RenderMeshResource* mesh, std::uint32_t sectionIndex) noexcept {
    if (mesh == nullptr || mesh->meshlets.empty()) {
        return {0U, 0U};
    }

    std::uint32_t first = 0U;
    std::uint32_t count = 0U;
    bool found = false;
    for (std::uint32_t meshletIndex = 0U; meshletIndex < mesh->meshlets.size(); ++meshletIndex) {
        if (mesh->meshlets[meshletIndex].sectionIndex != sectionIndex) {
            continue;
        }
        if (!found) {
            first = meshletIndex;
            found = true;
        }
        ++count;
    }
    return {first, count};
}

} // namespace kb::render
