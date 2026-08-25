#pragma once

#include "engine/particles/ParticleRenderSnapshot.hpp"

#include "kb/render/scene/RenderProxyId.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"
#include "kb/render/resources/RenderSkinningPaletteAllocator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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

// LIB-136: mirrors kb::scene::CameraClearMode.
enum class RenderCameraClearMode : std::uint8_t {
    SolidColor,
    DepthOnly,
    DontClear,
};

struct MeshRenderProxyDesc {
    std::uint64_t entityId = 0;
    std::uint64_t meshAssetId = 0;
    // Non-zero only for an entity-local CPU morph resource. meshAssetId then
    // names the transient resource while this field remains the authored asset.
    std::uint64_t skeletalMeshAssetId = 0;
    std::uint64_t materialAssetId = 0;
    std::array<std::uint64_t, kMaxSceneMaterialSlotOverrides> materialSlotAssetIds{};
    std::uint32_t materialSlotOverrideCount = 0;
    std::array<float, 16> model{};
    // Optional animated/deformed local-space sphere. When valid it replaces
    // static mesh bounds for every visibility and shadow-culling path.
    RenderBoundsSphere boundsOverride{};
    std::array<float, 4> color{ 0.76F, 0.80F, 0.86F, 1.0F };
    float fadeAmount = 1.0F;
    float customData0 = 0.0F;
    // Renderer-transient palette allocations; never mirror authored ECS state.
    RenderSkinningPaletteHandle currentSkinningPalette{};
    RenderSkinningPaletteHandle previousSkinningPalette{};
    bool visible = true;
    bool castsShadow = true;
    bool receivesShadow = true;
    // LIB-136: mirrors kb::scene::MeshRendererComponent::layer.
    std::uint32_t layer = 1U;
    std::uint64_t detailSwitchGroupId = 0U;
    std::uint32_t detailSwitchMinimumLod = 0U;
    std::uint32_t detailSwitchMaximumLod = 255U;
    float detailSwitchPromoteCoverage = 0.20F;
    float detailSwitchDemoteCoverage = 0.15F;
    bool detailSwitchEnabled = false;
    std::int32_t lodBias = 0;
    bool lodEnabled = true;
    bool morphDeformationEnabled = false;
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
    // LIB-135: mirrors kb::scene::CameraComponent::viewportId/priority. 0
    // means "any viewport". BuildPrimaryCamera uses these to pick a
    // deterministic camera per target viewport instead of an arbitrary
    // unordered_map-iteration-order first match.
    std::uint32_t viewportId = 0;
    std::int32_t priority = 0;
    // LIB-136: mirrors kb::scene::CameraComponent::cullingMask/clearMode/clearColor.
    std::uint32_t cullingMask = 0xFFFFFFFFU;
    RenderCameraClearMode clearMode = RenderCameraClearMode::SolidColor;
    std::array<float, 3> clearColor{ 0.0F, 0.0F, 0.0F };
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
    // LIB-141: mirrors kb::scene::LightComponent::layerMask. Filtered against the current
    // camera's cullingMask by SceneForwardLightSelector - see LightComponent.hpp's own doc
    // comment for the default-safety reasoning.
    std::uint32_t layer = 1U;
};

// This proxy never produces a draw. It is a renderer-derived representation
// of an ECS Visibility Blocker component and is consumed only by culling.
struct VisibilityBlockerRenderProxyDesc {
    std::uint64_t entityId = 0;
    std::array<float, 16> model{};
    std::array<float, 3> localCenter{};
    std::array<float, 3> size{ 1.0F, 1.0F, 1.0F };
};

// Renderer-owned expansion of one canonical GeometrySwarmComponent.  Its
// generated instances are transient input to the existing mesh batching and
// GPU-driven indirect path; no expanded copy is written back into ECS.
struct GeometrySwarmRenderProxyDesc {
    std::uint64_t entityId = 0U;
    std::uint64_t meshAssetId = 0U;
    std::uint64_t materialAssetId = 0U;
    std::array<float, 16> model{};
    std::uint32_t instanceCount = 0U;
    std::uint16_t columns = 1U;
    std::uint16_t rows = 1U;
    std::uint16_t layers = 1U;
    std::array<float, 3> spacing{ 1.0F, 1.0F, 1.0F };
    float instanceScale = 1.0F;
    bool visible = true;
    bool castsShadow = true;
    bool receivesShadow = true;
    std::uint32_t layer = 1U;
};

enum class RenderSurfaceCastRegion : std::uint8_t { Circle2D, Rectangle2D, Sphere, Box, Capsule };

// Renderer-derived projection descriptor. The canonical configuration remains
// the ECS SurfaceCastComponent plus RegionShapeComponent on the same entity.
struct SurfaceCastRenderProxyDesc {
    std::uint64_t entityId = 0U;
    std::uint64_t materialAssetId = 0U;
    std::array<float, 16> model{};
    std::array<float, 3> localCenter{};
    std::array<float, 3> size{ 1.0F, 1.0F, 1.0F };
    float radius = 0.5F;
    float height = 2.0F;
    std::uint32_t receiverLayerMask = 0xFFFFFFFFU;
    std::int32_t order = 0;
    RenderSurfaceCastRegion region = RenderSurfaceCastRegion::Box;
    bool visible = true;
};

// Renderer-derived stroke input. The curve points remain canonical ECS data;
// the per-segment mesh instances are created only at the existing batch edge.
struct SpaceStrokeRenderProxyDesc {
    std::uint64_t entityId = 0U;
    std::uint64_t meshAssetId = 0U;
    std::uint64_t materialAssetId = 0U;
    std::array<float, 16> model{};
    std::array<std::array<float, 3>, 8U> controlPoints{};
    std::uint8_t controlPointCount = 0U;
    std::uint8_t splineSegments = 0U;
    std::uint8_t mode = 0U;
    float width = 0.1F;
    float cableSag = 0.0F;
    std::uint32_t layer = 1U;
    bool closed = false;
    bool visible = true;
    bool castsShadow = false;
    bool receivesShadow = true;
};

struct MeshRenderProxy {
    RenderProxyId id{};
    MeshRenderProxyDesc desc{};
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
    // Cached draw-group instance location, stamped with the draw-group build
    // version so a transform-only update can refresh the instance in place with a
    // single proxy lookup (no second entity->location map). A stale stamp falls
    // back to a full rebuild.
    mutable std::uint32_t instanceGroupIndex = 0;
    mutable std::uint32_t instanceIndexInGroup = 0;
    mutable std::uint64_t instanceLocationVersion = 0;
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

struct VisibilityBlockerRenderProxy {
    RenderProxyId id{};
    VisibilityBlockerRenderProxyDesc desc{};
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
};

struct GeometrySwarmRenderProxy {
    RenderProxyId id{};
    GeometrySwarmRenderProxyDesc desc{};
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
};
struct SurfaceCastRenderProxy { RenderProxyId id{}; SurfaceCastRenderProxyDesc desc{}; RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None; };
struct SpaceStrokeRenderProxy { RenderProxyId id{}; SpaceStrokeRenderProxyDesc desc{}; RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None; };

struct RenderSceneReserveDesc {
    std::uint32_t meshProxies = 0;
    std::uint32_t cameraProxies = 0;
    std::uint32_t lightProxies = 0;
    std::uint32_t visibilityBlockerProxies = 0;
    std::uint32_t geometrySwarmProxies = 0;
    std::uint32_t surfaceCastProxies = 0;
    std::uint32_t spaceStrokeProxies = 0;
    std::uint32_t drawGroupKeys = 0;
};

struct RenderSceneStats {
    std::uint32_t meshProxyCount = 0;
    std::uint32_t cameraProxyCount = 0;
    std::uint32_t lightProxyCount = 0;
    std::uint32_t visibilityBlockerProxyCount = 0;
    std::uint32_t geometrySwarmProxyCount = 0;
    std::uint32_t surfaceCastProxyCount = 0;
    std::uint32_t spaceStrokeProxyCount = 0;
    std::uint32_t meshProxyCapacity = 0;
    std::uint32_t cameraProxyCapacity = 0;
    std::uint32_t lightProxyCapacity = 0;
    std::uint32_t visibilityBlockerProxyCapacity = 0;
    std::uint32_t drawGroupLookupCapacity = 0;
    // H2/H7 telemetry: transform-only updates that refreshed an instance in place
    // (no draw-group rebuild) versus those that fell back to a full invalidation.
    std::uint64_t transformInPlaceUpdateCount = 0;
    std::uint64_t transformFallbackUpdateCount = 0;
};

class RenderScene {
public:
    using MeshProxyMap = std::unordered_map<std::uint64_t, MeshRenderProxy>;
    using CameraProxyMap = std::unordered_map<std::uint64_t, CameraRenderProxy>;
    using LightProxyMap = std::unordered_map<std::uint64_t, LightRenderProxy>;
    using VisibilityBlockerProxyMap = std::unordered_map<std::uint64_t, VisibilityBlockerRenderProxy>;
    using GeometrySwarmProxyMap = std::unordered_map<std::uint64_t, GeometrySwarmRenderProxy>;
    using SurfaceCastProxyMap = std::unordered_map<std::uint64_t, SurfaceCastRenderProxy>;
    using SpaceStrokeProxyMap = std::unordered_map<std::uint64_t, SpaceStrokeRenderProxy>;

    void Reserve(const RenderSceneReserveDesc& desc);
    [[nodiscard]] RenderSceneStats Stats() const noexcept;
    [[nodiscard]] RenderProxyId UpsertMesh(const MeshRenderProxyDesc& desc);
    [[nodiscard]] RenderProxyId UpsertCamera(const CameraRenderProxyDesc& desc);
    [[nodiscard]] RenderProxyId UpsertLight(const LightRenderProxyDesc& desc);
    [[nodiscard]] RenderProxyId UpsertVisibilityBlocker(const VisibilityBlockerRenderProxyDesc& desc);
    [[nodiscard]] RenderProxyId UpsertGeometrySwarm(const GeometrySwarmRenderProxyDesc& desc);
    [[nodiscard]] RenderProxyId UpsertSurfaceCast(const SurfaceCastRenderProxyDesc& desc);
    [[nodiscard]] RenderProxyId UpsertSpaceStroke(const SpaceStrokeRenderProxyDesc& desc);
    void SetWorldBackdrop(std::optional<SceneRenderWorldBackdrop> backdrop) noexcept;
    [[nodiscard]] const std::optional<SceneRenderWorldBackdrop>& WorldBackdrop() const noexcept;
    void SetAmbientRadiance(std::optional<SceneRenderAmbientRadiance> ambientRadiance) noexcept;
    [[nodiscard]] const std::optional<SceneRenderAmbientRadiance>& AmbientRadiance() const noexcept;

    enum class TransformUpdateOutcome {
        NotFound,
        InPlace,
        Fallback,
    };

    // H2/H7 - transform-only fast update. When the draw-group cache is clean it
    // refreshes the cached instance's model matrix in place (no regrouping, no
    // rebuild); otherwise it updates the proxy and invalidates so the next
    // rebuild picks it up. Returns false only when the entity has no mesh proxy.
    [[nodiscard]] bool UpdateMeshTransform(std::uint64_t entityId, const std::array<float, 16>& model);
    [[nodiscard]] bool UpdateVisibilityBlockerTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept;
    [[nodiscard]] bool UpdateGeometrySwarmTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept;
    [[nodiscard]] bool UpdateSurfaceCastTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept;
    [[nodiscard]] bool UpdateSpaceStrokeTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept;

    // H6 - shared core for the single-entity and parallel batch paths. Updates
    // the proxy's source-of-truth model and, when the cache is clean, the cached
    // instance in place. Never invalidates or touches telemetry, so it is safe to
    // call concurrently for distinct entity ids (each owns a distinct proxy and
    // instance slot). The caller applies invalidation/telemetry once per batch.
    [[nodiscard]] TransformUpdateOutcome ApplyMeshTransform(std::uint64_t entityId, const std::array<float, 16>& model);
    [[nodiscard]] bool ApplyGeometrySwarmTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept;
    [[nodiscard]] bool ApplySurfaceCastTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept;
    [[nodiscard]] bool ApplySpaceStrokeTransform(std::uint64_t entityId, const std::array<float, 16>& model) noexcept;
    void InvalidateDrawGroupsIfFallback(TransformUpdateOutcome outcome) noexcept;
    void AddTransformUpdateCounts(std::uint64_t inPlace, std::uint64_t fallback) noexcept;

    [[nodiscard]] bool RemoveMesh(std::uint64_t entityId) noexcept;
    [[nodiscard]] bool RemoveCamera(std::uint64_t entityId) noexcept;
    [[nodiscard]] bool RemoveLight(std::uint64_t entityId) noexcept;
    [[nodiscard]] bool RemoveVisibilityBlocker(std::uint64_t entityId) noexcept;
    [[nodiscard]] bool RemoveGeometrySwarm(std::uint64_t entityId) noexcept;
    [[nodiscard]] bool RemoveSurfaceCast(std::uint64_t entityId) noexcept;
    [[nodiscard]] bool RemoveSpaceStroke(std::uint64_t entityId) noexcept;

    [[nodiscard]] const MeshRenderProxy* FindMeshByEntity(std::uint64_t entityId) const noexcept;
    [[nodiscard]] const CameraRenderProxy* FindCameraByEntity(std::uint64_t entityId) const noexcept;
    [[nodiscard]] const LightRenderProxy* FindLightByEntity(std::uint64_t entityId) const noexcept;

    [[nodiscard]] std::uint32_t RemoveMeshesNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept;
    [[nodiscard]] std::uint32_t RemoveCamerasNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept;
    [[nodiscard]] std::uint32_t RemoveLightsNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept;
    [[nodiscard]] std::uint32_t RemoveVisibilityBlockersNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept;
    [[nodiscard]] std::uint32_t RemoveGeometrySwarmsNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept;
    [[nodiscard]] std::uint32_t RemoveSurfaceCastsNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept;
    [[nodiscard]] std::uint32_t RemoveSpaceStrokesNotInSorted(std::span<const std::uint64_t> sortedEntityIds) noexcept;

    [[nodiscard]] std::size_t MeshProxyCount() const noexcept;
    [[nodiscard]] std::size_t CameraProxyCount() const noexcept;
    [[nodiscard]] std::size_t LightProxyCount() const noexcept;
    [[nodiscard]] std::size_t VisibilityBlockerProxyCount() const noexcept;
    [[nodiscard]] std::size_t GeometrySwarmProxyCount() const noexcept;
    [[nodiscard]] std::size_t SurfaceCastProxyCount() const noexcept;
    [[nodiscard]] std::size_t SpaceStrokeProxyCount() const noexcept;

    [[nodiscard]] const MeshProxyMap& MeshProxies() const noexcept;
    [[nodiscard]] const CameraProxyMap& CameraProxies() const noexcept;
    [[nodiscard]] const LightProxyMap& LightProxies() const noexcept;
    [[nodiscard]] const VisibilityBlockerProxyMap& VisibilityBlockerProxies() const noexcept;
    [[nodiscard]] const GeometrySwarmProxyMap& GeometrySwarmProxies() const noexcept;
    [[nodiscard]] const SurfaceCastProxyMap& SurfaceCastProxies() const noexcept;
    [[nodiscard]] const SpaceStrokeProxyMap& SpaceStrokeProxies() const noexcept;

    void SetParticleRenderSnapshot(std::shared_ptr<const kb::particles::ParticleRenderSnapshot> snapshot) noexcept;
    [[nodiscard]] const std::shared_ptr<const kb::particles::ParticleRenderSnapshot>& ParticleRenderSnapshot() const noexcept;

    void ClearDirty() noexcept;
    // LIB-135: targetViewportId selects among cameras whose viewportId either
    // matches exactly or is 0 ("any viewport" - the default every camera
    // authored before LIB-135 keeps, so single-viewport callers that never
    // pass a real id keep matching every camera exactly as before). Among
    // matching, visible, primary candidates the highest priority wins; a
    // priority tie breaks on the lowest entityId, so the result is
    // deterministic regardless of unordered_map iteration order.
    [[nodiscard]] std::optional<SceneRenderCamera> BuildPrimaryCamera(std::uint32_t viewportWidth, std::uint32_t viewportHeight, std::uint32_t targetViewportId = 0U) const;
    // LIB-136: same selection as BuildPrimaryCamera, without building view/projection
    // matrices - used where only the selected camera's non-matrix settings (clearMode/
    // clearColor) are needed, cheaply, before the rest of a frame's camera data is resolved.
    [[nodiscard]] const CameraRenderProxyDesc* FindPrimaryCameraProxy(std::uint32_t targetViewportId = 0U) const noexcept;
    // The returned cache is refreshed lazily and remains stable until the next mesh proxy mutation.
    [[nodiscard]] const std::vector<SceneRenderDrawGroup>& DrawGroups() const;
    void BuildDrawGroups(std::vector<SceneRenderDrawGroup>& outDrawGroups) const;
    [[nodiscard]] std::size_t DrawGroupCapacity() const noexcept;
    [[nodiscard]] std::size_t DrawGroupInstanceCapacity() const noexcept;
    [[nodiscard]] std::size_t DrawGroupLookupScratchCapacity() const noexcept;
    void BuildSnapshotInto(std::uint32_t viewportWidth, std::uint32_t viewportHeight, SceneRenderSnapshot& outSnapshot, std::uint32_t targetViewportId = 0U) const;

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
    void ApplySurfaceCasts(SceneRenderMeshInstance& instance) const;

    MeshProxyMap meshes_;
    CameraProxyMap cameras_;
    LightProxyMap lights_;
    VisibilityBlockerProxyMap visibilityBlockers_;
    GeometrySwarmProxyMap geometrySwarms_;
    SurfaceCastProxyMap surfaceCasts_;
    SpaceStrokeProxyMap spaceStrokes_;
    std::optional<SceneRenderWorldBackdrop> worldBackdrop_;
    std::optional<SceneRenderAmbientRadiance> ambientRadiance_;
    std::shared_ptr<const kb::particles::ParticleRenderSnapshot> particleRenderSnapshot_;
    mutable std::vector<SceneRenderDrawGroup> drawGroups_;
    mutable std::unordered_map<DrawGroupKey, std::size_t, DrawGroupKeyHash> drawGroupLookupScratch_;
    std::uint64_t nextProxyId_ = 1U;
    mutable bool drawGroupsDirty_ = true;
    mutable std::uint64_t drawGroupBuildVersion_ = 1U;
    std::uint64_t transformInPlaceUpdateCount_ = 0;
    std::uint64_t transformFallbackUpdateCount_ = 0;
};

} // namespace kb::render
