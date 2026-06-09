#pragma once

#include "kb/render/scene/RenderProxyId.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace kb::render {

enum class RenderProxyDirtyFlag : std::uint32_t {
    None = 0U,
    Transform = 1U << 0U,
    Mesh = 1U << 1U,
    Material = 1U << 2U,
    Visibility = 1U << 3U,
    Camera = 1U << 4U,
    Light = 1U << 5U,
    All = (1U << 6U) - 1U,
};

[[nodiscard]] constexpr RenderProxyDirtyFlag operator|(RenderProxyDirtyFlag lhs, RenderProxyDirtyFlag rhs) noexcept {
    return static_cast<RenderProxyDirtyFlag>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr RenderProxyDirtyFlag& operator|=(RenderProxyDirtyFlag& lhs, RenderProxyDirtyFlag rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool HasDirtyFlag(RenderProxyDirtyFlag value, RenderProxyDirtyFlag flag) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0U;
}

enum class RenderCameraProjection : std::uint8_t {
    Perspective,
    Orthographic,
};

struct MeshRenderProxyDesc {
    std::uint64_t entityId = 0;
    std::uint64_t meshAssetId = 0;
    std::uint64_t materialAssetId = 0;
    std::array<std::uint64_t, kMaxSceneMaterialSlotOverrides> materialSlotAssetIds{};
    std::uint32_t materialSlotOverrideCount = 0;
    std::array<float, 16> model{};
    std::array<float, 4> color{ 0.76F, 0.80F, 0.86F, 1.0F };
    bool visible = true;
    bool castsShadow = true;
    bool receivesShadow = true;
};

struct CameraRenderProxyDesc {
    std::uint64_t entityId = 0;
    std::array<float, 3> position{};
    std::array<float, 4> rotation{ 0.0F, 0.0F, 0.0F, 1.0F };
    RenderCameraProjection projection = RenderCameraProjection::Perspective;
    float verticalFovDegrees = 60.0F;
    float orthographicHeight = 10.0F;
    float nearClip = 0.01F;
    float farClip = 1000.0F;
    bool primary = false;
    bool visible = true;
};

struct LightRenderProxyDesc {
    std::uint64_t entityId = 0;
    RenderLightKind kind = RenderLightKind::Point;
    std::array<float, 3> position{};
    std::array<float, 4> rotation{ 0.0F, 0.0F, 0.0F, 1.0F };
    std::array<float, 3> color{ 1.0F, 1.0F, 1.0F };
    float intensity = 1.0F;
    float range = 10.0F;
    float innerConeDegrees = 25.0F;
    float outerConeDegrees = 35.0F;
    float areaWidth = 1.0F;
    float areaHeight = 1.0F;
    float contactShadowLength = 0.0F;
    float volumetricScattering = 0.0F;
    bool castsShadow = true;
    bool visible = true;
};

struct MeshRenderProxy {
    RenderProxyId id{};
    MeshRenderProxyDesc desc{};
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
};

struct CameraRenderProxy {
    RenderProxyId id{};
    CameraRenderProxyDesc desc{};
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
};

struct LightRenderProxy {
    RenderProxyId id{};
    LightRenderProxyDesc desc{};
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
};

struct RenderSceneReserveDesc {
    std::uint32_t meshProxies = 0;
    std::uint32_t cameraProxies = 0;
    std::uint32_t lightProxies = 0;
    std::uint32_t drawGroupKeys = 0;
};

struct RenderSceneStats {
    std::uint32_t meshProxyCount = 0;
    std::uint32_t cameraProxyCount = 0;
    std::uint32_t lightProxyCount = 0;
    std::uint32_t meshProxyCapacity = 0;
    std::uint32_t cameraProxyCapacity = 0;
    std::uint32_t lightProxyCapacity = 0;
    std::uint32_t drawGroupLookupCapacity = 0;
};

class RenderScene {
public:
    using MeshProxyMap = std::unordered_map<std::uint64_t, MeshRenderProxy>;
    using CameraProxyMap = std::unordered_map<std::uint64_t, CameraRenderProxy>;
    using LightProxyMap = std::unordered_map<std::uint64_t, LightRenderProxy>;

    void Reserve(const RenderSceneReserveDesc& desc);
    [[nodiscard]] RenderSceneStats Stats() const noexcept;
    [[nodiscard]] RenderProxyId UpsertMesh(const MeshRenderProxyDesc& desc);
    [[nodiscard]] RenderProxyId UpsertCamera(const CameraRenderProxyDesc& desc);
    [[nodiscard]] RenderProxyId UpsertLight(const LightRenderProxyDesc& desc);

    [[nodiscard]] bool RemoveMesh(std::uint64_t entityId) noexcept;
    [[nodiscard]] bool RemoveCamera(std::uint64_t entityId) noexcept;
    [[nodiscard]] bool RemoveLight(std::uint64_t entityId) noexcept;

    [[nodiscard]] const MeshRenderProxy* FindMeshByEntity(std::uint64_t entityId) const noexcept;
    [[nodiscard]] const CameraRenderProxy* FindCameraByEntity(std::uint64_t entityId) const noexcept;
    [[nodiscard]] const LightRenderProxy* FindLightByEntity(std::uint64_t entityId) const noexcept;

    [[nodiscard]] std::uint32_t RemoveMeshesNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept;
    [[nodiscard]] std::uint32_t RemoveCamerasNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept;
    [[nodiscard]] std::uint32_t RemoveLightsNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept;

    [[nodiscard]] std::size_t MeshProxyCount() const noexcept;
    [[nodiscard]] std::size_t CameraProxyCount() const noexcept;
    [[nodiscard]] std::size_t LightProxyCount() const noexcept;

    [[nodiscard]] const MeshProxyMap& MeshProxies() const noexcept;
    [[nodiscard]] const CameraProxyMap& CameraProxies() const noexcept;
    [[nodiscard]] const LightProxyMap& LightProxies() const noexcept;

    void ClearDirty() noexcept;
    [[nodiscard]] std::optional<SceneRenderCamera> BuildPrimaryCamera(std::uint32_t viewportWidth, std::uint32_t viewportHeight) const;
    // The returned cache is refreshed lazily and remains stable until the next mesh proxy mutation.
    [[nodiscard]] const std::vector<SceneRenderDrawGroup>& DrawGroups() const;
    void BuildDrawGroups(std::vector<SceneRenderDrawGroup>& outDrawGroups) const;
    [[nodiscard]] std::size_t DrawGroupCapacity() const noexcept;
    [[nodiscard]] std::size_t DrawGroupInstanceCapacity() const noexcept;
    [[nodiscard]] std::size_t DrawGroupLookupScratchCapacity() const noexcept;
    void BuildSnapshotInto(std::uint32_t viewportWidth, std::uint32_t viewportHeight, SceneRenderSnapshot& outSnapshot) const;

private:
    struct DrawGroupKey {
        std::uint64_t meshAssetId = 0;
        std::uint64_t materialAssetId = 0;

        [[nodiscard]] friend constexpr bool operator==(DrawGroupKey lhs, DrawGroupKey rhs) noexcept = default;
    };

    struct DrawGroupKeyHash {
        [[nodiscard]] std::size_t operator()(DrawGroupKey key) const noexcept;
    };

    [[nodiscard]] RenderProxyId AllocateProxyId() noexcept;
    void InvalidateDrawGroups() noexcept;
    void RebuildDrawGroupsIfNeeded() const;

    MeshProxyMap meshes_;
    CameraProxyMap cameras_;
    LightProxyMap lights_;
    mutable std::vector<SceneRenderDrawGroup> drawGroups_;
    mutable std::unordered_map<DrawGroupKey, std::size_t, DrawGroupKeyHash> drawGroupLookupScratch_;
    std::uint64_t nextProxyId_ = 1U;
    mutable bool drawGroupsDirty_ = true;
};

} // namespace kb::render
