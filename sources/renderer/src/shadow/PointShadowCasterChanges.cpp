#include "shadow/PointShadowCasterChanges.hpp"

#include "scene/pipeline/MeshPipelineVisibility.hpp"

namespace kb::render {
namespace {

[[nodiscard]] bool Reaches(const RenderBoundsSphere& caster, const PointShadowSetup& setup, std::uint32_t row) noexcept {
    const float reach = caster.radius + setup.binding.positionRange[row * 4U + 3U];
    float distanceSquared = 0.0F;
    for (std::uint32_t axis = 0U; axis < 3U; ++axis) {
        const float delta = caster.center[axis] - setup.binding.positionRange[row * 4U + axis];
        distanceSquared += delta * delta;
    }
    return distanceSquared <= reach * reach;
}

} // namespace

void PointShadowCasterChanges::Apply(
    const RenderScene& renderScene,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    const PointShadowSetup& setup,
    ShadowMapResource& map) noexcept {
    const std::uint64_t frame = renderScene.MotionFrame();
    if (map.CasterChangesFrame() == frame) {
        return;
    }
    const std::uint64_t previousFrame = map.CasterChangesFrame();
    const std::uint64_t previousRevision = map.CasterChangesRevision();
    map.SetCasterChangesFrame(frame);
    map.SetCasterChangesRevision(renderScene.MeshContentRevision());
    if (previousFrame + 1U != frame || renderScene.ShadowCasterGlobalChangeFrame() > previousFrame) {
        map.DiscardAllCachedTiles();
        return;
    }
    if (previousRevision == renderScene.MeshContentRevision()) {
        return;
    }
    for (const auto& [entityId, proxy] : renderScene.MeshProxies()) {
        static_cast<void>(entityId);
        if (proxy.shadowChangeFrame != frame) {
            continue;
        }
        RenderBoundsSphere local = proxy.desc.boundsOverride;
        if (!local.IsValid()) {
            const RenderMeshResource* mesh = resources.FindMesh(resourceMap.ResolveMesh(proxy.desc.meshAssetId));
            if (mesh == nullptr) {
                map.DiscardAllCachedTiles();
                return;
            }
            local = mesh->bounds;
        }
        const bool moved = proxy.previousModelFrame == frame && proxy.spawnMotionFrame != frame;
        const RenderBoundsSphere reaches[2]{
            MeshPipelineVisibility::TransformBounds(local, proxy.desc.model),
            MeshPipelineVisibility::TransformBounds(local, moved ? proxy.previousModel : proxy.desc.model),
        };
        for (std::uint32_t row = 0U; row < setup.lightCount; ++row) {
            if (setup.binding.entityId[row] != 0U && (Reaches(reaches[0], setup, row) || Reaches(reaches[1], setup, row))) {
                map.DiscardCachedTiles(row);
            }
        }
    }
}

} // namespace kb::render
