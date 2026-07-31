#include "kb/render/scene/RenderScene.hpp"

#include "RenderSceneProxyConverters.hpp"
#include "RenderSceneProxyDirtyTracker.hpp"

#include <algorithm>
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
bool RenderScene::RemoveVisibilityBlocker(std::uint64_t entityId) noexcept { return visibilityBlockers_.erase(entityId) != 0U; }
bool RenderScene::RemoveGeometrySwarm(std::uint64_t entityId) noexcept { const bool removed = geometrySwarms_.erase(entityId) != 0U; if (removed) InvalidateDrawGroups(); return removed; }

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
    outSnapshot.meshes.reserve(meshes_.size() + geometrySwarmInstanceCount);
    for (const auto& [entityId, proxy] : meshes_) {
        const MeshRenderProxyDesc& mesh = proxy.desc;
        if (!mesh.visible) {
            static_cast<void>(entityId);
            continue;
        }
        outSnapshot.meshes.push_back(RenderSceneMeshInstanceBuilder::Build(mesh));
    }
    for (const auto& [entityId, proxy] : geometrySwarms_) {
        const GeometrySwarmRenderProxyDesc& swarm = proxy.desc;
        if (!swarm.visible || swarm.meshAssetId == 0U || swarm.instanceCount == 0U || swarm.columns == 0U || swarm.rows == 0U || swarm.layers == 0U) {
            continue;
        }
        for (std::uint32_t index = 0U; index < swarm.instanceCount; ++index) {
            outSnapshot.meshes.push_back(SceneRenderMeshInstance{
                .entityId = GeometrySwarmInstanceId(entityId, index),
                .meshAssetId = swarm.meshAssetId,
                .materialAssetId = swarm.materialAssetId,
                .model = GeometrySwarmModel(swarm, index),
                .castsShadow = swarm.castsShadow,
                .receivesShadow = swarm.receivesShadow,
                .layer = swarm.layer,
            });
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
    drawGroupLookupScratch_.reserve(meshes_.size() + geometrySwarms_.size());
    // A fresh build version invalidates every previously stamped instance location.
    ++drawGroupBuildVersion_;
    std::size_t writeGroupCount = 0U;
    for (const auto& [entityId, proxy] : meshes_) {
        const MeshRenderProxyDesc& mesh = proxy.desc;
        if (!mesh.visible) {
            static_cast<void>(entityId);
            continue;
        }

        const DrawGroupKey key{
            .meshAssetId = mesh.meshAssetId,
            .materialAssetId = mesh.materialAssetId,
        };
        auto lookupIt = drawGroupLookupScratch_.find(key);
        if (lookupIt == drawGroupLookupScratch_.end()) {
            if (writeGroupCount == drawGroups_.size()) {
                drawGroups_.push_back(SceneRenderDrawGroup{});
            }
            SceneRenderDrawGroup& group = drawGroups_[writeGroupCount];
            group.meshAssetId = mesh.meshAssetId;
            group.materialAssetId = mesh.materialAssetId;
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
        group.instances.push_back(RenderSceneMeshInstanceBuilder::Build(mesh));
    }

    // Geometry swarm data stays compact and canonical in ECS.  Only here, at
    // the existing mesh-batch boundary, it expands into the per-instance stream
    // consumed by GPU culling and indirect submission.
    for (const auto& [entityId, proxy] : geometrySwarms_) {
        const GeometrySwarmRenderProxyDesc& swarm = proxy.desc;
        if (!swarm.visible || swarm.meshAssetId == 0U || swarm.instanceCount == 0U || swarm.columns == 0U || swarm.rows == 0U || swarm.layers == 0U) {
            continue;
        }
        const DrawGroupKey key{ .meshAssetId = swarm.meshAssetId, .materialAssetId = swarm.materialAssetId };
        auto lookupIt = drawGroupLookupScratch_.find(key);
        if (lookupIt == drawGroupLookupScratch_.end()) {
            if (writeGroupCount == drawGroups_.size()) drawGroups_.push_back(SceneRenderDrawGroup{});
            SceneRenderDrawGroup& group = drawGroups_[writeGroupCount];
            group.meshAssetId = swarm.meshAssetId;
            group.materialAssetId = swarm.materialAssetId;
            lookupIt = drawGroupLookupScratch_.emplace(key, writeGroupCount).first;
            ++writeGroupCount;
        }
        SceneRenderDrawGroup& group = drawGroups_[lookupIt->second];
        for (std::uint32_t index = 0U; index < swarm.instanceCount; ++index) {
            group.instances.push_back(SceneRenderMeshInstance{
                .entityId = GeometrySwarmInstanceId(entityId, index),
                .meshAssetId = swarm.meshAssetId,
                .materialAssetId = swarm.materialAssetId,
                .model = GeometrySwarmModel(swarm, index),
                .castsShadow = swarm.castsShadow,
                .receivesShadow = swarm.receivesShadow,
                .layer = swarm.layer,
            });
        }
    }

    drawGroups_.resize(writeGroupCount);
    drawGroupsDirty_ = false;
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
