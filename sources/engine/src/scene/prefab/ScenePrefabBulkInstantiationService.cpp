#include "scene/prefab/ScenePrefabBulkInstantiationService.hpp"

#include "engine/ecs/CommandBuffer.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/prefab/ScenePrefabNameResolver.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

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

template <typename T, typename Reader>
void QueueOptionalComponents(
    kb::ecs::CommandBuffer::WorkerBuffer& worker,
    std::span<const kb::ecs::CommandEntity> entities,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::size_t instanceCount,
    Reader reader) {
    std::vector<kb::ecs::CommandEntity> targets;
    std::vector<T> values;
    targets.reserve(entities.size());
    values.reserve(entities.size());

    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const std::optional<T>& component = reader(nodes[nodeIndex].components);
            if (!component.has_value()) {
                continue;
            }
            targets.push_back(entities[EntityIndex(instanceIndex, nodeIndex, nodes.size())]);
            values.push_back(*component);
        }
    }

    if (!targets.empty()) {
        worker.Set(std::span<const kb::ecs::CommandEntity>{ targets.data(), targets.size() }, std::span<const T>{ values.data(), values.size() });
    }
}

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

void QueuePrefabComponents(
    kb::ecs::CommandBuffer::WorkerBuffer& worker,
    std::span<const kb::ecs::CommandEntity> entities,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::size_t instanceCount) {
    QueueOptionalComponents<CameraComponent>(worker, entities, nodes, instanceCount, [](const ScenePrefabNodeComponents& components) -> const std::optional<CameraComponent>& {
        return components.camera;
    });
    QueueOptionalComponents<MeshRendererComponent>(worker, entities, nodes, instanceCount, [](const ScenePrefabNodeComponents& components) -> const std::optional<MeshRendererComponent>& {
        return components.meshRenderer;
    });
    QueueOptionalComponents<LightComponent>(worker, entities, nodes, instanceCount, [](const ScenePrefabNodeComponents& components) -> const std::optional<LightComponent>& {
        return components.light;
    });
    QueueOptionalComponents<InputComponent>(worker, entities, nodes, instanceCount, [](const ScenePrefabNodeComponents& components) -> const std::optional<InputComponent>& {
        return components.input;
    });
    QueueOptionalComponents<RigidbodyComponent>(worker, entities, nodes, instanceCount, [](const ScenePrefabNodeComponents& components) -> const std::optional<RigidbodyComponent>& {
        return components.rigidbody;
    });
    QueueOptionalComponents<ColliderComponent>(worker, entities, nodes, instanceCount, [](const ScenePrefabNodeComponents& components) -> const std::optional<ColliderComponent>& {
        return components.collider;
    });
    QueueOptionalComponents<TagsComponent>(worker, entities, nodes, instanceCount, [](const ScenePrefabNodeComponents& components) -> const std::optional<TagsComponent>& {
        return components.tags;
    });
    QueueOptionalComponents<BehaviourComponent>(worker, entities, nodes, instanceCount, [](const ScenePrefabNodeComponents& components) -> const std::optional<BehaviourComponent>& {
        return components.behaviour;
    });
    QueueOptionalComponents<AudioSourceComponent>(worker, entities, nodes, instanceCount, [](const ScenePrefabNodeComponents& components) -> const std::optional<AudioSourceComponent>& {
        return components.audioSource;
    });
    QueueOptionalComponents<AudioListenerComponent>(worker, entities, nodes, instanceCount, [](const ScenePrefabNodeComponents& components) -> const std::optional<AudioListenerComponent>& {
        return components.audioListener;
    });
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
    const std::size_t totalCount = TotalNodeCount(count, nodes.size());
    if (totalCount == 0) {
        return {};
    }

    std::vector<TransformComponent> transforms;
    std::vector<VisibilityComponent> visibility;
    transforms.reserve(totalCount);
    visibility.reserve(totalCount);

    for (std::size_t instanceIndex = 0; instanceIndex < count; ++instanceIndex) {
        static_cast<void>(instanceIndex);
        for (const ScenePrefabNodeDesc& node : nodes) {
            TransformComponent transform = node.transform;
            transform.worldDirty = true;
            transforms.push_back(transform);
            visibility.push_back(node.visibility);
        }
    }

    kb::ecs::CommandBuffer commandBuffer{ 1 };
    kb::ecs::CommandBuffer::WorkerBuffer worker = commandBuffer.Worker(0);
    std::vector<kb::ecs::CommandEntity> entities = worker.CreateEntities(
        std::span<const TransformComponent>{ transforms.data(), transforms.size() },
        std::span<const VisibilityComponent>{ visibility.data(), visibility.size() });
    QueuePrefabComponents(worker, std::span<const kb::ecs::CommandEntity>{ entities.data(), entities.size() }, nodes, count);
    QueueHierarchy(worker, std::span<const kb::ecs::CommandEntity>{ entities.data(), entities.size() }, nodes, count, settings.parent);

    kb::ecs::CommandBufferPlaybackResult playback = commandBuffer.Playback(SceneAccess::State(scene).world);
    return BuildInstances(scene, nodes, std::span<const kb::ecs::CommandEntity>{ entities.data(), entities.size() }, playback, settings, count);
}

} // namespace kb::scene
