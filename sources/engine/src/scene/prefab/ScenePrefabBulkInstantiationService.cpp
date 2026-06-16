#include "scene/prefab/ScenePrefabBulkInstantiationService.hpp"

#include "engine/ecs/CommandBuffer.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/prefab/ScenePrefabBakedData.hpp"
#include "scene/prefab/ScenePrefabNameResolver.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] std::size_t TotalNodeCount(std::size_t instanceCount, std::size_t nodeCount) {
    if (nodeCount != 0 && instanceCount > std::numeric_limits<std::size_t>::max() / nodeCount) {
        throw std::length_error("Scene prefab bulk instantiation node count exceeds addressable size");
    }
    return instanceCount * nodeCount;
}

[[nodiscard]] std::size_t EntityIndex(std::size_t instanceIndex, std::size_t nodeIndex, std::size_t nodeCount) noexcept {
    return instanceIndex * nodeCount + nodeIndex;
}

template <typename T>
void RepeatComponents(std::vector<T>& output, std::span<const T> source, std::size_t instanceCount) {
    const std::size_t totalCount = TotalNodeCount(instanceCount, source.size());
    output.clear();
    output.reserve(totalCount);
    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        output.insert(output.end(), source.begin(), source.end());
    }
}

template <typename T>
void AddComponentView(std::vector<kb::ecs::CommandBuffer::BulkComponentView>& views, std::span<const T> components) {
    views.push_back(kb::ecs::CommandBuffer::MakeBulkComponentView<T>(components));
}

struct ScenePrefabArchetypeSpawnPayload {
    std::vector<TransformComponent> transforms;
    std::vector<VisibilityComponent> visibility;
    std::vector<CameraComponent> cameras;
    std::vector<MeshRendererComponent> meshRenderers;
    std::vector<LightComponent> lights;
    std::vector<InputComponent> inputs;
    std::vector<RigidbodyComponent> rigidbodies;
    std::vector<ColliderComponent> colliders;
    std::vector<TagsComponent> tags;
    std::vector<BehaviourComponent> behaviours;
    std::vector<AudioSourceComponent> audioSources;
    std::vector<AudioListenerComponent> audioListeners;
    std::vector<kb::ecs::CommandBuffer::BulkComponentView> views;

    void Build(const ScenePrefabBakedArchetype& archetype, std::size_t instanceCount) {
        RepeatComponents(transforms, std::span<const TransformComponent>{ archetype.transforms }, instanceCount);
        RepeatComponents(visibility, std::span<const VisibilityComponent>{ archetype.visibility }, instanceCount);

        views.clear();
        views.reserve(12U);
        AddComponentView(views, std::span<const TransformComponent>{ transforms });
        AddComponentView(views, std::span<const VisibilityComponent>{ visibility });

        const std::uint16_t mask = archetype.componentMask;
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Camera)) {
            RepeatComponents(cameras, std::span<const CameraComponent>{ archetype.cameras }, instanceCount);
            AddComponentView(views, std::span<const CameraComponent>{ cameras });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::MeshRenderer)) {
            RepeatComponents(meshRenderers, std::span<const MeshRendererComponent>{ archetype.meshRenderers }, instanceCount);
            AddComponentView(views, std::span<const MeshRendererComponent>{ meshRenderers });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Light)) {
            RepeatComponents(lights, std::span<const LightComponent>{ archetype.lights }, instanceCount);
            AddComponentView(views, std::span<const LightComponent>{ lights });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Input)) {
            RepeatComponents(inputs, std::span<const InputComponent>{ archetype.inputs }, instanceCount);
            AddComponentView(views, std::span<const InputComponent>{ inputs });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Rigidbody)) {
            RepeatComponents(rigidbodies, std::span<const RigidbodyComponent>{ archetype.rigidbodies }, instanceCount);
            AddComponentView(views, std::span<const RigidbodyComponent>{ rigidbodies });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Collider)) {
            RepeatComponents(colliders, std::span<const ColliderComponent>{ archetype.colliders }, instanceCount);
            AddComponentView(views, std::span<const ColliderComponent>{ colliders });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Tags)) {
            RepeatComponents(tags, std::span<const TagsComponent>{ archetype.tags }, instanceCount);
            AddComponentView(views, std::span<const TagsComponent>{ tags });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Behaviour)) {
            RepeatComponents(behaviours, std::span<const BehaviourComponent>{ archetype.behaviours }, instanceCount);
            AddComponentView(views, std::span<const BehaviourComponent>{ behaviours });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AudioSource)) {
            RepeatComponents(audioSources, std::span<const AudioSourceComponent>{ archetype.audioSources }, instanceCount);
            AddComponentView(views, std::span<const AudioSourceComponent>{ audioSources });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AudioListener)) {
            RepeatComponents(audioListeners, std::span<const AudioListenerComponent>{ archetype.audioListeners }, instanceCount);
            AddComponentView(views, std::span<const AudioListenerComponent>{ audioListeners });
        }
    }
};

void QueueHierarchy(
    kb::ecs::CommandBuffer::WorkerBuffer& worker,
    std::span<const kb::ecs::CommandEntity> entities,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::size_t instanceCount,
    SceneObject rootParent) {
    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const ScenePrefabNodeDesc& node = nodes[nodeIndex];
            const std::size_t childIndex = EntityIndex(instanceIndex, nodeIndex, nodes.size());
            if (node.parentNode == ScenePrefabNodeDesc::NoParent) {
                if (rootParent.EntityHandle().IsValid()) {
                    worker.SetParent(entities[childIndex], kb::ecs::CommandEntity::Existing(rootParent.Entity()));
                }
                continue;
            }
            worker.SetParent(entities[childIndex], entities[EntityIndex(instanceIndex, node.parentNode, nodes.size())]);
        }
    }
}

[[nodiscard]] std::vector<kb::ecs::CommandEntity> CreateBakedEntities(
    kb::ecs::CommandBuffer::WorkerBuffer& worker,
    const ScenePrefabBakedData& baked,
    std::size_t instanceCount) {
    std::vector<kb::ecs::CommandEntity> entities(TotalNodeCount(instanceCount, baked.NodeCount()));
    ScenePrefabArchetypeSpawnPayload payload;

    for (const ScenePrefabBakedArchetype& archetype : baked.Archetypes()) {
        const std::size_t archetypeNodeCount = archetype.nodeIndices.size();
        const std::size_t archetypeEntityCount = TotalNodeCount(instanceCount, archetypeNodeCount);
        payload.Build(archetype, instanceCount);

        std::vector<kb::ecs::CommandEntity> created = worker.CreateEntities(archetypeEntityCount, std::span<const kb::ecs::CommandBuffer::BulkComponentView>{ payload.views });
        for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
            for (std::size_t archetypeNodeIndex = 0; archetypeNodeIndex < archetypeNodeCount; ++archetypeNodeIndex) {
                const std::size_t createdIndex = EntityIndex(instanceIndex, archetypeNodeIndex, archetypeNodeCount);
                const std::size_t prefabIndex = EntityIndex(instanceIndex, archetype.nodeIndices[archetypeNodeIndex], baked.NodeCount());
                entities[prefabIndex] = created[createdIndex];
            }
        }
    }

    return entities;
}

[[nodiscard]] std::vector<ScenePrefabInstance> BuildInstances(
    Scene& scene,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::span<const kb::ecs::CommandEntity> commandEntities,
    const kb::ecs::CommandBufferPlaybackResult& playback,
    const ScenePrefabInstantiationSettings& settings,
    std::size_t instanceCount) {
    SceneState& state = SceneAccess::State(scene);
    std::vector<ScenePrefabInstance> instances;
    instances.reserve(instanceCount);

    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        std::vector<SceneObject> objects;
        objects.reserve(nodes.size());

        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const kb::ecs::Entity entity = playback.Resolve(commandEntities[EntityIndex(instanceIndex, nodeIndex, nodes.size())]);
            state.hierarchyOrder[entity.Id()] = state.nextHierarchyOrder++;
            const SceneEntity parent = nodes[nodeIndex].parentNode == ScenePrefabNodeDesc::NoParent
                ? settings.parent.Entity()
                : playback.Resolve(commandEntities[EntityIndex(instanceIndex, nodes[nodeIndex].parentNode, nodes.size())]);
            SceneHierarchyCache::Add(state, entity, parent);
            const std::string name = ScenePrefabNameResolver::Resolve(nodes[nodeIndex], settings);
            if (!name.empty()) {
                state.world.SetName(entity, name);
            }
            objects.push_back(SceneAccess::MakeObject(scene, entity));
        }

        instances.emplace_back(std::move(objects));
    }

    return instances;
}

} // namespace

std::vector<ScenePrefabInstance> ScenePrefabBulkInstantiationService::Instantiate(
    Scene& scene,
    const ScenePrefab& prefab,
    std::size_t count,
    const ScenePrefabInstantiationSettings& settings) {
    if (count == 0 || !ScenePrefabValidator::IsValid(prefab)) {
        return {};
    }

    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    const ScenePrefabBakedData baked = ScenePrefabBakedData::Bake(nodes);
    const std::size_t totalCount = TotalNodeCount(count, baked.NodeCount());
    if (totalCount == 0) {
        return {};
    }

    kb::ecs::CommandBuffer commandBuffer{ 1 };
    kb::ecs::CommandBuffer::WorkerBuffer worker = commandBuffer.Worker(0);
    std::vector<kb::ecs::CommandEntity> entities = CreateBakedEntities(worker, baked, count);
    QueueHierarchy(worker, std::span<const kb::ecs::CommandEntity>{ entities.data(), entities.size() }, nodes, count, settings.parent);

    kb::ecs::CommandBufferPlaybackResult playback = commandBuffer.Playback(SceneAccess::State(scene).world);
    return BuildInstances(scene, nodes, std::span<const kb::ecs::CommandEntity>{ entities.data(), entities.size() }, playback, settings, count);
}

} // namespace kb::scene
