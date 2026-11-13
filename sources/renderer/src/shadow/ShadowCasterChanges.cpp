#include "shadow/ShadowCasterChanges.hpp"

#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <cmath>

namespace kb::render {
namespace {

// What a cache has to forget this motion frame: `discardAll` once when the whole cache is stale, else `reached`
// with the bounds of each changed caster, both where it was and where it is.
template <typename DiscardAll, typename Reached>
void ScanChanges(
    const RenderScene& renderScene,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    ShadowMapResource& map,
    DiscardAll&& discardAll,
    Reached&& reached) noexcept {
    const std::uint64_t frame = renderScene.MotionFrame();
    if (map.CasterChangesScene() != renderScene.InstanceStamp()) {
        // Made for another scene (or for none): nothing in it can be kept.
        map.SetCasterChangesScene(renderScene.InstanceStamp());
        map.SetCasterChangesFrame(frame);
        map.SetCasterChangesRevision(renderScene.MeshContentRevision());
        discardAll();
        return;
    }
    if (map.CasterChangesFrame() == frame) {
        return;
    }
    const std::uint64_t previousFrame = map.CasterChangesFrame();
    const std::uint64_t previousRevision = map.CasterChangesRevision();
    map.SetCasterChangesFrame(frame);
    map.SetCasterChangesRevision(renderScene.MeshContentRevision());
    if (previousFrame + 1U != frame || renderScene.ShadowCasterGlobalChangeFrame() > previousFrame) {
        discardAll();
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
                discardAll();
                return;
            }
            local = mesh->bounds;
        }
        const bool moved = proxy.previousModelFrame == frame && proxy.spawnMotionFrame != frame;
        reached(MeshPipelineVisibility::TransformBounds(local, proxy.desc.model));
        reached(MeshPipelineVisibility::TransformBounds(local, moved ? proxy.previousModel : proxy.desc.model));
    }
}

[[nodiscard]] bool Reaches(const RenderBoundsSphere& caster, const PointShadowSetup& setup, std::uint32_t row) noexcept {
    const float reach = caster.radius + setup.binding.positionRange[row * 4U + 3U];
    float distanceSquared = 0.0F;
    for (std::uint32_t axis = 0U; axis < 3U; ++axis) {
        const float delta = caster.center[axis] - setup.binding.positionRange[row * 4U + axis];
        distanceSquared += delta * delta;
    }
    return distanceSquared <= reach * reach;
}

// Whether the caster overlaps the square the cascade's orthographic window covers, whatever its depth.
[[nodiscard]] bool Reaches(const RenderBoundsSphere& caster, const ShadowMapResource::CachedCascade& cascade) noexcept {
    const auto& view = cascade.view;
    const auto& center = caster.center;
    const float x = view[0] * center[0] + view[4] * center[1] + view[8] * center[2] + view[12];
    const float y = view[1] * center[0] + view[5] * center[1] + view[9] * center[2] + view[13];
    const float reach = cascade.halfExtent + caster.radius;
    return std::abs(x) <= reach && std::abs(y) <= reach;
}

} // namespace

void ShadowCasterChanges::ApplyToLocalLights(
    const RenderScene& renderScene,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    const PointShadowSetup& setup,
    ShadowMapResource& map) noexcept {
    ScanChanges(renderScene, resources, resourceMap, map,
        [&map] { map.DiscardAllCachedTiles(); },
        [&](const RenderBoundsSphere& caster) {
            for (std::uint32_t row = 0U; row < setup.lightCount; ++row) {
                if (setup.binding.entityId[row] != 0U && Reaches(caster, setup, row)) {
                    map.DiscardCachedTiles(row);
                }
            }
        });
}

void ShadowCasterChanges::ApplyToCascades(
    const RenderScene& renderScene,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    ShadowMapResource& map) noexcept {
    ScanChanges(renderScene, resources, resourceMap, map,
        [&map] { map.DiscardAllCachedCascades(); },
        [&map](const RenderBoundsSphere& caster) {
            for (std::uint32_t cascade = 0U; cascade < ShadowMapResource::kCascades; ++cascade) {
                if (map.CachedCascadeAt(cascade).key != 0U && Reaches(caster, map.CachedCascadeAt(cascade))) {
                    map.DiscardCachedCascade(cascade);
                }
            }
        });
}

} // namespace kb::render
