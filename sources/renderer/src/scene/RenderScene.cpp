#include "kb/render/scene/RenderScene.hpp"

#include "RenderSceneProxyConverters.hpp"
#include "RenderSceneProxyDirtyTracker.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

namespace kb::render {

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
        .meshProxyCapacity = static_cast<std::uint32_t>(meshes_.bucket_count()),
        .cameraProxyCapacity = static_cast<std::uint32_t>(cameras_.bucket_count()),
        .lightProxyCapacity = static_cast<std::uint32_t>(lights_.bucket_count()),
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

std::size_t RenderScene::MeshProxyCount() const noexcept {
    return meshes_.size();
}

std::size_t RenderScene::CameraProxyCount() const noexcept {
    return cameras_.size();
}

std::size_t RenderScene::LightProxyCount() const noexcept {
    return lights_.size();
}

const RenderScene::MeshProxyMap& RenderScene::MeshProxies() const noexcept {
    return meshes_;
}

const RenderScene::CameraProxyMap& RenderScene::CameraProxies() const noexcept {
    return cameras_;
}

const RenderScene::LightProxyMap& RenderScene::LightProxies() const noexcept {
    return lights_;
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
}

std::optional<SceneRenderCamera> RenderScene::BuildPrimaryCamera(std::uint32_t viewportWidth, std::uint32_t viewportHeight) const {
    for (const auto& [entityId, proxy] : cameras_) {
        const CameraRenderProxyDesc& camera = proxy.desc;
        if (camera.visible && camera.primary) {
            return RenderSceneCameraBuilder::Build(camera, viewportWidth, viewportHeight);
        }
        static_cast<void>(entityId);
    }
    return std::nullopt;
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

void RenderScene::BuildSnapshotInto(std::uint32_t viewportWidth, std::uint32_t viewportHeight, SceneRenderSnapshot& outSnapshot) const {
    outSnapshot.camera = BuildPrimaryCamera(viewportWidth, viewportHeight);
    outSnapshot.meshes.clear();
    outSnapshot.lights.clear();

    outSnapshot.meshes.reserve(meshes_.size());
    for (const auto& [entityId, proxy] : meshes_) {
        const MeshRenderProxyDesc& mesh = proxy.desc;
        if (!mesh.visible) {
            static_cast<void>(entityId);
            continue;
        }
        outSnapshot.meshes.push_back(RenderSceneMeshInstanceBuilder::Build(mesh));
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
    drawGroupLookupScratch_.reserve(meshes_.size());
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
