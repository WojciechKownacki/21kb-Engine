#include "scene/SceneContentInstanceService.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace kb::scene {
namespace {

struct AuthoredContentInstance {
    SceneEntity entity{};
    ContentInstanceComponent component{};
    std::int32_t streamPriority = 0;
};

struct ActiveStreamFocus {
    SceneEntity entity{};
    StreamFocusComponent component{};
    kb::math::Vec3 position{};
};

[[nodiscard]] StreamLoadMask MaskFor(ContentInstanceKind kind) noexcept {
    switch (kind) {
    case ContentInstanceKind::Prefab: return StreamLoadMask::Prefab;
    case ContentInstanceKind::Subscene: return StreamLoadMask::Subscene;
    case ContentInstanceKind::WorldFragment: return StreamLoadMask::WorldFragment;
    }
    return StreamLoadMask::None;
}

[[nodiscard]] std::vector<ActiveStreamFocus> CollectStreamFocuses(const Scene& scene) {
    std::vector<ActiveStreamFocus> output;
    kb::ecs::Query<StreamFocusComponent, TransformComponent> query = const_cast<Scene&>(scene).Runtime().EcsWorld().CreateQuery<StreamFocusComponent, TransformComponent>();
    kb::ecs::UnsafeHotReadQuery<StreamFocusComponent, TransformComponent> hot;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!query.IsValid() || !hot.Rebuild(query, settings)) return output;
    hot.ForEachRange(settings.maxBatchSize, [&output](const auto& batch) {
        const StreamFocusComponent* focuses = batch.template Components<0>();
        const TransformComponent* transforms = batch.template Components<1>();
        for (std::size_t index = 0U; index < batch.Count(); ++index) {
            if (batch.EntityAt(index).IsValid() && focuses[index].enabled && IsStreamFocusValid(focuses[index])) {
                output.push_back({ batch.EntityAt(index), focuses[index], transforms[index].worldPosition });
            }
        }
    });
    return output;
}

[[nodiscard]] std::optional<std::int32_t> StreamPriority(
    const std::vector<ActiveStreamFocus>& focuses,
    kb::math::Vec3 position,
    ContentInstanceKind kind,
    bool retain) noexcept {
    if (focuses.empty()) return 0;
    const StreamLoadMask kindMask = MaskFor(kind);
    std::optional<std::int32_t> result;
    for (const ActiveStreamFocus& focus : focuses) {
        if (!ContainsStreamLoadMask(focus.component.loadMask, kindMask)) continue;
        const float dx = position.x - focus.position.x;
        const float dy = position.y - focus.position.y;
        const float dz = position.z - focus.position.z;
        const float radius = retain ? focus.component.outerRadius : focus.component.innerRadius;
        if (dx * dx + dy * dy + dz * dz <= radius * radius &&
            (!result.has_value() || focus.component.priority > *result)) result = focus.component.priority;
    }
    return result;
}

[[nodiscard]] bool Matches(const ContentInstanceRuntimeRecord& runtime, const ContentInstanceComponent& component) noexcept {
    return runtime.assetId == component.assetId && runtime.kind == component.kind && runtime.lifetime == component.lifetime;
}

void Release(Scene& scene, ContentInstanceRuntimeRecord& runtime, bool preserve) noexcept {
    if (preserve) return;
    if (runtime.loadedSceneId != 0U) {
        static_cast<void>(scene.LoadedContent().Unload(runtime.loadedSceneId));
    } else if (runtime.root.IsValid() && scene.Entities().IsAlive(runtime.root)) {
        scene.Entities().Destroy(runtime.root);
    }
    runtime.root = {};
    runtime.loadedSceneId = 0U;
    runtime.prefab = {};
}

[[nodiscard]] bool ActivatePrefab(Scene& scene, SceneEntity owner, const ContentInstanceComponent& component, ContentInstanceRuntimeRecord& runtime) {
    runtime.prefab = scene.Assets().Manager().Load<ScenePrefab>(kb::assets::AssetId{ component.assetId });
    if (!runtime.prefab.IsLoaded() || runtime.prefab->Empty()) return false;
    ScenePrefabInstantiationSettings settings{};
    if (component.lifetime == ContentInstanceLifetime::Owner) settings.parent = scene.Entities().Object(owner);
    const ScenePrefabInstance instance = scene.Prefabs().Instantiate(*runtime.prefab, settings);
    const SceneObject root = instance.RootObject();
    if (!root.IsValid()) return false;
    runtime.root = root.Entity();
    return true;
}

[[nodiscard]] bool ActivateScene(Scene& scene, SceneEntity owner, const ContentInstanceComponent& component, ContentInstanceRuntimeRecord& runtime) {
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ component.assetId });
    if (metadata == nullptr) return false;
    const std::uint64_t loadedId = scene.LoadedContent().Load(metadata->virtualPath, true);
    if (loadedId == 0U) return false;
    SceneState& state = SceneAccess::State(scene);
    const auto record = std::ranges::find_if(state.loadedScenes, [loadedId](const SceneState::LoadedSceneRecord& item) { return item.id == loadedId; });
    if (record == state.loadedScenes.end() || !record->root.IsValid()) {
        static_cast<void>(scene.LoadedContent().Unload(loadedId));
        return false;
    }
    runtime.loadedSceneId = loadedId;
    runtime.root = record->root;
    if (component.lifetime == ContentInstanceLifetime::Owner && !scene.Entities().Object(runtime.root).SetParent(scene.Entities().Object(owner))) {
        static_cast<void>(scene.LoadedContent().Unload(loadedId));
        runtime.root = {};
        runtime.loadedSceneId = 0U;
        return false;
    }
    return true;
}

[[nodiscard]] bool Activate(Scene& scene, SceneEntity owner, const ContentInstanceComponent& component, ContentInstanceRuntimeRecord& runtime) {
    runtime.owner = owner;
    runtime.assetId = component.assetId;
    runtime.kind = component.kind;
    runtime.lifetime = component.lifetime;
    if (component.kind == ContentInstanceKind::Prefab) return ActivatePrefab(scene, owner, component, runtime);
    return ActivateScene(scene, owner, component, runtime);
}

[[nodiscard]] std::vector<AuthoredContentInstance> Collect(const Scene& scene, const SceneState& state) {
    std::vector<AuthoredContentInstance> output;
    const std::vector<ActiveStreamFocus> focuses = CollectStreamFocuses(scene);
    kb::ecs::Query<ContentInstanceComponent> query = const_cast<Scene&>(scene).Runtime().EcsWorld().CreateQuery<ContentInstanceComponent>();
    if (!query.IsValid()) return output;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    kb::ecs::UnsafeHotReadQuery<ContentInstanceComponent> hot;
    if (!hot.Rebuild(query, settings)) return output;
    hot.ForEachRange(settings.maxBatchSize, [&output, &scene, &state, &focuses](const auto& batch) {
        const ContentInstanceComponent* components = batch.template Components<0>();
        for (std::size_t index = 0U; index < batch.Count(); ++index) {
            const SceneEntity entity = batch.EntityAt(index);
            const ContentInstanceComponent& component = components[index];
            if (!entity.IsValid() || !component.active || component.assetId == 0U || !IsContentInstanceKindValid(component.kind) || !IsContentInstanceLifetimeValid(component.lifetime)) continue;
            const TransformComponent* transform = scene.Transforms().TryGet(entity);
            if (transform == nullptr) continue;
            const bool retain = state.contentInstances.contains(entity.Id());
            const std::optional<std::int32_t> priority = StreamPriority(focuses, transform->worldPosition, component.kind, retain);
            if (priority.has_value()) output.push_back({ entity, component, *priority });
        }
    });
    std::ranges::sort(output, [](const AuthoredContentInstance& left, const AuthoredContentInstance& right) {
        return left.streamPriority != right.streamPriority ? left.streamPriority > right.streamPriority : left.entity.Id() < right.entity.Id();
    });
    return output;
}

} // namespace

void SceneContentInstanceService::Synchronize(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    if (!state.isPlaying || state.mode == SceneMode::PrefabPrivate) {
        Shutdown(scene);
        return;
    }
    const std::vector<AuthoredContentInstance> authored = Collect(scene, state);
    for (auto it = state.contentInstances.begin(); it != state.contentInstances.end();) {
        const auto current = std::ranges::find_if(authored, [entity = it->second.owner](const AuthoredContentInstance& candidate) { return candidate.entity == entity; });
        const bool ownerAlive = scene.Entities().IsAlive(it->second.owner);
        const bool preserve = !ownerAlive && it->second.lifetime == ContentInstanceLifetime::Persistent;
        if (current == authored.end() || !ownerAlive || !Matches(it->second, current->component)) {
            Release(scene, it->second, preserve);
            it = state.contentInstances.erase(it);
        } else ++it;
    }
    for (const AuthoredContentInstance& item : authored) {
        if (state.contentInstances.contains(item.entity.Id())) continue;
        ContentInstanceRuntimeRecord runtime{};
        if (Activate(scene, item.entity, item.component, runtime)) state.contentInstances.emplace(item.entity.Id(), std::move(runtime));
    }
}

void SceneContentInstanceService::Shutdown(Scene& scene) noexcept {
    SceneState& state = SceneAccess::State(scene);
    for (auto& [id, runtime] : state.contentInstances) {
        static_cast<void>(id);
        Release(scene, runtime, false);
    }
    state.contentInstances.clear();
}

} // namespace kb::scene
