#include "engine/scene/ScenePostProcessVolumes.hpp"

#include "engine/scene/PostProcessVolumeComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneRegionShapeQueries.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityCounter.hpp"

#include <algorithm>
#include <optional>

namespace kb::scene {

bool SceneHasPostProcessVolumes(const Scene& scene) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    return SceneEntityCounter::CountWithComponent(state.world, state.components.PostProcessVolumeComponentId()) != 0U;
}

void ScenePostProcessVolumeInfluences(const Scene& scene, const kb::math::DVec3& worldPoint, std::vector<ScenePostProcessVolumeInfluence>& out) {
    SceneState& state = SceneAccess::State(const_cast<Scene&>(scene));
    const std::size_t first = out.size();
    struct Gather {
        const Scene& scene;
        const kb::math::DVec3& point;
        std::vector<ScenePostProcessVolumeInfluence>& out;
    } gather{ scene, worldPoint, out };
    state.world.CreateQuery<PostProcessVolumeComponent>().ForEach(
        [](SceneEntity entity, const PostProcessVolumeComponent& volume, void* context) {
            auto& gathered = *static_cast<Gather*>(context);
            if (!volume.enabled || volume.profileAssetId == 0U || !IsPostProcessVolumeComponentValid(volume)) return;
            float reach = 1.0F;
            if (gathered.scene.Components().RegionShapes().Has(entity)) {
                const std::optional<float> distance = SceneRegionShapeDistance(gathered.scene, entity, gathered.point);
                if (!distance.has_value()) return;
                if (*distance > 0.0F) {
                    if (volume.blendDistance <= 0.0F || *distance >= volume.blendDistance) return;
                    reach = 1.0F - *distance / volume.blendDistance;
                }
            }
            const float weight = volume.weight * reach;
            if (weight <= 0.0F) return;
            gathered.out.push_back(ScenePostProcessVolumeInfluence{ entity, volume.profileAssetId, volume.priority, weight });
        }, &gather);
    std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(), [](const ScenePostProcessVolumeInfluence& a, const ScenePostProcessVolumeInfluence& b) {
        return a.priority != b.priority ? a.priority < b.priority : a.entity.Id() < b.entity.Id();
    });
}

} // namespace kb::scene
