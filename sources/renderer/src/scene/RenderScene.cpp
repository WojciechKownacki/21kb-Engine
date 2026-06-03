#include "kb/render/scene/RenderScene.hpp"

#include "kb/render/SceneDepthPolicy.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] float Aspect(std::uint32_t width, std::uint32_t height) noexcept {
    return height == 0U ? 1.0F : static_cast<float>(std::max(1U, width)) / static_cast<float>(height);
}

[[nodiscard]] float DegreesToRadians(float degrees) noexcept {
    return degrees * 0.017453292519943295769F;
}

struct Basis {
    float xx = 1.0F;
    float xy = 0.0F;
    float xz = 0.0F;
    float yx = 0.0F;
    float yy = 1.0F;
    float yz = 0.0F;
    float zx = 0.0F;
    float zy = 0.0F;
    float zz = 1.0F;
};

[[nodiscard]] Basis BasisFromQuat(const std::array<float, 4>& q) noexcept {
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    const float x2 = x + x;
    const float y2 = y + y;
    const float z2 = z + z;
    const float xx = x * x2;
    const float xy = x * y2;
    const float xz = x * z2;
    const float yy = y * y2;
    const float yz = y * z2;
    const float zz = z * z2;
    const float wx = w * x2;
    const float wy = w * y2;
    const float wz = w * z2;

    return Basis{
        .xx = 1.0F - (yy + zz),
        .xy = xy + wz,
        .xz = xz - wy,
        .yx = xy - wz,
        .yy = 1.0F - (xx + zz),
        .yz = yz + wx,
        .zx = xz + wy,
        .zy = yz - wx,
        .zz = 1.0F - (xx + yy),
    };
}

[[nodiscard]] SceneRenderCamera BuildCamera(const CameraRenderProxyDesc& camera, std::uint32_t viewportWidth, std::uint32_t viewportHeight) {
    const Basis basis = BasisFromQuat(camera.rotation);
    const bx::Vec3 eye{ camera.position[0], camera.position[1], camera.position[2] };
    const bx::Vec3 at{
        camera.position[0] + basis.zx,
        camera.position[1] + basis.zy,
        camera.position[2] + basis.zz,
    };
    const bx::Vec3 up{ basis.yx, basis.yy, basis.yz };

    SceneRenderCamera renderCamera{};
    bx::mtxLookAt(renderCamera.view.data(), eye, at, up);
    const bool homogeneousDepth = SceneDepthPolicy::HomogeneousDepth();
    switch (camera.projection) {
    case RenderCameraProjection::Perspective:
        SceneDepthPolicy::MakePerspective(
            renderCamera.projection.data(),
            camera.verticalFovDegrees,
            Aspect(viewportWidth, viewportHeight),
            camera.nearClip,
            camera.farClip,
            homogeneousDepth);
        break;
    case RenderCameraProjection::Orthographic:
        SceneDepthPolicy::MakeOrthographic(
            renderCamera.projection.data(),
            camera.orthographicHeight,
            Aspect(viewportWidth, viewportHeight),
            camera.nearClip,
            camera.farClip,
            homogeneousDepth);
        break;
    }
    return renderCamera;
}

[[nodiscard]] RenderProxyDirtyFlag DirtyForMeshChange(const MeshRenderProxyDesc& current, const MeshRenderProxyDesc& next) noexcept {
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
    if (current.model != next.model) {
        dirty |= RenderProxyDirtyFlag::Transform;
    }
    if (current.meshAssetId != next.meshAssetId || current.castsShadow != next.castsShadow || current.receivesShadow != next.receivesShadow) {
        dirty |= RenderProxyDirtyFlag::Mesh;
    }
    if (current.materialAssetId != next.materialAssetId ||
        current.materialSlotAssetIds != next.materialSlotAssetIds ||
        current.materialSlotOverrideCount != next.materialSlotOverrideCount ||
        current.color != next.color) {
        dirty |= RenderProxyDirtyFlag::Material;
    }
    if (current.visible != next.visible) {
        dirty |= RenderProxyDirtyFlag::Visibility;
    }
    return dirty;
}

[[nodiscard]] RenderProxyDirtyFlag DirtyForCameraChange(const CameraRenderProxyDesc& current, const CameraRenderProxyDesc& next) noexcept {
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
    if (current.position != next.position || current.rotation != next.rotation) {
        dirty |= RenderProxyDirtyFlag::Transform;
    }
    if (current.projection != next.projection ||
        current.verticalFovDegrees != next.verticalFovDegrees ||
        current.orthographicHeight != next.orthographicHeight ||
        current.nearClip != next.nearClip ||
        current.farClip != next.farClip ||
        current.primary != next.primary) {
        dirty |= RenderProxyDirtyFlag::Camera;
    }
    if (current.visible != next.visible) {
        dirty |= RenderProxyDirtyFlag::Visibility;
    }
    return dirty;
}

[[nodiscard]] RenderProxyDirtyFlag DirtyForLightChange(const LightRenderProxyDesc& current, const LightRenderProxyDesc& next) noexcept {
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
    if (current.position != next.position || current.rotation != next.rotation) {
        dirty |= RenderProxyDirtyFlag::Transform;
    }
    if (current.kind != next.kind ||
        current.color != next.color ||
        current.intensity != next.intensity ||
        current.range != next.range ||
        current.innerConeDegrees != next.innerConeDegrees ||
        current.outerConeDegrees != next.outerConeDegrees ||
        current.areaWidth != next.areaWidth ||
        current.areaHeight != next.areaHeight ||
        current.contactShadowLength != next.contactShadowLength ||
        current.volumetricScattering != next.volumetricScattering ||
        current.castsShadow != next.castsShadow) {
        dirty |= RenderProxyDirtyFlag::Light;
    }
    if (current.visible != next.visible) {
        dirty |= RenderProxyDirtyFlag::Visibility;
    }
    return dirty;
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
    if (desc.drawGroupKeys > 0U) {
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
    };
}

RenderProxyId RenderScene::UpsertMesh(const MeshRenderProxyDesc& desc) {
    auto [it, inserted] = meshes_.try_emplace(desc.entityId);
    MeshRenderProxy& proxy = it->second;
    if (inserted) {
        proxy.id = AllocateProxyId();
        proxy.desc = desc;
        proxy.dirty = RenderProxyDirtyFlag::All;
        return proxy.id;
    }

    const RenderProxyDirtyFlag dirty = DirtyForMeshChange(proxy.desc, desc);
    if (dirty != RenderProxyDirtyFlag::None) {
        proxy.desc = desc;
        proxy.dirty |= dirty;
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

    const RenderProxyDirtyFlag dirty = DirtyForCameraChange(proxy.desc, desc);
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

    const RenderProxyDirtyFlag dirty = DirtyForLightChange(proxy.desc, desc);
    if (dirty != RenderProxyDirtyFlag::None) {
        proxy.desc = desc;
        proxy.dirty |= dirty;
    }
    return proxy.id;
}

bool RenderScene::RemoveMesh(std::uint64_t entityId) noexcept {
    return meshes_.erase(entityId) != 0U;
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
            return BuildCamera(camera, viewportWidth, viewportHeight);
        }
        static_cast<void>(entityId);
    }
    return std::nullopt;
}

void RenderScene::BuildDrawGroups(std::vector<SceneRenderDrawGroup>& outDrawGroups) const {
    for (SceneRenderDrawGroup& group : outDrawGroups) {
        group.instances.clear();
    }

    drawGroupLookupScratch_.clear();
    drawGroupLookupScratch_.reserve(meshes_.size());
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
            if (writeGroupCount == outDrawGroups.size()) {
                outDrawGroups.push_back(SceneRenderDrawGroup{});
            }
            SceneRenderDrawGroup& group = outDrawGroups[writeGroupCount];
            group.meshAssetId = mesh.meshAssetId;
            group.materialAssetId = mesh.materialAssetId;
            lookupIt = drawGroupLookupScratch_.emplace(key, writeGroupCount).first;
            ++writeGroupCount;
        }

        SceneRenderDrawGroup& group = outDrawGroups[lookupIt->second];
        group.instances.push_back(SceneRenderMeshInstance{
            .entityId = mesh.entityId,
            .meshAssetId = mesh.meshAssetId,
            .materialAssetId = mesh.materialAssetId,
            .materialSlotAssetIds = mesh.materialSlotAssetIds,
            .materialSlotOverrideCount = mesh.materialSlotOverrideCount,
            .model = mesh.model,
            .color = mesh.color,
            .castsShadow = mesh.castsShadow,
            .receivesShadow = mesh.receivesShadow,
        });
    }

    outDrawGroups.resize(writeGroupCount);
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
        outSnapshot.meshes.push_back(SceneRenderMeshInstance{
            .entityId = mesh.entityId,
            .meshAssetId = mesh.meshAssetId,
            .materialAssetId = mesh.materialAssetId,
            .materialSlotAssetIds = mesh.materialSlotAssetIds,
            .materialSlotOverrideCount = mesh.materialSlotOverrideCount,
            .model = mesh.model,
            .color = mesh.color,
            .castsShadow = mesh.castsShadow,
            .receivesShadow = mesh.receivesShadow,
        });
    }

    outSnapshot.lights.reserve(lights_.size());
    for (const auto& [entityId, proxy] : lights_) {
        const LightRenderProxyDesc& light = proxy.desc;
        if (!light.visible) {
            static_cast<void>(entityId);
            continue;
        }
        const Basis basis = BasisFromQuat(light.rotation);
        outSnapshot.lights.push_back(SceneRenderLight{
            .entityId = light.entityId,
            .kind = light.kind,
            .position = { light.position[0], light.position[1], light.position[2] },
            .direction = { basis.zx, basis.zy, basis.zz },
            .color = { light.color[0], light.color[1], light.color[2] },
            .intensity = light.intensity,
            .range = light.range,
            .innerConeCos = std::cos(DegreesToRadians(light.innerConeDegrees)),
            .outerConeCos = std::cos(DegreesToRadians(light.outerConeDegrees)),
            .areaWidth = light.areaWidth,
            .areaHeight = light.areaHeight,
            .contactShadowLength = light.contactShadowLength,
            .volumetricScattering = light.volumetricScattering,
            .castsShadow = light.castsShadow,
        });
    }
}

RenderProxyId RenderScene::AllocateProxyId() noexcept {
    return RenderProxyId{ nextProxyId_++ };
}

} // namespace kb::render
