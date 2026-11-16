#include "kb/render/scene/RenderScene.hpp"

#include "RenderSceneProxyConverters.hpp"
#include "RenderSceneProxyDirtyTracker.hpp"
#include "scene/GeometrySwarmVisibilityClusters.hpp"
#include "scene/SceneMeshVisibilityIndexBuilder.hpp"

#include "engine/ecs/QueryPrefetch.hpp"
#include "engine/ecs/WorkerPool.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <utility>

namespace kb::render {
namespace {

// The indexes of the draw groups are made and refreshed on workers from this many groups, in chunks of this many.
constexpr std::size_t kMinGroupsOnWorkers = 256U;
constexpr std::size_t kGroupsPerChunk = 32U;

// The instances of queued moves are refreshed on workers from this many moves, in chunks of this many.
constexpr std::size_t kMinMovesOnWorkers = 4096U;
constexpr std::size_t kMovesPerChunk = 1024U;

// Fetches every cache line of the bytes at `first`.
void PrefetchBytes(const void* first, std::size_t size, kb::ecs::QueryPrefetchAccess access = kb::ecs::QueryPrefetchAccess::Read) noexcept {
    const char* bytes = static_cast<const char*>(first);
    for (std::size_t offset = 0U; offset < size; offset += 64U) kb::ecs::PrefetchQueryMemory(bytes + offset, access);
    kb::ecs::PrefetchQueryMemory(bytes + size - 1U, access);
}

// How many queued moves ahead the refresh fetches the proxy, the draw group and the instance of a move.
constexpr std::size_t kProxyAhead = 16U;
constexpr std::size_t kGroupAhead = 10U;
constexpr std::size_t kInstanceAhead = 4U;

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
    return static_cast<std::size_t>(mixed ^ (key.owner * 0x85ebca77c2b2ae63ULL) ^ key.kind);
}

void RenderScene::MeshProxyIndex::Add(std::uint64_t entityId, MeshRenderProxy& proxy, std::size_t meshCount) {
    if (entityId >= slots_.size()) {
        const std::size_t limit = 4U * meshCount + 1024U;
        if (entityId >= limit) return;
        slots_.resize(std::min(limit, std::max(static_cast<std::size_t>(entityId) + 1U, slots_.size() * 2U)), nullptr);
    }
    slots_[static_cast<std::size_t>(entityId)] = &proxy;
}

void RenderScene::Reserve(const RenderSceneReserveDesc& desc) {
    if (desc.meshProxies > 0U) {
        meshes_.reserve(desc.meshProxies);
        sortedMeshProxies_.proxies.reserve(desc.meshProxies);
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

namespace {

[[nodiscard]] bool CastsMeshShadow(const MeshRenderProxyDesc& desc) noexcept {
    return desc.visible && desc.castsShadow;
}

// Whether two models differ as floats (-0 equals 0, NaN equals nothing), compared without stopping at the first element.
[[nodiscard]] bool ModelsDiffer(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs) noexcept {
    bool differ = false;
    for (std::size_t index = 0U; index < lhs.size(); ++index) differ |= lhs[index] != rhs[index];
    return differ;
}

// Whether a change of a mesh proxy can alter the shadow it casts: where it is, what it draws, whether it casts.
[[nodiscard]] bool ChangesMeshShadow(const MeshRenderProxyDesc& before, const MeshRenderProxyDesc& after) noexcept {
    if (!CastsMeshShadow(before) && !CastsMeshShadow(after)) return false;
    return before.model != after.model || before.meshAssetId != after.meshAssetId || before.materialAssetId != after.materialAssetId ||
        before.materialSlotOverrideCount != after.materialSlotOverrideCount || before.materialSlotAssetIds != after.materialSlotAssetIds ||
        before.boundsOverride.center != after.boundsOverride.center || before.boundsOverride.radius != after.boundsOverride.radius ||
        before.currentSkinningPalette != after.currentSkinningPalette || before.visible != after.visible ||
        before.castsShadow != after.castsShadow || before.shadowOnly != after.shadowOnly || before.layer != after.layer ||
        before.morphDeformationEnabled != after.morphDeformationEnabled || before.lodBias != after.lodBias ||
        before.lodEnabled != after.lodEnabled || before.detailSwitchEnabled != after.detailSwitchEnabled ||
        before.isStatic != after.isStatic;
}

} // namespace

RenderProxyId RenderScene::UpsertMesh(const MeshRenderProxyDesc& desc) {
    auto [it, inserted] = meshes_.try_emplace(desc.entityId);
    MeshRenderProxy& proxy = it->second;
    if (inserted) {
        meshById_.Add(desc.entityId, proxy, meshes_.size());
        meshContentRevision_ = NextContentRevision();
        meshSetVersion_ = NextContentRevision();
        if (!sortedMeshProxies_.dirty) {
            if (sortedMeshProxies_.proxies.empty() ||
                desc.entityId > sortedMeshProxies_.proxies.back()->desc.entityId) {
                sortedMeshProxies_.proxies.push_back(&proxy);
            } else {
                sortedMeshProxies_.dirty = true;
            }
        }
        proxy.id = AllocateProxyId();
        proxy.desc = desc;
        proxy.dirty = RenderProxyDirtyFlag::All;
        proxy.spawnMotionFrame = motionFrame_;
        if (CastsMeshShadow(desc)) proxy.shadowChangeFrame = motionFrame_;
        if (!drawGroupsDirty_ && desc.visible) AppendMeshInstance(proxy);
        return proxy.id;
    }

    const RenderProxyDirtyFlag dirty = RenderSceneProxyDirtyTracker::DirtyForMeshChange(proxy.desc, desc);
    if (dirty != RenderProxyDirtyFlag::None) {
        meshContentRevision_ = NextContentRevision();
        if (ChangesMeshShadow(proxy.desc, desc)) proxy.shadowChangeFrame = motionFrame_;
        if (proxy.desc.model != desc.model) {
            RecordPreviousModel(proxy, proxy.desc.model);
        }
        const bool sameGroupFlags = proxy.desc.visible && desc.visible &&
            proxy.desc.morphDeformationEnabled == desc.morphDeformationEnabled &&
            (proxy.desc.materialSlotOverrideCount != 0U) == (desc.materialSlotOverrideCount != 0U);
        if (!drawGroupsDirty_ && sameGroupFlags &&
            proxy.instanceLocationVersion == drawGroupBuildVersion_ &&
            proxy.instanceGroupIndex < drawGroups_.size()) {
            SceneRenderDrawGroup& group = drawGroups_[proxy.instanceGroupIndex];
            if (proxy.instanceIndexInGroup < group.instances.size() &&
                group.instances[proxy.instanceIndexInGroup].entityId == desc.entityId) {
                MeshRenderProxy updated = proxy;
                updated.desc = desc;
                const SceneRenderMeshInstance instance = BuildMeshInstance(updated);
                if (instance.meshAssetId == group.meshAssetId && instance.materialAssetId == group.materialAssetId) {
                    group.instances[proxy.instanceIndexInGroup] = instance;
                    SceneMeshVisibilityIndexBuilder::MarkChanged(group.visibility, proxy.instanceIndexInGroup);
                    visibilityStale_ = 1U;
                    group.contentRevision = NextContentRevision();
                    proxy.desc = desc;
                    proxy.dirty |= dirty;
                    return proxy.id;
                }
            }
        }
        const bool removedInPlace = RemoveMeshInstance(proxy);
        proxy.desc = desc;
        proxy.dirty |= dirty;
        if (removedInPlace) {
            if (desc.visible) AppendMeshInstance(proxy);
        } else InvalidateDrawGroups();
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
        lightContentRevision_ = NextContentRevision();
        return proxy.id;
    }

    const RenderProxyDirtyFlag dirty = RenderSceneProxyDirtyTracker::DirtyForLightChange(proxy.desc, desc);
    if (dirty != RenderProxyDirtyFlag::None) {
        proxy.desc = desc;
        proxy.dirty |= dirty;
        lightContentRevision_ = NextContentRevision();
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
    if (inserted) {
        proxy.id = AllocateProxyId(); proxy.desc = desc; proxy.dirty = RenderProxyDirtyFlag::All;
        proxy.spawnMotionFrame = motionFrame_;
        InvalidateDrawGroupsOfNonMeshProxy();
        return proxy.id;
    }
    const GeometrySwarmRenderProxyDesc& previous = proxy.desc;
    const bool onlyGrew = previous.meshAssetId == desc.meshAssetId && previous.materialAssetId == desc.materialAssetId &&
        previous.model == desc.model && previous.instanceCount < desc.instanceCount && previous.columns == desc.columns &&
        previous.rows == desc.rows && previous.layers == desc.layers && previous.spacing == desc.spacing &&
        previous.instanceScale == desc.instanceScale && previous.visible == desc.visible &&
        previous.castsShadow == desc.castsShadow && previous.receivesShadow == desc.receivesShadow && previous.layer == desc.layer;
    if (onlyGrew && !drawGroupsDirty_ && surfaceCasts_.empty()) {
        const DrawGroupKey key{ .meshAssetId = desc.meshAssetId, .materialAssetId = desc.materialAssetId, .owner = desc.entityId, .kind = 1U };
        const auto groupIt = drawGroupLookupScratch_.find(key);
        if (groupIt != drawGroupLookupScratch_.end() && groupIt->second < drawGroups_.size()) {
            SceneRenderDrawGroup& group = drawGroups_[groupIt->second];
            if (group.meshAssetId == desc.meshAssetId && group.materialAssetId == desc.materialAssetId) {
                const std::uint32_t firstNewInstance = previous.instanceCount;
                const auto firstGenerated = group.instances.size();
                NoteNonMeshShadowCasterChange();
                meshContentRevision_ = NextContentRevision();
                group.contentRevision = NextContentRevision();
                proxy.desc = desc;
                group.instances.reserve(group.instances.size() + (desc.instanceCount - firstNewInstance));
                for (std::uint32_t index = firstNewInstance; index < desc.instanceCount; ++index) {
                    SceneRenderMeshInstance instance{
                        .entityId = GeometrySwarmInstanceId(desc.entityId, index),
                        .meshAssetId = desc.meshAssetId,
                        .materialAssetId = desc.materialAssetId,
                        .model = GeometrySwarmModel(desc, index),
                        .castsShadow = desc.castsShadow,
                        .receivesShadow = desc.receivesShadow,
                        .layer = desc.layer,
                    };
                    ApplySurfaceCasts(instance);
                    group.instances.push_back(instance);
                }
                if (surfaceCasts_.empty()) GeometrySwarmVisibilityClusters::Append(group, firstGenerated);
                return proxy.id;
            }
        }
    }
    if (previous.meshAssetId != desc.meshAssetId || previous.materialAssetId != desc.materialAssetId || previous.model != desc.model ||
        previous.instanceCount != desc.instanceCount || previous.columns != desc.columns || previous.rows != desc.rows || previous.layers != desc.layers ||
        previous.spacing != desc.spacing || previous.instanceScale != desc.instanceScale || previous.visible != desc.visible ||
        previous.castsShadow != desc.castsShadow || previous.receivesShadow != desc.receivesShadow || previous.layer != desc.layer) {
        GeometrySwarmRenderProxyDesc movedOnly = desc;
        movedOnly.model = previous.model;
        if (movedOnly.instanceCount == previous.instanceCount && movedOnly.columns == previous.columns &&
            movedOnly.rows == previous.rows && movedOnly.layers == previous.layers && movedOnly.spacing == previous.spacing &&
            movedOnly.instanceScale == previous.instanceScale) {
            RecordPreviousSwarmModel(proxy, previous.model);
        } else {
            // A new layout places other instances at each index: none of them has a previous frame.
            proxy.previousModelFrame = 0U;
        }
        proxy.desc = desc; proxy.dirty |= RenderProxyDirtyFlag::All; InvalidateDrawGroupsOfNonMeshProxy();
    }
    return proxy.id;
}

RenderProxyId RenderScene::UpsertSurfaceCast(const SurfaceCastRenderProxyDesc& desc) {
    auto [it, inserted] = surfaceCasts_.try_emplace(desc.entityId);
    SurfaceCastRenderProxy& proxy = it->second;
    if (inserted) { proxy.id = AllocateProxyId(); proxy.desc = desc; proxy.dirty = RenderProxyDirtyFlag::All; InvalidateDrawGroupsOfNonMeshProxy(); return proxy.id; }
    if (proxy.desc.materialAssetId != desc.materialAssetId || proxy.desc.model != desc.model || proxy.desc.localCenter != desc.localCenter ||
        proxy.desc.size != desc.size || proxy.desc.radius != desc.radius || proxy.desc.height != desc.height ||
        proxy.desc.receiverLayerMask != desc.receiverLayerMask || proxy.desc.order != desc.order || proxy.desc.region != desc.region || proxy.desc.visible != desc.visible) {
        proxy.desc = desc; proxy.dirty |= RenderProxyDirtyFlag::All; InvalidateDrawGroupsOfNonMeshProxy();
    }
    return proxy.id;
}

RenderProxyId RenderScene::UpsertSpaceStroke(const SpaceStrokeRenderProxyDesc& desc) {
    auto [it, inserted] = spaceStrokes_.try_emplace(desc.entityId);
    SpaceStrokeRenderProxy& proxy = it->second;
    if (inserted) { proxy.id = AllocateProxyId(); proxy.desc = desc; proxy.dirty = RenderProxyDirtyFlag::All; InvalidateDrawGroupsOfNonMeshProxy(); return proxy.id; }
    if (proxy.desc.meshAssetId != desc.meshAssetId || proxy.desc.materialAssetId != desc.materialAssetId || proxy.desc.model != desc.model || proxy.desc.controlPoints != desc.controlPoints || proxy.desc.controlPointCount != desc.controlPointCount || proxy.desc.splineSegments != desc.splineSegments || proxy.desc.mode != desc.mode || proxy.desc.width != desc.width || proxy.desc.cableSag != desc.cableSag || proxy.desc.layer != desc.layer || proxy.desc.closed != desc.closed || proxy.desc.visible != desc.visible || proxy.desc.castsShadow != desc.castsShadow || proxy.desc.receivesShadow != desc.receivesShadow) {
        proxy.desc = desc; proxy.dirty |= RenderProxyDirtyFlag::All; InvalidateDrawGroupsOfNonMeshProxy();
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

void RenderScene::SetLocalReflectionProbes(std::vector<SceneRenderReflectionProbe> probes) {
    localReflectionProbes_ = std::move(probes);
}

std::span<const SceneRenderReflectionProbe> RenderScene::LocalReflectionProbes() const noexcept {
    return localReflectionProbes_;
}

bool RenderScene::RemoveMesh(std::uint64_t entityId) noexcept {
    const auto found = meshes_.find(entityId);
    if (found == meshes_.end()) return false;
    if (!RemoveMeshInstance(found->second)) InvalidateDrawGroups();
    else meshContentRevision_ = NextContentRevision();
    NoteNonMeshShadowCasterChange();
    ForgetQueuedMove(found->second);
    meshById_.Remove(entityId);
    meshes_.erase(found);
    meshSetVersion_ = NextContentRevision();
    sortedMeshProxies_.dirty = true;
    return true;
}

bool RenderScene::RemoveCamera(std::uint64_t entityId) noexcept {
    return cameras_.erase(entityId) != 0U;
}

bool RenderScene::RemoveLight(std::uint64_t entityId) noexcept {
    if (lights_.erase(entityId) == 0U) return false;
    lightContentRevision_ = NextContentRevision();
    return true;
}

bool RenderScene::UpdateVisibilityBlockerTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    // Most scenes have none of these proxies: the transform sync asks for every entity, and asking an empty map costs a hash.
    if (visibilityBlockers_.empty()) return false;
    const auto found = visibilityBlockers_.find(entityId);
    if (found == visibilityBlockers_.end()) return false;
    if (found->second.desc.model != model) {
        found->second.desc.model = model;
        found->second.dirty |= RenderProxyDirtyFlag::Transform;
    }
    return true;
}
bool RenderScene::ApplyGeometrySwarmTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    if (geometrySwarms_.empty()) return false;
    const auto found = geometrySwarms_.find(entityId);
    if (found == geometrySwarms_.end()) return false;
    GeometrySwarmRenderProxy& proxy = found->second;
    if (proxy.desc.model != model) {
        RecordPreviousSwarmModel(proxy, proxy.desc.model);
        proxy.desc.model = model;
        proxy.dirty |= RenderProxyDirtyFlag::Transform;
        return true;
    }
    return false;
}
bool RenderScene::UpdateGeometrySwarmTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    if (!ApplyGeometrySwarmTransform(entityId, model)) return false;
    InvalidateDrawGroupsOfNonMeshProxy();
    return true;
}
bool RenderScene::ApplySurfaceCastTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    if (surfaceCasts_.empty()) return false;
    const auto found = surfaceCasts_.find(entityId);
    if (found == surfaceCasts_.end()) return false;
    if (found->second.desc.model == model) return false;
    found->second.desc.model = model;
    found->second.dirty |= RenderProxyDirtyFlag::Transform;
    return true;
}
bool RenderScene::ApplySpaceStrokeTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    if (spaceStrokes_.empty()) return false;
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
    if (!ApplySurfaceCastTransform(entityId, model)) return false;
    InvalidateDrawGroupsOfNonMeshProxy();
    return true;
}
bool RenderScene::UpdateSpaceStrokeTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept {
    if (!ApplySpaceStrokeTransform(entityId, model)) return false;
    InvalidateDrawGroupsOfNonMeshProxy();
    return true;
}
bool RenderScene::RemoveVisibilityBlocker(std::uint64_t entityId) noexcept { return visibilityBlockers_.erase(entityId) != 0U; }
bool RenderScene::RemoveGeometrySwarm(std::uint64_t entityId) noexcept { const bool removed = geometrySwarms_.erase(entityId) != 0U; if (removed) InvalidateDrawGroupsOfNonMeshProxy(); return removed; }
bool RenderScene::RemoveSurfaceCast(std::uint64_t entityId) noexcept { const bool removed = surfaceCasts_.erase(entityId) != 0U; if (removed) InvalidateDrawGroupsOfNonMeshProxy(); return removed; }
bool RenderScene::RemoveSpaceStroke(std::uint64_t entityId) noexcept { const bool removed = spaceStrokes_.erase(entityId) != 0U; if (removed) InvalidateDrawGroupsOfNonMeshProxy(); return removed; }

const MeshRenderProxy* RenderScene::FindMeshByEntity(std::uint64_t entityId) const noexcept {
    if (const MeshRenderProxy* proxy = meshById_.Find(entityId)) {
        // What a lookup is for is usually to read or move the proxy: its lines are fetched together, not one after the other.
        PrefetchBytes(proxy, offsetof(MeshRenderProxy, desc), kb::ecs::QueryPrefetchAccess::Write);
        PrefetchBytes(&proxy->desc.model, sizeof(proxy->desc.model), kb::ecs::QueryPrefetchAccess::Write);
        kb::ecs::PrefetchQueryMemory(&proxy->desc.visible);
        return proxy;
    }
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
        if (!RemoveMeshInstance(it->second)) InvalidateDrawGroups();
        ForgetQueuedMove(it->second);
        meshById_.Remove(it->first);
        it = meshes_.erase(it);
        meshSetVersion_ = NextContentRevision();
        ++removed;
    }
    if (removed != 0U) {
        sortedMeshProxies_.dirty = true;
        meshContentRevision_ = NextContentRevision();
        NoteNonMeshShadowCasterChange();
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
    if (removed != 0U) lightContentRevision_ = NextContentRevision();
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
    if (removed != 0U) InvalidateDrawGroupsOfNonMeshProxy();
    return removed;
}
std::uint32_t RenderScene::RemoveSurfaceCastsNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept {
    std::uint32_t removed = 0U;
    for (auto it = surfaceCasts_.begin(); it != surfaceCasts_.end();) {
        if (std::ranges::binary_search(sortedEntityIds, it->first)) { ++it; continue; }
        it = surfaceCasts_.erase(it); ++removed;
    }
    if (removed != 0U) InvalidateDrawGroupsOfNonMeshProxy();
    return removed;
}
std::uint32_t RenderScene::RemoveSpaceStrokesNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept {
    std::uint32_t removed = 0U;
    for (auto it = spaceStrokes_.begin(); it != spaceStrokes_.end();) {
        if (std::ranges::binary_search(sortedEntityIds, it->first)) { ++it; continue; }
        it = spaceStrokes_.erase(it); ++removed;
    }
    if (removed != 0U) InvalidateDrawGroupsOfNonMeshProxy();
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

std::span<const MeshRenderProxy* const> RenderScene::SortedMeshProxies() const {
    if (sortedMeshProxies_.dirty) {
        sortedMeshProxies_.proxies.clear();
        sortedMeshProxies_.proxies.reserve(meshes_.size());
        for (const auto& [entityId, proxy] : meshes_) {
            sortedMeshProxies_.proxies.push_back(&proxy);
            static_cast<void>(entityId);
        }
        std::sort(sortedMeshProxies_.proxies.begin(), sortedMeshProxies_.proxies.end(),
            [](const MeshRenderProxy* lhs, const MeshRenderProxy* rhs) noexcept {
                return lhs->desc.entityId < rhs->desc.entityId;
            });
        sortedMeshProxies_.dirty = false;
    }
    return sortedMeshProxies_.proxies;
}

const RenderScene::CameraProxyMap& RenderScene::CameraProxies() const noexcept {
    return cameras_;
}

const RenderScene::LightProxyMap& RenderScene::LightProxies() const noexcept {
    return lights_;
}
const RenderScene::VisibilityBlockerProxyMap& RenderScene::VisibilityBlockerProxies() const noexcept { return visibilityBlockers_; }

kb::math::DVec3 RenderScene::RenderOriginFor(const kb::math::DVec3& eye, const RenderOriginPolicy& policy) const noexcept {
    const bool finite = std::isfinite(eye.x) && std::isfinite(eye.y) && std::isfinite(eye.z);
    if (!finite || (std::abs(eye.x - renderOrigin_.x) <= policy.rebaseDistance && std::abs(eye.y - renderOrigin_.y) <= policy.rebaseDistance &&
        std::abs(eye.z - renderOrigin_.z) <= policy.rebaseDistance)) {
        return renderOrigin_;
    }
    const double step = policy.gridStep > 0.0 ? policy.gridStep : 1.0;
    return kb::math::DVec3{ std::round(eye.x / step) * step, std::round(eye.y / step) * step, std::round(eye.z / step) * step };
}

void RenderScene::BeginMotionFrame(std::uint64_t rendererFrame) noexcept {
    if (rendererFrame == motionRendererFrame_) {
        return;
    }
    motionRendererFrame_ = rendererFrame;
    ++motionFrame_;
}

bool RenderScene::SetRenderOrigin(const kb::math::DVec3& origin) {
    if (origin == renderOrigin_) return false;
    const kb::math::DVec3 previous = renderOrigin_;
    renderOrigin_ = origin;
    ++renderOriginRevision_;
    // No pulled transform matches a world version of UINT64_MAX: the next pull re-derives every proxy.
    constexpr std::uint64_t kStale = UINT64_MAX;
    for (auto& [entityId, proxy] : meshes_) proxy.pulledWorldVersion = kStale;
    for (auto& [entityId, proxy] : cameras_) proxy.pulledWorldVersion = kStale;
    for (auto& [entityId, proxy] : lights_) proxy.pulledWorldVersion = kStale;
    for (auto& [entityId, proxy] : visibilityBlockers_) proxy.pulledWorldVersion = kStale;
    for (auto& [entityId, proxy] : geometrySwarms_) proxy.pulledWorldVersion = kStale;
    for (auto& [entityId, proxy] : surfaceCasts_) proxy.pulledWorldVersion = kStale;
    for (auto& [entityId, proxy] : spaceStrokes_) proxy.pulledWorldVersion = kStale;
    lightContentRevision_ = NextContentRevision();
    InvalidateDrawGroupsOfNonMeshProxy();
    for (const auto& [listenerId, listener] : renderOriginListeners_) {
        static_cast<void>(listenerId);
        if (listener) listener(previous, renderOrigin_);
    }
    return true;
}

std::uint64_t RenderScene::AddRenderOriginListener(RenderOriginListener listener) {
    const std::uint64_t listenerId = nextRenderOriginListenerId_++;
    renderOriginListeners_.emplace_back(listenerId, std::move(listener));
    return listenerId;
}

void RenderScene::RemoveRenderOriginListener(std::uint64_t listenerId) noexcept {
    std::erase_if(renderOriginListeners_, [listenerId](const auto& entry) { return entry.first == listenerId; });
}

void RenderScene::SetPortalVisibility(std::optional<kb::scene::ScenePortalVisibility> visibility) {
    portalVisibility_ = std::move(visibility);
}

const kb::scene::ScenePortalVisibility* RenderScene::PortalVisibility() const noexcept {
    return portalVisibility_.has_value() && portalVisibility_->Active() ? &*portalVisibility_ : nullptr;
}
const RenderScene::GeometrySwarmProxyMap& RenderScene::GeometrySwarmProxies() const noexcept { return geometrySwarms_; }
const RenderScene::SurfaceCastProxyMap& RenderScene::SurfaceCastProxies() const noexcept { return surfaceCasts_; }
const RenderScene::SpaceStrokeProxyMap& RenderScene::SpaceStrokeProxies() const noexcept { return spaceStrokes_; }

void RenderScene::SetParticleRenderSnapshot(
    std::shared_ptr<const kb::particles::ParticleRenderSnapshot> snapshot) noexcept {
    particleRenderSnapshot_ = std::move(snapshot);
}

const std::shared_ptr<const kb::particles::ParticleRenderSnapshot>&
RenderScene::ParticleRenderSnapshot() const noexcept {
    return particleRenderSnapshot_;
}

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
    PrepareDrawGroups(nullptr);
    return drawGroups_;
}

void RenderScene::PrepareDrawGroups(kb::ecs::WorkerPool* workers) const {
    ApplyQueuedMoves(workers);
    RebuildDrawGroupsIfNeeded(workers);
    RefreshVisibility(workers);
}

RenderScene::ResourceGroupCoverage RenderScene::ResourceGroupsCoverMeshProxies() const {
    // Geometry swarms, space strokes and terrain foliage add generated instances that do not correspond
    // one-to-one with mesh proxies, so their presence makes the coverage count ambiguous.
    if (!geometrySwarms_.empty() || !spaceStrokes_.empty() || !terrainFoliage_.empty()) {
        return {};
    }
    std::size_t represented = 0U;
    bool hasMorphDeformation = false;
    bool hasMaterialSlotOverrides = false;
    for (const SceneRenderDrawGroup& group : DrawGroups()) {
        represented += group.instances.size();
        hasMorphDeformation = hasMorphDeformation || group.hasMorphDeformation;
        hasMaterialSlotOverrides = hasMaterialSlotOverrides || group.hasMaterialSlotOverrides;
    }
    if (represented != meshes_.size()) {
        return {};
    }
    return ResourceGroupCoverage{
        .meshes = !hasMorphDeformation,
        .materials = !hasMaterialSlotOverrides && surfaceCasts_.empty(),
    };
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
        outSnapshot.meshes.push_back(BuildMeshInstance(proxy));
    }
    for (const auto& [entityId, proxy] : geometrySwarms_) {
        const GeometrySwarmRenderProxyDesc& swarm = proxy.desc;
        if (!swarm.visible || swarm.meshAssetId == 0U || swarm.instanceCount == 0U || swarm.columns == 0U || swarm.rows == 0U || swarm.layers == 0U) {
            continue;
        }
        for (std::uint32_t index = 0U; index < swarm.instanceCount; ++index) {
            outSnapshot.meshes.push_back(BuildGeometrySwarmInstance(entityId, proxy, index));
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

SceneRenderMeshInstance RenderScene::BuildMeshInstance(const MeshRenderProxy& proxy) const {
    SceneRenderMeshInstance instance = RenderSceneMeshInstanceBuilder::Build(proxy.desc);
    instance.previousModel = proxy.previousModel;
    instance.previousModelFrame = proxy.previousModelFrame;
    ApplySurfaceCasts(instance);
    return instance;
}

SceneRenderMeshInstance RenderScene::BuildGeometrySwarmInstance(
    std::uint64_t entityId, const GeometrySwarmRenderProxy& proxy, std::uint32_t index) const {
    const GeometrySwarmRenderProxyDesc& swarm = proxy.desc;
    SceneRenderMeshInstance instance{
        .entityId = GeometrySwarmInstanceId(entityId, index),
        .meshAssetId = swarm.meshAssetId,
        .materialAssetId = swarm.materialAssetId,
        .model = GeometrySwarmModel(swarm, index),
        .castsShadow = swarm.castsShadow,
        .receivesShadow = swarm.receivesShadow,
        .layer = swarm.layer,
    };
    if (proxy.previousModelFrame != 0U) {
        // A generated instance moves with its swarm: its previous model is the same layout placed by the
        // previous model of the swarm.
        GeometrySwarmRenderProxyDesc previous = swarm;
        previous.model = proxy.previousModel;
        instance.previousModel = GeometrySwarmModel(previous, index);
        instance.previousModelFrame = proxy.previousModelFrame;
    }
    ApplySurfaceCasts(instance);
    return instance;
}

void RenderScene::AppendMeshInstance(MeshRenderProxy& proxy) {
    const SceneRenderMeshInstance instance = BuildMeshInstance(proxy);
    const DrawGroupKey key{ .meshAssetId = instance.meshAssetId, .materialAssetId = instance.materialAssetId, .owner = proxy.desc.entityId / 1024U, .kind = 0U };
    auto [groupIt, newGroup] = drawGroupLookupScratch_.try_emplace(key, drawGroups_.size());
    if (newGroup) {
        drawGroups_.push_back(SceneRenderDrawGroup{
            .meshAssetId = instance.meshAssetId,
            .materialAssetId = instance.materialAssetId,
        });
    }
    SceneRenderDrawGroup& group = drawGroups_[groupIt->second];
    if (newGroup) {
        group.cacheId = NextContentRevision();
        group.partitionOwner = key.owner;
        group.partitionKind = key.kind;
    }
    group.contentRevision = NextContentRevision();
    proxy.instanceGroupIndex = static_cast<std::uint32_t>(groupIt->second);
    proxy.instanceIndexInGroup = static_cast<std::uint32_t>(group.instances.size());
    proxy.instanceLocationVersion = drawGroupBuildVersion_;
    group.hasMaterialSlotOverrides = group.hasMaterialSlotOverrides || instance.materialSlotOverrideCount != 0U;
    group.hasMorphDeformation = group.hasMorphDeformation || proxy.desc.morphDeformationEnabled;
    group.instances.push_back(instance);
    SceneMeshVisibilityIndexBuilder::Append(group.instances, group.visibility);
    visibilityStale_ = 1U;
}

bool RenderScene::RemoveMeshInstance(const MeshRenderProxy& proxy) noexcept {
    if (drawGroupsDirty_) return false;
    if (!proxy.desc.visible) return true;
    if (proxy.instanceLocationVersion != drawGroupBuildVersion_ || proxy.instanceGroupIndex >= drawGroups_.size()) return false;
    const std::size_t groupIndex = proxy.instanceGroupIndex;
    SceneRenderDrawGroup& group = drawGroups_[groupIndex];
    const std::size_t instanceIndex = proxy.instanceIndexInGroup;
    if (instanceIndex >= group.instances.size() || group.instances[instanceIndex].entityId != proxy.desc.entityId) return false;
    SceneMeshVisibilityIndexBuilder::Remove(group.visibility, static_cast<std::uint32_t>(instanceIndex));
    visibilityStale_ = 1U;
    if (instanceIndex + 1U != group.instances.size()) {
        group.instances[instanceIndex] = std::move(group.instances.back());
        meshes_.at(group.instances[instanceIndex].entityId).instanceIndexInGroup = static_cast<std::uint32_t>(instanceIndex);
    }
    group.instances.pop_back();
    group.contentRevision = NextContentRevision();
    if (!group.instances.empty()) {
        if (proxy.desc.materialSlotOverrideCount != 0U || proxy.desc.morphDeformationEnabled) {
            group.hasMaterialSlotOverrides = false; group.hasMorphDeformation = false;
            for (const auto& instance : group.instances) {
                group.hasMaterialSlotOverrides |= instance.materialSlotOverrideCount != 0U;
                group.hasMorphDeformation |= meshes_.at(instance.entityId).desc.morphDeformationEnabled;
            }
        }
        return true;
    }
    drawGroupLookupScratch_.erase(DrawGroupKey{group.meshAssetId, group.materialAssetId, group.partitionOwner, group.partitionKind});
    if (groupIndex + 1U != drawGroups_.size()) {
        group = std::move(drawGroups_.back());
        drawGroupLookupScratch_.at(DrawGroupKey{group.meshAssetId, group.materialAssetId, group.partitionOwner, group.partitionKind}) = groupIndex;
        if (group.partitionKind == 0U) {
            for (const auto& instance : group.instances) meshes_.at(instance.entityId).instanceGroupIndex = static_cast<std::uint32_t>(groupIndex);
        }
    }
    drawGroups_.pop_back();
    return true;
}

void RenderScene::InvalidateDrawGroupsOfNonMeshProxy() noexcept {
    NoteNonMeshShadowCasterChange();
    InvalidateDrawGroups();
}

void RenderScene::InvalidateDrawGroups() noexcept {
    meshContentRevision_ = NextContentRevision();
    drawGroupsDirty_ = true;
}

std::uint64_t RenderScene::NextContentRevision() noexcept {
    static std::atomic<std::uint64_t> next{1U};
    return next.fetch_add(1U, std::memory_order_relaxed);
}

void RenderScene::RebuildDrawGroupsIfNeeded(kb::ecs::WorkerPool* workers) const {
    if (!drawGroupsDirty_) {
        return;
    }

    for (SceneRenderDrawGroup& group : drawGroups_) {
        group.instances.clear();
        group.sharedInstances.reset();
        group.visibilityClusters.clear();
        group.fadeStartDistance = 0.0F;
        group.cullDistance = 0.0F;
        group.instancesMoved = 0U;
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

        const SceneRenderMeshInstance instance = BuildMeshInstance(proxy);
        const DrawGroupKey key{ .meshAssetId = instance.meshAssetId, .materialAssetId = instance.materialAssetId, .owner = entityId / 1024U, .kind = 0U };
        auto lookupIt = drawGroupLookupScratch_.find(key);
        if (lookupIt == drawGroupLookupScratch_.end()) {
            if (writeGroupCount == drawGroups_.size()) {
                drawGroups_.push_back(SceneRenderDrawGroup{});
            }
            SceneRenderDrawGroup& group = drawGroups_[writeGroupCount];
            group.cacheId = NextContentRevision();
            group.contentRevision = group.cacheId;
            group.partitionOwner = key.owner; group.partitionKind = key.kind;
            group.meshAssetId = instance.meshAssetId;
            group.materialAssetId = instance.materialAssetId;
            group.hasMaterialSlotOverrides = false;
            group.hasMorphDeformation = false;
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
        group.hasMaterialSlotOverrides = group.hasMaterialSlotOverrides || instance.materialSlotOverrideCount != 0U;
        group.hasMorphDeformation = group.hasMorphDeformation || mesh.morphDeformationEnabled;
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
        std::size_t firstGenerated = 0U, generatedGroup = 0U;
        for (std::uint32_t index = 0U; index < swarm.instanceCount; ++index) {
            const SceneRenderMeshInstance instance = BuildGeometrySwarmInstance(entityId, proxy, index);
            const DrawGroupKey key{ .meshAssetId = instance.meshAssetId, .materialAssetId = instance.materialAssetId, .owner = entityId, .kind = 1U };
            auto lookupIt = drawGroupLookupScratch_.find(key);
            if (lookupIt == drawGroupLookupScratch_.end()) {
                if (writeGroupCount == drawGroups_.size()) drawGroups_.push_back(SceneRenderDrawGroup{});
                SceneRenderDrawGroup& group = drawGroups_[writeGroupCount];
                group.cacheId = NextContentRevision(); group.contentRevision = group.cacheId;
                group.partitionOwner = key.owner; group.partitionKind = key.kind;
                group.meshAssetId = instance.meshAssetId; group.materialAssetId = instance.materialAssetId; group.hasMaterialSlotOverrides = false; group.hasMorphDeformation = false;
                lookupIt = drawGroupLookupScratch_.emplace(key, writeGroupCount).first; ++writeGroupCount;
            }
            if (index == 0U) { generatedGroup = lookupIt->second; firstGenerated = drawGroups_[generatedGroup].instances.size(); }
            drawGroups_[lookupIt->second].instances.push_back(instance);
        }
        if (surfaceCasts_.empty()) GeometrySwarmVisibilityClusters::Append(drawGroups_[generatedGroup], firstGenerated);
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
            const DrawGroupKey key{ .meshAssetId = instance.meshAssetId, .materialAssetId = instance.materialAssetId, .owner = entityId, .kind = 2U };
            auto lookupIt = drawGroupLookupScratch_.find(key);
            if (lookupIt == drawGroupLookupScratch_.end()) {
                if (writeGroupCount == drawGroups_.size()) drawGroups_.push_back(SceneRenderDrawGroup{});
                SceneRenderDrawGroup& group = drawGroups_[writeGroupCount];
                group.cacheId = NextContentRevision(); group.contentRevision = group.cacheId;
                group.partitionOwner = key.owner; group.partitionKind = key.kind;
                group.meshAssetId = instance.meshAssetId; group.materialAssetId = instance.materialAssetId; group.hasMaterialSlotOverrides = false; group.hasMorphDeformation = false;
                lookupIt = drawGroupLookupScratch_.emplace(key, writeGroupCount).first; ++writeGroupCount;
            }
            drawGroups_[lookupIt->second].instances.push_back(instance);
        }
    }

    AppendTerrainFoliageGroups(writeGroupCount);

    drawGroups_.resize(writeGroupCount);
    const auto index = [this](std::size_t begin, std::size_t end) {
        for (std::size_t groupIndex = begin; groupIndex < end; ++groupIndex) {
            SceneRenderDrawGroup& group = drawGroups_[groupIndex];
            if (group.partitionKind == 0U) SceneMeshVisibilityIndexBuilder::Build(group.instances, group.visibility);
            else group.visibility = {};
        }
    };
    if (workers != nullptr && workers->Running() && drawGroups_.size() >= kMinGroupsOnWorkers) {
        workers->ParallelForChunks(drawGroups_.size(), kGroupsPerChunk,
            [&index](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) { index(chunk.begin, chunk.begin + chunk.count); });
    } else {
        index(0U, drawGroups_.size());
    }
    visibilityStale_ = 0U;
    drawGroupsDirty_ = false;
}

void RenderScene::RefreshVisibility(kb::ecs::WorkerPool* workers) const {
    if (visibilityStale_ == 0U) {
        return;
    }
    visibilityStale_ = 0U;
    const auto refresh = [this](std::size_t begin, std::size_t end) {
        for (std::size_t index = begin; index < end; ++index) {
            SceneRenderDrawGroup& group = drawGroups_[index];
            if (group.instancesMoved != 0U) {
                group.instancesMoved = 0U;
                group.contentRevision = NextContentRevision();
            }
            SceneMeshVisibilityIndexBuilder::Refresh(group.instances, group.visibility);
        }
    };
    // The groups are independent: each refresh reads its own instances and writes its own index.
    if (workers != nullptr && workers->Running() && drawGroups_.size() >= kMinGroupsOnWorkers) {
        workers->ParallelForChunks(drawGroups_.size(), kGroupsPerChunk,
            [&refresh](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) { refresh(chunk.begin, chunk.begin + chunk.count); });
    } else {
        refresh(0U, drawGroups_.size());
    }
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
    const MeshRenderProxy* proxy = FindMeshByEntity(entityId);
    if (proxy == nullptr) {
        return TransformUpdateOutcome::NotFound;
    }
    return ApplyMeshTransform(const_cast<MeshRenderProxy&>(*proxy), model, InstanceRefresh::Immediate);
}

void RenderScene::RecordPreviousModel(MeshRenderProxy& proxy, const std::array<float, 16>& model) const noexcept {
    // A proxy created in this motion frame has no previous frame: it draws without motion of its own.
    // Later changes in the same frame keep the model the frame began with.
    if (proxy.spawnMotionFrame == motionFrame_ || proxy.previousModelFrame == motionFrame_) {
        return;
    }
    proxy.previousModel = model;
    proxy.previousModelFrame = motionFrame_;
}

void RenderScene::RecordPreviousSwarmModel(GeometrySwarmRenderProxy& proxy, const std::array<float, 16>& model) const noexcept {
    if (proxy.spawnMotionFrame == motionFrame_ || proxy.previousModelFrame == motionFrame_) {
        return;
    }
    proxy.previousModel = model;
    proxy.previousModelFrame = motionFrame_;
}

RenderScene::TransformUpdateOutcome RenderScene::ApplyMeshTransform(MeshRenderProxy& proxy, const std::array<float, 16>& worldModel, InstanceRefresh refresh) {
    auto model = worldModel;
    for (std::size_t component = 0; component < 3; ++component) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            model[12 + component] += worldModel[axis * 4 + component] * proxy.desc.localRenderOffset[axis];
        }
    }
    if (ModelsDiffer(proxy.desc.model, model)) {
        RecordPreviousModel(proxy, proxy.desc.model);
        if (CastsMeshShadow(proxy.desc)) proxy.shadowChangeFrame = motionFrame_;
    }
    proxy.desc.model = model; // source of truth, always current
    proxy.dirty |= RenderProxyDirtyFlag::Transform;

    // Fast path: clean cache + a location stamped by the current build. The proxy is all this reads: the instance is
    // refreshed from it now, or, queued, by the next PrepareDrawGroups. No invalidation, no telemetry.
    if (drawGroupsDirty_ || !proxy.desc.visible || proxy.instanceLocationVersion != drawGroupBuildVersion_ ||
        proxy.instanceGroupIndex >= drawGroups_.size()) {
        return TransformUpdateOutcome::Fallback;
    }
    if (refresh == InstanceRefresh::Immediate) {
        return RefreshMovedInstance(proxy) ? TransformUpdateOutcome::InPlace : TransformUpdateOutcome::Fallback;
    }
    if (proxy.moveQueue.index == UINT32_MAX) {
        proxy.moveQueue.index = static_cast<std::uint32_t>(queuedMoves_.entries.size());
        queuedMoves_.entries.push_back(&proxy);
    }
    return TransformUpdateOutcome::InPlace;
}

bool RenderScene::RefreshMovedInstance(const MeshRenderProxy& proxy) const noexcept {
    // A proxy that is not visible has no instance.
    if (!proxy.desc.visible) return true;
    if (proxy.instanceLocationVersion != drawGroupBuildVersion_ || proxy.instanceGroupIndex >= drawGroups_.size()) return false;
    SceneRenderDrawGroup& group = drawGroups_[proxy.instanceGroupIndex];
    const std::size_t position = proxy.instanceIndexInGroup;
    if (position >= group.instances.size() || group.instances[position].entityId != proxy.desc.entityId) return false;
    SceneRenderMeshInstance& instance = group.instances[position];
    instance.model = proxy.desc.model;
    instance.previousModel = proxy.previousModel;
    instance.previousModelFrame = proxy.previousModelFrame;
    SceneMeshVisibilityIndexBuilder::MarkMoved(group.visibility, static_cast<std::uint32_t>(position));
    // Workers refresh disjoint instances of one group, and every instance that moves would write the flags: only the first does.
    std::atomic_ref<std::uint8_t> groupMoved(group.instancesMoved);
    if (groupMoved.load(std::memory_order_relaxed) == 0U) groupMoved.store(1U, std::memory_order_relaxed);
    std::atomic_ref<std::uint8_t> stale(visibilityStale_);
    if (stale.load(std::memory_order_relaxed) == 0U) stale.store(1U, std::memory_order_relaxed);
    return true;
}

void RenderScene::ForgetQueuedMove(const MeshRenderProxy& proxy) noexcept {
    if (proxy.moveQueue.index == UINT32_MAX) return;
    queuedMoves_.entries[proxy.moveQueue.index] = nullptr;
    proxy.moveQueue.index = UINT32_MAX;
}

void RenderScene::ApplyQueuedMoves(kb::ecs::WorkerPool* workers) const {
    if (queuedMoves_.copiedWithMoves) {
        queuedMoves_.copiedWithMoves = false;
        drawGroupsDirty_ = true;
    }
    std::vector<MeshRenderProxy*>& queue = queuedMoves_.entries;
    if (queue.empty()) {
        return;
    }
    // Draw groups that are built again take every proxy as it is; their old instances need nothing.
    std::atomic<bool> misplaced{ drawGroupsDirty_ };
    if (!drawGroupsDirty_) {
        const auto refresh = [&](std::size_t begin, std::size_t end) {
            bool failed = false;
            for (std::size_t index = begin; index < end; ++index) {
                // The proxy, then its draw group, then its instance are fetched while the moves before this one are refreshed.
                if (index + kProxyAhead < end) {
                    if (const MeshRenderProxy* ahead = queue[index + kProxyAhead]) {
                        PrefetchBytes(ahead, offsetof(MeshRenderProxy, desc), kb::ecs::QueryPrefetchAccess::Write);
                        PrefetchBytes(&ahead->desc.model, sizeof(ahead->desc.model));
                    }
                }
                if (index + kGroupAhead < end) {
                    if (const MeshRenderProxy* ahead = queue[index + kGroupAhead]; ahead != nullptr && ahead->instanceGroupIndex < drawGroups_.size()) {
                        PrefetchBytes(&drawGroups_[ahead->instanceGroupIndex], 64U);
                    }
                }
                if (index + kInstanceAhead < end) {
                    if (const MeshRenderProxy* ahead = queue[index + kInstanceAhead]; ahead != nullptr && ahead->instanceGroupIndex < drawGroups_.size()) {
                        const SceneRenderDrawGroup& group = drawGroups_[ahead->instanceGroupIndex];
                        if (ahead->instanceIndexInGroup < group.instances.size()) {
                            const SceneRenderMeshInstance& instance = group.instances[ahead->instanceIndexInGroup];
                            PrefetchBytes(&instance, offsetof(SceneRenderMeshInstance, boundsOverride), kb::ecs::QueryPrefetchAccess::Write);
                            PrefetchBytes(&instance.previousModelFrame, sizeof(instance.previousModelFrame) + sizeof(instance.previousModel), kb::ecs::QueryPrefetchAccess::Write);
                        }
                    }
                }
                if (const MeshRenderProxy* proxy = queue[index]; proxy != nullptr && !RefreshMovedInstance(*proxy)) failed = true;
            }
            if (failed) misplaced.store(true, std::memory_order_relaxed);
        };
        if (workers != nullptr && workers->Running() && queue.size() >= kMinMovesOnWorkers) {
            workers->ParallelForChunks(queue.size(), kMovesPerChunk,
                [&refresh](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) { refresh(chunk.begin, chunk.begin + chunk.count); });
        } else {
            refresh(0U, queue.size());
        }
    }
    for (MeshRenderProxy* proxy : queue) {
        if (proxy != nullptr) proxy->moveQueue.index = UINT32_MAX;
    }
    queue.clear();
    drawGroupsDirty_ = misplaced.load(std::memory_order_relaxed);
}

void RenderScene::InvalidateDrawGroupsIfFallback(TransformUpdateOutcome outcome) noexcept {
    if (outcome == TransformUpdateOutcome::Fallback) {
        InvalidateDrawGroups();
    }
}

void RenderScene::AddTransformUpdateCounts(std::uint64_t inPlace, std::uint64_t fallback) noexcept {
    if (inPlace != 0U || fallback != 0U) {
        meshContentMoved_ = true;
        if (!surfaceCasts_.empty()) drawGroupsDirty_ = true;
    }
    transformInPlaceUpdateCount_ += inPlace;
    transformFallbackUpdateCount_ += fallback;
}

bool RenderScene::UpdateMeshTransform(std::uint64_t entityId, const std::array<float, 16>& model) {
    const MeshRenderProxy* proxy = FindMeshByEntity(entityId);
    if (proxy == nullptr) {
        return false;
    }
    UpdateMeshTransform(entityId, *proxy, model);
    return true;
}

void RenderScene::UpdateMeshTransform(std::uint64_t, const MeshRenderProxy& proxy, const std::array<float, 16>& model) {
    // The proxy is one of meshes_, found through the const lookup.
    const TransformUpdateOutcome outcome = ApplyMeshTransform(const_cast<MeshRenderProxy&>(proxy), model, InstanceRefresh::Queued);
    switch (outcome) {
    case TransformUpdateOutcome::NotFound:
        return;
    case TransformUpdateOutcome::InPlace:
        meshContentMoved_ = true;
        if (!surfaceCasts_.empty()) drawGroupsDirty_ = true;
        ++transformInPlaceUpdateCount_;
        return;
    case TransformUpdateOutcome::Fallback:
        ++transformFallbackUpdateCount_;
        InvalidateDrawGroups();
        return;
    }
}

} // namespace kb::render
