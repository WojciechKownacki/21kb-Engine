#include "kb/render/scene/RenderScene.hpp"

#include "RenderSceneProxyConverters.hpp"
#include "RenderSceneProxyDirtyTracker.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] std::array<float, 16> GeometrySwarmModel(
    const GeometrySwarmRenderProxyDesc& swarm, std::uint32_t index) noexcept {
    const std::uint32_t columns = swarm.columns;
    const std::uint32_t rows = swarm.rows;
    const std::uint32_t plane = columns * rows;
    const std::uint32_t x = index % columns;
    const std::uint32_t y = (index / columns) % rows;
    const std::uint32_t z = index / plane;
    const float localX = (static_cast<float>(x) - static_cast<float>(columns - 1U) * 0.5F) * swarm.spacing[0];
    const float localY = (static_cast<float>(y) - static_cast<float>(rows - 1U) * 0.5F) * swarm.spacing[1];
    const float localZ = (static_cast<float>(z) - static_cast<float>(swarm.layers - 1U) * 0.5F) * swarm.spacing[2];
    std::array<float, 16> model = swarm.model;
    model[12] += swarm.model[0] * localX + swarm.model[4] * localY + swarm.model[8] * localZ;
    model[13] += swarm.model[1] * localX + swarm.model[5] * localY + swarm.model[9] * localZ;
    model[14] += swarm.model[2] * localX + swarm.model[6] * localY + swarm.model[10] * localZ;
    for (std::uint32_t column = 0U; column < 3U; ++column) {
        for (std::uint32_t row = 0U; row < 3U; ++row) model[column * 4U + row] *= swarm.instanceScale;
    }
    return model;
}

[[nodiscard]] std::uint64_t GeometrySwarmInstanceId(std::uint64_t owner, std::uint32_t index) noexcept {
    std::uint64_t value = owner ^ (static_cast<std::uint64_t>(index) + 0x9e3779b97f4a7c15ULL);
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] std::uint64_t SpaceStrokeInstanceId(std::uint64_t owner, std::uint32_t index) noexcept {
    return GeometrySwarmInstanceId(owner ^ 0xd1b54a32d192ed03ULL, index);
}

using StrokePoint = std::array<float, 3>;

[[nodiscard]] StrokePoint Add(StrokePoint lhs, StrokePoint rhs) noexcept { return { lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2] }; }
[[nodiscard]] StrokePoint Subtract(StrokePoint lhs, StrokePoint rhs) noexcept { return { lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2] }; }
[[nodiscard]] StrokePoint Scale(StrokePoint value, float scalar) noexcept { return { value[0] * scalar, value[1] * scalar, value[2] * scalar }; }
[[nodiscard]] float Dot(StrokePoint lhs, StrokePoint rhs) noexcept { return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2]; }
[[nodiscard]] StrokePoint Cross(StrokePoint lhs, StrokePoint rhs) noexcept { return { lhs[1] * rhs[2] - lhs[2] * rhs[1], lhs[2] * rhs[0] - lhs[0] * rhs[2], lhs[0] * rhs[1] - lhs[1] * rhs[0] }; }
[[nodiscard]] StrokePoint Normalize(StrokePoint value) noexcept { const float lengthSquared = Dot(value, value); return lengthSquared > 1.0e-10F ? Scale(value, 1.0F / std::sqrt(lengthSquared)) : StrokePoint{}; }
[[nodiscard]] StrokePoint TransformPoint(const std::array<float, 16>& matrix, StrokePoint point) noexcept {
    return { matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12], matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13], matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14] };
}
[[nodiscard]] std::uint32_t SpaceStrokeSegmentCount(const SpaceStrokeRenderProxyDesc& stroke) noexcept {
    if (stroke.controlPointCount < 2U) return 0U;
    if (stroke.mode == 2U) return 1U;
    if (stroke.mode == 1U) return stroke.splineSegments;
    return stroke.closed ? stroke.controlPointCount : stroke.controlPointCount - 1U;
}
[[nodiscard]] StrokePoint SpaceStrokeCurvePoint(const SpaceStrokeRenderProxyDesc& stroke, float parameter) noexcept {
    const std::uint32_t count = stroke.controlPointCount;
    if (stroke.mode == 2U) return parameter <= 0.0F ? stroke.controlPoints[0] : stroke.controlPoints[count - 1U];
    const float scaled = std::clamp(parameter, 0.0F, 1.0F) * static_cast<float>(stroke.closed ? count : count - 1U);
    const std::uint32_t segment = std::min(static_cast<std::uint32_t>(scaled), stroke.closed ? count - 1U : count - 2U);
    const float local = scaled - static_cast<float>(segment);
    const auto point = [&stroke, count](std::int32_t index) noexcept -> StrokePoint {
        if (stroke.closed) { index = (index % static_cast<std::int32_t>(count) + static_cast<std::int32_t>(count)) % static_cast<std::int32_t>(count); }
        else { index = std::clamp(index, 0, static_cast<std::int32_t>(count) - 1); }
        return stroke.controlPoints[static_cast<std::size_t>(index)];
    };
    if (stroke.mode != 1U) {
        StrokePoint result = Add(point(static_cast<std::int32_t>(segment)), Scale(Subtract(point(static_cast<std::int32_t>(segment + 1U)), point(static_cast<std::int32_t>(segment))), local));
        if (stroke.mode == 3U) result[1] -= stroke.cableSag * 4.0F * parameter * (1.0F - parameter);
        return result;
    }
    const StrokePoint p0 = point(static_cast<std::int32_t>(segment) - 1), p1 = point(static_cast<std::int32_t>(segment)), p2 = point(static_cast<std::int32_t>(segment) + 1), p3 = point(static_cast<std::int32_t>(segment) + 2);
    const float t2 = local * local, t3 = t2 * local;
    return Scale(Add(Add(Scale(p1, 2.0F), Scale(Subtract(p2, p0), local)), Add(Scale(Add(Add(Scale(p0, 2.0F), Scale(p1, -5.0F)), Add(Scale(p2, 4.0F), Scale(p3, -1.0F))), t2), Scale(Add(Add(Scale(p0, -1.0F), Scale(p1, 3.0F)), Add(Scale(p2, -3.0F), p3)), t3))), 0.5F);
}
[[nodiscard]] std::array<float, 16> SpaceStrokeSegmentModel(const SpaceStrokeRenderProxyDesc& stroke, std::uint32_t index) noexcept {
    const std::uint32_t count = SpaceStrokeSegmentCount(stroke);
    const float first = static_cast<float>(index) / static_cast<float>(count);
    const float second = static_cast<float>(index + 1U) / static_cast<float>(count);
    const StrokePoint start = TransformPoint(stroke.model, SpaceStrokeCurvePoint(stroke, first));
    const StrokePoint end = TransformPoint(stroke.model, SpaceStrokeCurvePoint(stroke, second));
    const StrokePoint forwardRaw = Subtract(end, start);
    const float length = std::sqrt(Dot(forwardRaw, forwardRaw));
    if (length <= 1.0e-5F) return {};
    const StrokePoint forward = Scale(forwardRaw, 1.0F / length);
    const StrokePoint basis = std::abs(forward[1]) < 0.99F ? StrokePoint{ 0.0F, 1.0F, 0.0F } : StrokePoint{ 1.0F, 0.0F, 0.0F };
    const StrokePoint right = Normalize(Cross(basis, forward));
    const StrokePoint up = Cross(forward, right);
    const StrokePoint center = Scale(Add(start, end), 0.5F);
    return { right[0] * stroke.width, right[1] * stroke.width, right[2] * stroke.width, 0.0F, up[0] * stroke.width, up[1] * stroke.width, up[2] * stroke.width, 0.0F, forward[0] * length, forward[1] * length, forward[2] * length, 0.0F, center[0], center[1], center[2], 1.0F };
}

[[nodiscard]] bool SurfaceCastContains(const SurfaceCastRenderProxyDesc& cast, const std::array<float, 16>& receiver) noexcept {
    const float px = receiver[12] - cast.model[12], py = receiver[13] - cast.model[13], pz = receiver[14] - cast.model[14];
    const float a00 = cast.model[0], a01 = cast.model[4], a02 = cast.model[8];
    const float a10 = cast.model[1], a11 = cast.model[5], a12 = cast.model[9];
    const float a20 = cast.model[2], a21 = cast.model[6], a22 = cast.model[10];
    const float determinant = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
    if (std::abs(determinant) <= 1.0e-8F) return false;
    const float inverseDeterminant = 1.0F / determinant;
    const float x = ((a11 * a22 - a12 * a21) * px + (a02 * a21 - a01 * a22) * py + (a01 * a12 - a02 * a11) * pz) * inverseDeterminant - cast.localCenter[0];
    const float y = ((a12 * a20 - a10 * a22) * px + (a00 * a22 - a02 * a20) * py + (a02 * a10 - a00 * a12) * pz) * inverseDeterminant - cast.localCenter[1];
    const float z = ((a10 * a21 - a11 * a20) * px + (a01 * a20 - a00 * a21) * py + (a00 * a11 - a01 * a10) * pz) * inverseDeterminant - cast.localCenter[2];
    switch (cast.region) {
    case RenderSurfaceCastRegion::Circle2D: return x * x + y * y <= cast.radius * cast.radius;
    case RenderSurfaceCastRegion::Rectangle2D: return std::abs(x) <= cast.size[0] * 0.5F && std::abs(y) <= cast.size[1] * 0.5F;
    case RenderSurfaceCastRegion::Sphere: return x * x + y * y + z * z <= cast.radius * cast.radius;
    case RenderSurfaceCastRegion::Box: return std::abs(x) <= cast.size[0] * 0.5F && std::abs(y) <= cast.size[1] * 0.5F && std::abs(z) <= cast.size[2] * 0.5F;
    case RenderSurfaceCastRegion::Capsule: {
        const float halfLine = std::max(0.0F, cast.height * 0.5F - cast.radius);
        const float clampedY = std::clamp(y, -halfLine, halfLine);
        const float dy = y - clampedY;
        return x * x + dy * dy + z * z <= cast.radius * cast.radius;
    }
    }
    return false;
}

} // namespace

std::size_t RenderScene::DrawGroupKeyHash::operator()(DrawGroupKey key) const noexcept {
    const std::uint64_t mixed = key.meshAssetId ^ (key.materialAssetId + 0x9e3779b97f4a7c15ULL + (key.meshAssetId << 6U) + (key.meshAssetId >> 2U));
    return static_cast<std::size_t>(mixed);
}

void RenderScene::Reserve(const RenderSceneReserveDesc& desc) {
    if (desc.meshProxies > 0U) {
        meshes_.reserve(desc.meshProxies);
    }
    if (desc.cameraProxies > 0U) {
        cameras_.reserve(desc.cameraProxies);
    }
    if (desc.lightProxies > 0U) {
        lights_.reserve(desc.lightProxies);
    }
    if (desc.visibilityBlockerProxies > 0U) visibilityBlockers_.reserve(desc.visibilityBlockerProxies);
    if (desc.geometrySwarmProxies > 0U) geometrySwarms_.reserve(desc.geometrySwarmProxies);
    if (desc.spaceStrokeProxies > 0U) spaceStrokes_.reserve(desc.spaceStrokeProxies);
    if (desc.drawGroupKeys > 0U) {
        drawGroups_.reserve(desc.drawGroupKeys);
        drawGroupLookupScratch_.reserve(desc.drawGroupKeys);
    }
}

RenderSceneStats RenderScene::Stats() const noexcept {
    return RenderSceneStats{
        .meshProxyCount = static_cast<std::uint32_t>(meshes_.size()),
        .cameraProxyCount = static_cast<std::uint32_t>(cameras_.size()),
        .lightProxyCount = static_cast<std::uint32_t>(lights_.size()),
        .visibilityBlockerProxyCount = static_cast<std::uint32_t>(visibilityBlockers_.size()),
        .geometrySwarmProxyCount = static_cast<std::uint32_t>(geometrySwarms_.size()),
        .spaceStrokeProxyCount = static_cast<std::uint32_t>(spaceStrokes_.size()),
        .meshProxyCapacity = static_cast<std::uint32_t>(meshes_.bucket_count()),
        .cameraProxyCapacity = static_cast<std::uint32_t>(cameras_.bucket_count()),
        .lightProxyCapacity = static_cast<std::uint32_t>(lights_.bucket_count()),
        .visibilityBlockerProxyCapacity = static_cast<std::uint32_t>(visibilityBlockers_.bucket_count()),
        .drawGroupLookupCapacity = static_cast<std::uint32_t>(drawGroupLookupScratch_.bucket_count()),
        .transformInPlaceUpdateCount = transformInPlaceUpdateCount_,
        .transformFallbackUpdateCount = transformFallbackUpdateCount_,
    };
}

RenderProxyId RenderScene::UpsertMesh(const MeshRenderProxyDesc& desc) {
    auto [it, inserted] = meshes_.try_emplace(desc.entityId);
    MeshRenderProxy& proxy = it->second;
    if (inserted) {
        proxy.id = AllocateProxyId();
        proxy.desc = desc;
        proxy.dirty = RenderProxyDirtyFlag::All;
        InvalidateDrawGroups();
        return proxy.id;
    }

    const RenderProxyDirtyFlag dirty = RenderSceneProxyDirtyTracker::DirtyForMeshChange(proxy.desc, desc);
    if (dirty != RenderProxyDirtyFlag::None) {
        proxy.desc = desc;
        proxy.dirty |= dirty;
        InvalidateDrawGroups();
    }
    return proxy.id;
}

RenderProxyId RenderScene::UpsertCamera(const CameraRenderProxyDesc& desc) {
    auto [it, inserted] = cameras_.try_emplace(desc.entityId);
    CameraRenderProxy& proxy = it->second;
    if (inserted) {
        proxy.id = AllocateProxyId();
        proxy.desc = desc;
        proxy.dirty = RenderProxyDirtyFlag::All;
        return proxy.id;
    }

    const RenderProxyDirtyFlag dirty = RenderSceneProxyDirtyTracker::DirtyForCameraChange(proxy.desc, desc);
    if (dirty != RenderProxyDirtyFlag::None) {
        proxy.desc = desc;
        proxy.dirty |= dirty;
    }
    return proxy.id;
}

RenderProxyId RenderScene::UpsertLight(const LightRenderProxyDesc& desc) {
    auto [it, inserted] = lights_.try_emplace(desc.entityId);
    LightRenderProxy& proxy = it->second;
    if (inserted) {
        proxy.id = AllocateProxyId();
        proxy.desc = desc;
        proxy.dirty = RenderProxyDirtyFlag::All;
        return proxy.id;
    }

    const RenderProxyDirtyFlag dirty = RenderSceneProxyDirtyTracker::DirtyForLightChange(proxy.desc, desc);
    if (dirty != RenderProxyDirtyFlag::None) {
        proxy.desc = desc;
        proxy.dirty |= dirty;
    }
    return proxy.id;
}

RenderProxyId RenderScene::UpsertVisibilityBlocker(const VisibilityBlockerRenderProxyDesc& desc) {
    auto [it, inserted] = visibilityBlockers_.try_emplace(desc.entityId);
    VisibilityBlockerRenderProxy& proxy = it->second;
    if (inserted) { proxy.id = AllocateProxyId(); proxy.desc = desc; proxy.dirty = RenderProxyDirtyFlag::All; return proxy.id; }
    if (proxy.desc.model != desc.model || proxy.desc.localCenter != desc.localCenter || proxy.desc.size != desc.size) {
        proxy.desc = desc;
        proxy.dirty |= RenderProxyDirtyFlag::Transform | RenderProxyDirtyFlag::Visibility;
    }
    return proxy.id;
}

RenderProxyId RenderScene::UpsertGeometrySwarm(const GeometrySwarmRenderProxyDesc& desc) {
    auto [it, inserted] = geometrySwarms_.try_emplace(desc.entityId);
    GeometrySwarmRenderProxy& proxy = it->second;
    if (inserted) { proxy.id = AllocateProxyId(); proxy.desc = desc; proxy.dirty = RenderProxyDirtyFlag::All; InvalidateDrawGroups(); return proxy.id; }
    const GeometrySwarmRenderProxyDesc& previous = proxy.desc;
    if (previous.meshAssetId != desc.meshAssetId || previous.materialAssetId != desc.materialAssetId || previous.model != desc.model ||
        previous.instanceCount != desc.instanceCount || previous.columns != desc.columns || previous.rows != desc.rows || previous.layers != desc.layers ||
        previous.spacing != desc.spacing || previous.instanceScale != desc.instanceScale || previous.visible != desc.visible ||
        previous.castsShadow != desc.castsShadow || previous.receivesShadow != desc.receivesShadow || previous.layer != desc.layer) {
        proxy.desc = desc; proxy.dirty |= RenderProxyDirtyFlag::All; InvalidateDrawGroups();
    }
    return proxy.id;
}

RenderProxyId RenderScene::UpsertSurfaceCast(const SurfaceCastRenderProxyDesc& desc) {
    auto [it, inserted] = surfaceCasts_.try_emplace(desc.entityId);
    SurfaceCastRenderProxy& proxy = it->second;
    if (inserted) { proxy.id = AllocateProxyId(); proxy.desc = desc; proxy.dirty = RenderProxyDirtyFlag::All; InvalidateDrawGroups(); return proxy.id; }
    if (proxy.desc.materialAssetId != desc.materialAssetId || proxy.desc.model != desc.model || proxy.desc.localCenter != desc.localCenter ||
        proxy.desc.size != desc.size || proxy.desc.radius != desc.radius || proxy.desc.height != desc.height ||
        proxy.desc.receiverLayerMask != desc.receiverLayerMask || proxy.desc.order != desc.order || proxy.desc.region != desc.region || proxy.desc.visible != desc.visible) {
        proxy.desc = desc; proxy.dirty |= RenderProxyDirtyFlag::All; InvalidateDrawGroups();
    }
    return proxy.id;
}

RenderProxyId RenderScene::UpsertSpaceStroke(const SpaceStrokeRenderProxyDesc& desc) {
    auto [it, inserted] = spaceStrokes_.try_emplace(desc.entityId);
    SpaceStrokeRenderProxy& proxy = it->second;
    if (inserted) { proxy.id = AllocateProxyId(); proxy.desc = desc; proxy.dirty = RenderProxyDirtyFlag::All; InvalidateDrawGroups(); return proxy.id; }
    if (proxy.desc.meshAssetId != desc.meshAssetId || proxy.desc.materialAssetId != desc.materialAssetId || proxy.desc.model != desc.model || proxy.desc.controlPoints != desc.controlPoints || proxy.desc.controlPointCount != desc.controlPointCount || proxy.desc.splineSegments != desc.splineSegments || proxy.desc.mode != desc.mode || proxy.desc.width != desc.width || proxy.desc.cableSag != desc.cableSag || proxy.desc.layer != desc.layer || proxy.desc.closed != desc.closed || proxy.desc.visible != desc.visible || proxy.desc.castsShadow != desc.castsShadow || proxy.desc.receivesShadow != desc.receivesShadow) {
        proxy.desc = desc; proxy.dirty |= RenderProxyDirtyFlag::All; InvalidateDrawGroups();
    }
    return proxy.id;
}

void RenderScene::SetWorldBackdrop(std::optional<SceneRenderWorldBackdrop> backdrop) noexcept {
    worldBackdrop_ = std::move(backdrop);
}

const std::optional<SceneRenderWorldBackdrop>& RenderScene::WorldBackdrop() const noexcept {
    return worldBackdrop_;
}

void RenderScene::SetAmbientRadiance(std::optional<SceneRenderAmbientRadiance> ambientRadiance) noexcept {
    ambientRadiance_ = std::move(ambientRadiance);
}

const std::optional<SceneRenderAmbientRadiance>& RenderScene::AmbientRadiance() const noexcept {
    return ambientRadiance_;
}

bool RenderScene::RemoveMesh(std::uint64_t entityId) noexcept {
    const bool removed = meshes_.erase(entityId) != 0U;
    if (removed) {
        InvalidateDrawGroups();
    }
    return removed;
}

bool RenderScene::RemoveCamera(std::uint64_t entityId) noexcept {
    return cameras_.erase(entityId) != 0U;
}

bool RenderScene::RemoveLight(std::uint64_t entityId) noexcept {
    return lights_.erase(entityId) != 0U;
}

bool RenderScene::UpdateVisibilityBlockerTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    const auto found = visibilityBlockers_.find(entityId);
    if (found == visibilityBlockers_.end()) return false;
    if (found->second.desc.model != model) {
        found->second.desc.model = model;
        found->second.dirty |= RenderProxyDirtyFlag::Transform;
    }
    return true;
}
bool RenderScene::ApplyGeometrySwarmTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    const auto found = geometrySwarms_.find(entityId);
    if (found == geometrySwarms_.end()) return false;
    GeometrySwarmRenderProxy& proxy = found->second;
    if (proxy.desc.model != model) {
        proxy.desc.model = model;
        proxy.dirty |= RenderProxyDirtyFlag::Transform;
        return true;
    }
    return false;
}
bool RenderScene::UpdateGeometrySwarmTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    if (!ApplyGeometrySwarmTransform(entityId, model)) return false;
    InvalidateDrawGroups();
    return true;
}
bool RenderScene::ApplySpaceStrokeTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    const auto found = spaceStrokes_.find(entityId);
    if (found == spaceStrokes_.end()) return false;
    SpaceStrokeRenderProxy& proxy = found->second;
    if (proxy.desc.model != model) {
        proxy.desc.model = model;
        proxy.dirty |= RenderProxyDirtyFlag::Transform;
        return true;
    }
    return false;
}
bool RenderScene::UpdateSurfaceCastTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    const auto found = surfaceCasts_.find(entityId);
    if (found == surfaceCasts_.end()) return false;
    if (found->second.desc.model != model) { found->second.desc.model = model; found->second.dirty |= RenderProxyDirtyFlag::Transform; InvalidateDrawGroups(); }
    return true;
}
bool RenderScene::UpdateSpaceStrokeTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    if (!ApplySpaceStrokeTransform(entityId, model)) return false;
    InvalidateDrawGroups();
    return true;
}
bool RenderScene::RemoveVisibilityBlocker(std::uint64_t entityId) noexcept { return visibilityBlockers_.erase(entityId) != 0U; }
bool RenderScene::RemoveGeometrySwarm(std::uint64_t entityId) noexcept { const bool removed = geometrySwarms_.erase(entityId) != 0U; if (removed) InvalidateDrawGroups(); return removed; }
bool RenderScene::RemoveSurfaceCast(std::uint64_t entityId) noexcept { const bool removed = surfaceCasts_.erase(entityId) != 0U; if (removed) InvalidateDrawGroups(); return removed; }
bool RenderScene::RemoveSpaceStroke(std::uint64_t entityId) noexcept { const bool removed = spaceStrokes_.erase(entityId) != 0U; if (removed) InvalidateDrawGroups(); return removed; }

const MeshRenderProxy* RenderScene::FindMeshByEntity(std::uint64_t entityId) const noexcept {
    const auto it = meshes_.find(entityId);
    return it == meshes_.end() ? nullptr : &it->second;
}

const CameraRenderProxy* RenderScene::FindCameraByEntity(std::uint64_t entityId) const noexcept {
    const auto it = cameras_.find(entityId);
    return it == cameras_.end() ? nullptr : &it->second;
}

const LightRenderProxy* RenderScene::FindLightByEntity(std::uint64_t entityId) const noexcept {
    const auto it = lights_.find(entityId);
    return it == lights_.end() ? nullptr : &it->second;
}

std::uint32_t RenderScene::RemoveMeshesNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept {
    std::uint32_t removed = 0U;
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (std::ranges::binary_search(sortedEntityIds, it->first)) {
            ++it;
            continue;
        }
        it = meshes_.erase(it);
        ++removed;
    }
    if (removed != 0U) {
        InvalidateDrawGroups();
    }
    return removed;
}

std::uint32_t RenderScene::RemoveCamerasNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept {
    std::uint32_t removed = 0U;
    for (auto it = cameras_.begin(); it != cameras_.end();) {
        if (std::ranges::binary_search(sortedEntityIds, it->first)) {
            ++it;
            continue;
        }
        it = cameras_.erase(it);
        ++removed;
    }
    return removed;
}

std::uint32_t RenderScene::RemoveLightsNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept {
    std::uint32_t removed = 0U;
    for (auto it = lights_.begin(); it != lights_.end();) {
        if (std::ranges::binary_search(sortedEntityIds, it->first)) {
            ++it;
            continue;
        }
        it = lights_.erase(it);
        ++removed;
    }
    return removed;
}
std::uint32_t RenderScene::RemoveVisibilityBlockersNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept {
    std::uint32_t removed = 0U;
    for (auto it = visibilityBlockers_.begin(); it != visibilityBlockers_.end();) {
        if (std::ranges::binary_search(sortedEntityIds, it->first)) { ++it; continue; }
        it = visibilityBlockers_.erase(it); ++removed;
    }
    return removed;
}
std::uint32_t RenderScene::RemoveGeometrySwarmsNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept {
    std::uint32_t removed = 0U;
    for (auto it = geometrySwarms_.begin(); it != geometrySwarms_.end();) {
        if (std::ranges::binary_search(sortedEntityIds, it->first)) { ++it; continue; }
        it = geometrySwarms_.erase(it); ++removed;
    }
    if (removed != 0U) InvalidateDrawGroups();
    return removed;
}
std::uint32_t RenderScene::RemoveSurfaceCastsNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept {
    std::uint32_t removed = 0U;
    for (auto it = surfaceCasts_.begin(); it != surfaceCasts_.end();) {
        if (std::ranges::binary_search(sortedEntityIds, it->first)) { ++it; continue; }
        it = surfaceCasts_.erase(it); ++removed;
    }
    if (removed != 0U) InvalidateDrawGroups();
    return removed;
}
std::uint32_t RenderScene::RemoveSpaceStrokesNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept {
    std::uint32_t removed = 0U;
    for (auto it = spaceStrokes_.begin(); it != spaceStrokes_.end();) {
        if (std::ranges::binary_search(sortedEntityIds, it->first)) { ++it; continue; }
        it = spaceStrokes_.erase(it); ++removed;
    }
    if (removed != 0U) InvalidateDrawGroups();
    return removed;
}

std::size_t RenderScene::MeshProxyCount() const noexcept {
    return meshes_.size();
}

std::size_t RenderScene::CameraProxyCount() const noexcept {
    return cameras_.size();
}

std::size_t RenderScene::LightProxyCount() const noexcept {
    return lights_.size();
}
std::size_t RenderScene::VisibilityBlockerProxyCount() const noexcept { return visibilityBlockers_.size(); }
std::size_t RenderScene::GeometrySwarmProxyCount() const noexcept { return geometrySwarms_.size(); }
std::size_t RenderScene::SurfaceCastProxyCount() const noexcept { return surfaceCasts_.size(); }
std::size_t RenderScene::SpaceStrokeProxyCount() const noexcept { return spaceStrokes_.size(); }

const RenderScene::MeshProxyMap& RenderScene::MeshProxies() const noexcept {
    return meshes_;
}

const RenderScene::CameraProxyMap& RenderScene::CameraProxies() const noexcept {
    return cameras_;
}

const RenderScene::LightProxyMap& RenderScene::LightProxies() const noexcept {
    return lights_;
}
const RenderScene::VisibilityBlockerProxyMap& RenderScene::VisibilityBlockerProxies() const noexcept { return visibilityBlockers_; }
const RenderScene::GeometrySwarmProxyMap& RenderScene::GeometrySwarmProxies() const noexcept { return geometrySwarms_; }
const RenderScene::SurfaceCastProxyMap& RenderScene::SurfaceCastProxies() const noexcept { return surfaceCasts_; }
const RenderScene::SpaceStrokeProxyMap& RenderScene::SpaceStrokeProxies() const noexcept { return spaceStrokes_; }

void RenderScene::ClearDirty() noexcept {
    for (auto& [entityId, proxy] : meshes_) {
        proxy.dirty = RenderProxyDirtyFlag::None;
        static_cast<void>(entityId);
    }
    for (auto& [entityId, proxy] : cameras_) {
        proxy.dirty = RenderProxyDirtyFlag::None;
        static_cast<void>(entityId);
    }
    for (auto& [entityId, proxy] : lights_) {
        proxy.dirty = RenderProxyDirtyFlag::None;
        static_cast<void>(entityId);
    }
    for (auto& [entityId, proxy] : visibilityBlockers_) { proxy.dirty = RenderProxyDirtyFlag::None; static_cast<void>(entityId); }
    for (auto& [entityId, proxy] : geometrySwarms_) { proxy.dirty = RenderProxyDirtyFlag::None; static_cast<void>(entityId); }
    for (auto& [entityId, proxy] : surfaceCasts_) { proxy.dirty = RenderProxyDirtyFlag::None; static_cast<void>(entityId); }
    for (auto& [entityId, proxy] : spaceStrokes_) { proxy.dirty = RenderProxyDirtyFlag::None; static_cast<void>(entityId); }
}

const CameraRenderProxyDesc* RenderScene::FindPrimaryCameraProxy(std::uint32_t targetViewportId) const noexcept {
    const CameraRenderProxyDesc* selected = nullptr;
    for (const auto& [entityId, proxy] : cameras_) {
        const CameraRenderProxyDesc& camera = proxy.desc;
        if (!camera.visible || !camera.primary) {
            continue;
        }
        if (camera.viewportId != 0U && camera.viewportId != targetViewportId) {
            continue;
        }
        if (selected == nullptr || camera.priority > selected->priority ||
            (camera.priority == selected->priority && camera.entityId < selected->entityId)) {
            selected = &camera;
        }
    }
    return selected;
}

std::optional<SceneRenderCamera> RenderScene::BuildPrimaryCamera(std::uint32_t viewportWidth, std::uint32_t viewportHeight, std::uint32_t targetViewportId) const {
    const CameraRenderProxyDesc* selected = FindPrimaryCameraProxy(targetViewportId);
    if (selected == nullptr) {
        return std::nullopt;
    }
    return RenderSceneCameraBuilder::Build(*selected, viewportWidth, viewportHeight);
}

const std::vector<SceneRenderDrawGroup>& RenderScene::DrawGroups() const {
    RebuildDrawGroupsIfNeeded();
    return drawGroups_;
}

void RenderScene::BuildDrawGroups(std::vector<SceneRenderDrawGroup>& outDrawGroups) const {
    outDrawGroups = DrawGroups();
}

std::size_t RenderScene::DrawGroupCapacity() const noexcept {
    return drawGroups_.capacity();
}

std::size_t RenderScene::DrawGroupInstanceCapacity() const noexcept {
    std::size_t capacity = 0U;
    for (const SceneRenderDrawGroup& group : drawGroups_) {
        capacity += group.instances.capacity();
    }
    return capacity;
}

std::size_t RenderScene::DrawGroupLookupScratchCapacity() const noexcept {
    return drawGroupLookupScratch_.bucket_count();
}

void RenderScene::BuildSnapshotInto(std::uint32_t viewportWidth, std::uint32_t viewportHeight, SceneRenderSnapshot& outSnapshot, std::uint32_t targetViewportId) const {
    outSnapshot.camera = BuildPrimaryCamera(viewportWidth, viewportHeight, targetViewportId);
    outSnapshot.meshes.clear();
    outSnapshot.lights.clear();

    std::size_t geometrySwarmInstanceCount = 0U;
    for (const auto& [entityId, proxy] : geometrySwarms_) {
        static_cast<void>(entityId);
        if (proxy.desc.visible) geometrySwarmInstanceCount += proxy.desc.instanceCount;
    }
    std::size_t spaceStrokeInstanceCount = 0U;
    for (const auto& [entityId, proxy] : spaceStrokes_) { static_cast<void>(entityId); if (proxy.desc.visible) spaceStrokeInstanceCount += SpaceStrokeSegmentCount(proxy.desc); }
    outSnapshot.meshes.reserve(meshes_.size() + geometrySwarmInstanceCount + spaceStrokeInstanceCount);
    for (const auto& [entityId, proxy] : meshes_) {
        const MeshRenderProxyDesc& mesh = proxy.desc;
        if (!mesh.visible) {
            static_cast<void>(entityId);
            continue;
        }
        SceneRenderMeshInstance instance = RenderSceneMeshInstanceBuilder::Build(mesh);
        ApplySurfaceCasts(instance);
        outSnapshot.meshes.push_back(instance);
    }
    for (const auto& [entityId, proxy] : geometrySwarms_) {
        const GeometrySwarmRenderProxyDesc& swarm = proxy.desc;
        if (!swarm.visible || swarm.meshAssetId == 0U || swarm.instanceCount == 0U || swarm.columns == 0U || swarm.rows == 0U || swarm.layers == 0U) {
            continue;
        }
        for (std::uint32_t index = 0U; index < swarm.instanceCount; ++index) {
            SceneRenderMeshInstance instance{
                .entityId = GeometrySwarmInstanceId(entityId, index),
                .meshAssetId = swarm.meshAssetId,
                .materialAssetId = swarm.materialAssetId,
                .model = GeometrySwarmModel(swarm, index),
                .castsShadow = swarm.castsShadow,
                .receivesShadow = swarm.receivesShadow,
                .layer = swarm.layer,
            };
            ApplySurfaceCasts(instance);
            outSnapshot.meshes.push_back(instance);
        }
    }
    for (const auto& [entityId, proxy] : spaceStrokes_) {
        const SpaceStrokeRenderProxyDesc& stroke = proxy.desc;
        const std::uint32_t segmentCount = SpaceStrokeSegmentCount(stroke);
        if (!stroke.visible || stroke.meshAssetId == 0U || segmentCount == 0U) continue;
        for (std::uint32_t index = 0U; index < segmentCount; ++index) {
            const std::array<float, 16> model = SpaceStrokeSegmentModel(stroke, index);
            if (model[15] == 0.0F) continue;
            SceneRenderMeshInstance instance{ .entityId = SpaceStrokeInstanceId(entityId, index), .meshAssetId = stroke.meshAssetId, .materialAssetId = stroke.materialAssetId, .model = model, .castsShadow = stroke.castsShadow, .receivesShadow = stroke.receivesShadow, .layer = stroke.layer };
            ApplySurfaceCasts(instance);
            outSnapshot.meshes.push_back(instance);
        }
    }

    outSnapshot.lights.reserve(lights_.size());
    for (const auto& [entityId, proxy] : lights_) {
        const LightRenderProxyDesc& light = proxy.desc;
        if (!light.visible) {
            static_cast<void>(entityId);
            continue;
        }
        outSnapshot.lights.push_back(RenderSceneLightBuilder::Build(light));
    }
}

RenderProxyId RenderScene::AllocateProxyId() noexcept {
    return RenderProxyId{ nextProxyId_++ };
}

void RenderScene::InvalidateDrawGroups() noexcept {
    drawGroupsDirty_ = true;
}

void RenderScene::RebuildDrawGroupsIfNeeded() const {
    if (!drawGroupsDirty_) {
        return;
    }

    for (SceneRenderDrawGroup& group : drawGroups_) {
        group.instances.clear();
    }

    drawGroupLookupScratch_.clear();
    drawGroupLookupScratch_.reserve(meshes_.size() + geometrySwarms_.size() + spaceStrokes_.size());
    // A fresh build version invalidates every previously stamped instance location.
    ++drawGroupBuildVersion_;
    std::size_t writeGroupCount = 0U;
    for (const auto& [entityId, proxy] : meshes_) {
        const MeshRenderProxyDesc& mesh = proxy.desc;
        if (!mesh.visible) {
            static_cast<void>(entityId);
            continue;
        }

        SceneRenderMeshInstance instance = RenderSceneMeshInstanceBuilder::Build(mesh);
        ApplySurfaceCasts(instance);
        const DrawGroupKey key{ .meshAssetId = instance.meshAssetId, .materialAssetId = instance.materialAssetId };
        auto lookupIt = drawGroupLookupScratch_.find(key);
        if (lookupIt == drawGroupLookupScratch_.end()) {
            if (writeGroupCount == drawGroups_.size()) {
                drawGroups_.push_back(SceneRenderDrawGroup{});
            }
            SceneRenderDrawGroup& group = drawGroups_[writeGroupCount];
            group.meshAssetId = instance.meshAssetId;
            group.materialAssetId = instance.materialAssetId;
            lookupIt = drawGroupLookupScratch_.emplace(key, writeGroupCount).first;
            ++writeGroupCount;
        }

        const std::size_t groupIndex = lookupIt->second;
        SceneRenderDrawGroup& group = drawGroups_[groupIndex];
        // Stamp the instance location directly on the proxy (mutable) so a later
        // transform-only update needs a single proxy lookup, no second map.
        proxy.instanceGroupIndex = static_cast<std::uint32_t>(groupIndex);
        proxy.instanceIndexInGroup = static_cast<std::uint32_t>(group.instances.size());
        proxy.instanceLocationVersion = drawGroupBuildVersion_;
        group.instances.push_back(instance);
    }

    // Geometry swarm data stays compact and canonical in ECS.  Only here, at
    // the existing mesh-batch boundary, it expands into the per-instance stream
    // consumed by GPU culling and indirect submission.
    for (const auto& [entityId, proxy] : geometrySwarms_) {
        const GeometrySwarmRenderProxyDesc& swarm = proxy.desc;
        if (!swarm.visible || swarm.meshAssetId == 0U || swarm.instanceCount == 0U || swarm.columns == 0U || swarm.rows == 0U || swarm.layers == 0U) {
            continue;
        }
        for (std::uint32_t index = 0U; index < swarm.instanceCount; ++index) {
            SceneRenderMeshInstance instance{
                .entityId = GeometrySwarmInstanceId(entityId, index),
                .meshAssetId = swarm.meshAssetId,
                .materialAssetId = swarm.materialAssetId,
                .model = GeometrySwarmModel(swarm, index),
                .castsShadow = swarm.castsShadow,
                .receivesShadow = swarm.receivesShadow,
                .layer = swarm.layer,
            };
            ApplySurfaceCasts(instance);
            const DrawGroupKey key{ .meshAssetId = instance.meshAssetId, .materialAssetId = instance.materialAssetId };
            auto lookupIt = drawGroupLookupScratch_.find(key);
            if (lookupIt == drawGroupLookupScratch_.end()) {
                if (writeGroupCount == drawGroups_.size()) drawGroups_.push_back(SceneRenderDrawGroup{});
                SceneRenderDrawGroup& group = drawGroups_[writeGroupCount];
                group.meshAssetId = instance.meshAssetId; group.materialAssetId = instance.materialAssetId;
                lookupIt = drawGroupLookupScratch_.emplace(key, writeGroupCount).first; ++writeGroupCount;
            }
            drawGroups_[lookupIt->second].instances.push_back(instance);
        }
    }
    for (const auto& [entityId, proxy] : spaceStrokes_) {
        const SpaceStrokeRenderProxyDesc& stroke = proxy.desc;
        const std::uint32_t segmentCount = SpaceStrokeSegmentCount(stroke);
        if (!stroke.visible || stroke.meshAssetId == 0U || segmentCount == 0U) continue;
        for (std::uint32_t index = 0U; index < segmentCount; ++index) {
            const std::array<float, 16> model = SpaceStrokeSegmentModel(stroke, index);
            if (model[15] == 0.0F) continue;
            SceneRenderMeshInstance instance{ .entityId = SpaceStrokeInstanceId(entityId, index), .meshAssetId = stroke.meshAssetId, .materialAssetId = stroke.materialAssetId, .model = model, .castsShadow = stroke.castsShadow, .receivesShadow = stroke.receivesShadow, .layer = stroke.layer };
            ApplySurfaceCasts(instance);
            const DrawGroupKey key{ .meshAssetId = instance.meshAssetId, .materialAssetId = instance.materialAssetId };
            auto lookupIt = drawGroupLookupScratch_.find(key);
            if (lookupIt == drawGroupLookupScratch_.end()) {
                if (writeGroupCount == drawGroups_.size()) drawGroups_.push_back(SceneRenderDrawGroup{});
                SceneRenderDrawGroup& group = drawGroups_[writeGroupCount];
                group.meshAssetId = instance.meshAssetId; group.materialAssetId = instance.materialAssetId;
                lookupIt = drawGroupLookupScratch_.emplace(key, writeGroupCount).first; ++writeGroupCount;
            }
            drawGroups_[lookupIt->second].instances.push_back(instance);
        }
    }

    drawGroups_.resize(writeGroupCount);
    drawGroupsDirty_ = false;
}

void RenderScene::ApplySurfaceCasts(SceneRenderMeshInstance& instance) const {
    const SurfaceCastRenderProxy* selected = nullptr;
    for (const auto& [entityId, proxy] : surfaceCasts_) {
        static_cast<void>(entityId);
        const SurfaceCastRenderProxyDesc& cast = proxy.desc;
        if (!cast.visible || cast.materialAssetId == 0U || (instance.layer & cast.receiverLayerMask) == 0U || !SurfaceCastContains(cast, instance.model)) continue;
        if (selected == nullptr || cast.order > selected->desc.order || (cast.order == selected->desc.order && cast.entityId > selected->desc.entityId)) selected = &proxy;
    }
    if (selected != nullptr) instance.materialAssetId = selected->desc.materialAssetId;
}

RenderScene::TransformUpdateOutcome RenderScene::ApplyMeshTransform(std::uint64_t entityId, const std::array<float, 16>& model) {
    const auto proxyIt = meshes_.find(entityId);
    if (proxyIt == meshes_.end()) {
        return TransformUpdateOutcome::NotFound;
    }

    MeshRenderProxy& proxy = proxyIt->second;
    proxy.desc.model = model; // source of truth, always current

    // Fast path: clean cache + a location stamped by the current build. Single
    // proxy lookup, instance refreshed in place. No invalidation, no telemetry.
    if (!drawGroupsDirty_ && proxy.instanceLocationVersion == drawGroupBuildVersion_ &&
        proxy.instanceGroupIndex < drawGroups_.size()) {
        SceneRenderDrawGroup& group = drawGroups_[proxy.instanceGroupIndex];
        if (proxy.instanceIndexInGroup < group.instances.size() &&
            group.instances[proxy.instanceIndexInGroup].entityId == entityId) {
            group.instances[proxy.instanceIndexInGroup].model = model;
            proxy.dirty |= RenderProxyDirtyFlag::Transform;
            return TransformUpdateOutcome::InPlace;
        }
    }

    proxy.dirty |= RenderProxyDirtyFlag::Transform;
    return TransformUpdateOutcome::Fallback;
}

void RenderScene::InvalidateDrawGroupsIfFallback(TransformUpdateOutcome outcome) noexcept {
    if (outcome == TransformUpdateOutcome::Fallback) {
        InvalidateDrawGroups();
    }
}

void RenderScene::AddTransformUpdateCounts(std::uint64_t inPlace, std::uint64_t fallback) noexcept {
    transformInPlaceUpdateCount_ += inPlace;
    transformFallbackUpdateCount_ += fallback;
}

bool RenderScene::UpdateMeshTransform(std::uint64_t entityId, const std::array<float, 16>& model) {
    const TransformUpdateOutcome outcome = ApplyMeshTransform(entityId, model);
    switch (outcome) {
    case TransformUpdateOutcome::NotFound:
        return false;
    case TransformUpdateOutcome::InPlace:
        ++transformInPlaceUpdateCount_;
        return true;
    case TransformUpdateOutcome::Fallback:
        ++transformFallbackUpdateCount_;
        InvalidateDrawGroups();
        return true;
    }
    return false;
}

} // namespace kb::render
