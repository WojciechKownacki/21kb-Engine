#include "scene/components/SceneParticleEffectComponentStore.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/World.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneParticleEffectComponentStore::SceneParticleEffectComponentStore(kb::ecs::World& world, std::uint64_t) noexcept
    : world_(&world) {}
bool SceneParticleEffectComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<ParticleEffectComponent>(world_, entity);
}
const ParticleEffectComponent* SceneParticleEffectComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<ParticleEffectComponent>(world_, entity);
}
ParticleEffectComponent* SceneParticleEffectComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<ParticleEffectComponent>(world_, entity);
}
void SceneParticleEffectComponentStore::ForEach(ParticleEffectVisitor visitor, void* context) const {
    if (world_ == nullptr || visitor == nullptr)
        return;
    kb::ecs::Query<ParticleEffectComponent> query = world_->CreateQuery<ParticleEffectComponent>();
    kb::ecs::UnsafeHotReadQuery<ParticleEffectComponent> hotQuery;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!query.IsValid() || !hotQuery.Rebuild(query, settings))
        return;
    hotQuery.ForEachRange(settings.maxBatchSize, [visitor, context](const auto& chunk) {
        const ParticleEffectComponent* components = chunk.template Components<0>();
        for (std::size_t index = 0U; index < chunk.Count(); ++index)
            visitor(chunk.EntityAt(index), components[index], context);
    });
}
void SceneParticleEffectComponentStore::Set(SceneEntity entity, const ParticleEffectComponent& component) {
    SceneComponentStorageAccess::Set<ParticleEffectComponent>(world_, entity, component);
}
void SceneParticleEffectComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<ParticleEffectComponent>(world_, entity);
}
void SceneParticleEffectComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<ParticleEffectComponent>(world_, entity);
}

} // namespace kb::scene
