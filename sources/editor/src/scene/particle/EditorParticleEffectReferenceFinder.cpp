#include "scene/particle/EditorParticleEffectReferenceFinder.hpp"

#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneParticleEffectComponents.hpp"

#include <utility>

namespace kb::editor {
namespace {

struct ParticleEffectReferenceScan {
    const kb::scene::Scene* scene = nullptr;
    kb::assets::AssetId target{};
    std::vector<std::string> references;
};

void CollectParticleEffectReference(
    kb::scene::SceneEntity entity,
    const kb::scene::ParticleEffectComponent& component,
    void* context) {
    auto* scan = static_cast<ParticleEffectReferenceScan*>(context);
    if (component.effectAssetId != scan->target.value) {
        return;
    }
    scan->references.push_back(scan->scene->Entities().Name(entity) + " / Particle Effect / Effect");
}

} // namespace

std::vector<std::string> EditorParticleEffectReferenceFinder::FindSceneReferences(
    const kb::scene::Scene& scene, kb::assets::AssetId effectAssetId) {
    if (!effectAssetId.IsValid()) {
        return {};
    }
    ParticleEffectReferenceScan scan{ .scene = &scene, .target = effectAssetId };
    scene.Components().ParticleEffects().ForEach(&CollectParticleEffectReference, &scan);
    return std::move(scan.references);
}

} // namespace kb::editor
