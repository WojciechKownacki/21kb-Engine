#include "scene/prefab/ScenePrefabBulkInstantiationService.hpp"

#include "engine/ecs/CommandBuffer.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityNaming.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/prefab/ScenePrefabBakedData.hpp"
#include "scene/prefab/ScenePrefabNameResolver.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

using PrefabStatsClock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t ElapsedNanoseconds(PrefabStatsClock::time_point start, PrefabStatsClock::time_point end) noexcept {
    const std::uint64_t nanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    return nanoseconds == 0U ? 1U : nanoseconds;
}

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
    output.resize(totalCount);
    if (source.empty()) {
        return;
    }

    std::copy(source.begin(), source.end(), output.begin());
    std::size_t filled = source.size();
    while (filled < totalCount) {
        const std::size_t copyCount = std::min(filled, totalCount - filled);
        std::copy_n(output.begin(), copyCount, output.begin() + static_cast<std::ptrdiff_t>(filled));
        filled += copyCount;
    }
}

template <typename T>
void AddCommandComponentView(std::vector<kb::ecs::CommandBuffer::BulkComponentView>& views, std::span<const T> components) {
    views.push_back(kb::ecs::CommandBuffer::MakeBulkComponentView<T>(components));
}

template <typename T>
void AddCommandComponentPatternView(std::vector<kb::ecs::CommandBuffer::BulkComponentView>& views, std::span<const T> components, std::size_t instanceCount) {
    kb::ecs::CommandBuffer::BulkComponentView view = kb::ecs::CommandBuffer::MakeBulkComponentView<T>(components);
    view.componentCount = TotalNodeCount(instanceCount, components.size());
    view.sourceCount = components.size();
    views.push_back(view);
}

template <typename T>
void AddWorldComponentView(std::vector<kb::ecs::World::BulkComponentView>& views, std::span<const T> components) {
    views.push_back(kb::ecs::World::MakeBulkComponentView<T>(components));
}

template <typename T>
void AddWorldComponentPatternView(std::vector<kb::ecs::World::BulkComponentView>& views, std::span<const T> components, std::size_t instanceCount) {
    kb::ecs::World::BulkComponentView view = kb::ecs::World::MakeBulkComponentView<T>(components);
    view.componentCount = TotalNodeCount(instanceCount, components.size());
    view.sourceCount = components.size();
    views.push_back(view);
}

template <typename T>
void AddComponentViews(
    std::vector<kb::ecs::CommandBuffer::BulkComponentView>& commandViews,
    std::vector<kb::ecs::World::BulkComponentView>& worldViews,
    std::span<const T> components) {
    AddCommandComponentView(commandViews, components);
    AddWorldComponentView(worldViews, components);
}

[[nodiscard]] std::vector<std::string> BuildNodeNames(
    std::span<const ScenePrefabNodeDesc> nodes,
    const ScenePrefabInstantiationSettings& settings) {
    std::vector<std::string> names;
    names.reserve(nodes.size());
    for (const ScenePrefabNodeDesc& node : nodes) {
        names.push_back(ScenePrefabNameResolver::Resolve(node, settings));
    }
    return names;
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
    std::vector<kb::ecs::World::BulkComponentView> worldViews;

    void Build(const ScenePrefabBakedArchetype& archetype, std::size_t instanceCount) {
        RepeatComponents(transforms, std::span<const TransformComponent>{ archetype.transforms }, instanceCount);
        RepeatComponents(visibility, std::span<const VisibilityComponent>{ archetype.visibility }, instanceCount);

        views.clear();
        views.reserve(12U);
        worldViews.clear();
        worldViews.reserve(12U);
        AddComponentViews(views, worldViews, std::span<const TransformComponent>{ transforms });
        AddComponentViews(views, worldViews, std::span<const VisibilityComponent>{ visibility });

        const std::uint16_t mask = archetype.componentMask;
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Camera)) {
            RepeatComponents(cameras, std::span<const CameraComponent>{ archetype.cameras }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const CameraComponent>{ cameras });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::MeshRenderer)) {
            RepeatComponents(meshRenderers, std::span<const MeshRendererComponent>{ archetype.meshRenderers }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const MeshRendererComponent>{ meshRenderers });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Light)) {
            RepeatComponents(lights, std::span<const LightComponent>{ archetype.lights }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const LightComponent>{ lights });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Input)) {
            RepeatComponents(inputs, std::span<const InputComponent>{ archetype.inputs }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const InputComponent>{ inputs });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Rigidbody)) {
            RepeatComponents(rigidbodies, std::span<const RigidbodyComponent>{ archetype.rigidbodies }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const RigidbodyComponent>{ rigidbodies });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Collider)) {
            RepeatComponents(colliders, std::span<const ColliderComponent>{ archetype.colliders }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const ColliderComponent>{ colliders });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Tags)) {
            RepeatComponents(tags, std::span<const TagsComponent>{ archetype.tags }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const TagsComponent>{ tags });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Behaviour)) {
            RepeatComponents(behaviours, std::span<const BehaviourComponent>{ archetype.behaviours }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const BehaviourComponent>{ behaviours });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AudioSource)) {
            RepeatComponents(audioSources, std::span<const AudioSourceComponent>{ archetype.audioSources }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const AudioSourceComponent>{ audioSources });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AudioListener)) {
            RepeatComponents(audioListeners, std::span<const AudioListenerComponent>{ archetype.audioListeners }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const AudioListenerComponent>{ audioListeners });
        }
    }

    void BuildPattern(const ScenePrefabBakedArchetype& archetype, std::size_t instanceCount) {
        views.clear();
        views.reserve(12U);
        worldViews.clear();
        worldViews.reserve(12U);
        AddCommandComponentPatternView(views, std::span<const TransformComponent>{ archetype.transforms }, instanceCount);
        AddWorldComponentPatternView(worldViews, std::span<const TransformComponent>{ archetype.transforms }, instanceCount);
        AddCommandComponentPatternView(views, std::span<const VisibilityComponent>{ archetype.visibility }, instanceCount);
        AddWorldComponentPatternView(worldViews, std::span<const VisibilityComponent>{ archetype.visibility }, instanceCount);

        const std::uint16_t mask = archetype.componentMask;
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Camera)) {
            AddCommandComponentPatternView(views, std::span<const CameraComponent>{ archetype.cameras }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const CameraComponent>{ archetype.cameras }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::MeshRenderer)) {
            AddCommandComponentPatternView(views, std::span<const MeshRendererComponent>{ archetype.meshRenderers }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const MeshRendererComponent>{ archetype.meshRenderers }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Light)) {
            AddCommandComponentPatternView(views, std::span<const LightComponent>{ archetype.lights }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const LightComponent>{ archetype.lights }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Input)) {
            AddCommandComponentPatternView(views, std::span<const InputComponent>{ archetype.inputs }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const InputComponent>{ archetype.inputs }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Rigidbody)) {
            AddCommandComponentPatternView(views, std::span<const RigidbodyComponent>{ archetype.rigidbodies }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const RigidbodyComponent>{ archetype.rigidbodies }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Collider)) {
            AddCommandComponentPatternView(views, std::span<const ColliderComponent>{ archetype.colliders }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const ColliderComponent>{ archetype.colliders }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Tags)) {
            AddCommandComponentPatternView(views, std::span<const TagsComponent>{ archetype.tags }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const TagsComponent>{ archetype.tags }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Behaviour)) {
            AddCommandComponentPatternView(views, std::span<const BehaviourComponent>{ archetype.behaviours }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const BehaviourComponent>{ archetype.behaviours }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AudioSource)) {
            AddCommandComponentPatternView(views, std::span<const AudioSourceComponent>{ archetype.audioSources }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const AudioSourceComponent>{ archetype.audioSources }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AudioListener)) {
            AddCommandComponentPatternView(views, std::span<const AudioListenerComponent>{ archetype.audioListeners }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const AudioListenerComponent>{ archetype.audioListeners }, instanceCount);
        }
    }
};

void AddPrefabHierarchyDense(
    SceneState& state,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::span<const SceneEntity> entities,
    const ScenePrefabInstantiationSettings& settings,
    std::size_t instanceCount) {
    if (nodes.empty() || entities.empty()) {
        return;
    }

    std::uint32_t maxEntityIndex = 0;
    bool hasDenseEntity = false;
    for (SceneEntity entity : entities) {
        const std::uint32_t index = kb::ecs::GeneratedEntityIndex(entity);
        if (index != kb::ecs::kInvalidGeneratedEntityIndex) {
            maxEntityIndex = hasDenseEntity ? std::max(maxEntityIndex, index) : index;
            hasDenseEntity = true;
        }
    }
    if (hasDenseEntity) {
        const std::size_t required = static_cast<std::size_t>(maxEntityIndex) + 1U;
        if (state.denseHierarchyParents.size() < required) {
            state.denseHierarchyParents.resize(required);
        }
        if (state.denseHierarchyChildren.size() < required) {
            state.denseHierarchyChildren.resize(required);
        }
        if (state.denseHierarchyOrder.size() < required) {
            state.denseHierarchyOrder.resize(required);
        }
    }

    std::vector<std::size_t>& childrenPerNode = state.prefabHierarchyChildrenPerNodeScratch;
    childrenPerNode.assign(nodes.size(), 0U);
    std::size_t rootCount = 0;
    for (const ScenePrefabNodeDesc& node : nodes) {
        if (node.parentNode == ScenePrefabNodeDesc::NoParent) {
            ++rootCount;
        } else if (node.parentNode < childrenPerNode.size()) {
            ++childrenPerNode[node.parentNode];
        }
    }
    state.hierarchyRoots.reserve(state.hierarchyRoots.size() + (settings.parent.Entity().IsValid() ? 0U : rootCount * instanceCount));
    if (settings.parent.Entity().IsValid()) {
        const std::uint32_t parentIndex = kb::ecs::GeneratedEntityIndex(settings.parent.Entity());
        if (parentIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
            if (state.denseHierarchyChildren.size() <= parentIndex) {
                state.denseHierarchyChildren.resize(static_cast<std::size_t>(parentIndex) + 1U);
            }
            state.denseHierarchyChildren[parentIndex].reserve(state.denseHierarchyChildren[parentIndex].size() + rootCount * instanceCount);
        } else {
            state.hierarchyChildren[settings.parent.Entity().Id()].reserve(state.hierarchyChildren[settings.parent.Entity().Id()].size() + rootCount * instanceCount);
        }
    }

    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            if (childrenPerNode[nodeIndex] == 0U) {
                continue;
            }
            const SceneEntity parent = entities[EntityIndex(instanceIndex, nodeIndex, nodes.size())];
            const std::uint32_t parentIndex = kb::ecs::GeneratedEntityIndex(parent);
            if (parentIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                state.denseHierarchyChildren[parentIndex].reserve(childrenPerNode[nodeIndex]);
            }
        }
    }

    SceneHierarchyCache::AssignDenseOrderRange(state, entities);
    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const SceneEntity entity = entities[EntityIndex(instanceIndex, nodeIndex, nodes.size())];
            const SceneEntity parent = nodes[nodeIndex].parentNode == ScenePrefabNodeDesc::NoParent
                ? settings.parent.Entity()
                : entities[EntityIndex(instanceIndex, nodes[nodeIndex].parentNode, nodes.size())];
            const std::uint32_t entityIndex = kb::ecs::GeneratedEntityIndex(entity);
            if (entityIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                state.denseHierarchyParents[entityIndex] = parent;
            } else {
                state.hierarchyParents.emplace(entity.Id(), parent);
            }

            if (!parent.IsValid()) {
                state.hierarchyRoots.push_back(entity);
                continue;
            }
            const std::uint32_t parentIndex = kb::ecs::GeneratedEntityIndex(parent);
            if (parentIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                state.denseHierarchyChildren[parentIndex].push_back(entity);
            } else {
                state.hierarchyChildren[parent.Id()].push_back(entity);
            }
        }
    }
    ++state.hierarchyTopologyVersion;
}

void QueueHierarchy(
    kb::ecs::CommandBuffer::WorkerBuffer& worker,
    std::span<const kb::ecs::CommandEntity> entities,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::size_t instanceCount,
    SceneObject rootParent) {
    std::vector<kb::ecs::CommandEntity> children;
    std::vector<kb::ecs::CommandEntity> parents;
    children.reserve(TotalNodeCount(instanceCount, nodes.size()));
    parents.reserve(TotalNodeCount(instanceCount, nodes.size()));

    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const ScenePrefabNodeDesc& node = nodes[nodeIndex];
            const std::size_t childIndex = EntityIndex(instanceIndex, nodeIndex, nodes.size());
            if (node.parentNode == ScenePrefabNodeDesc::NoParent) {
                if (rootParent.EntityHandle().IsValid()) {
                    children.push_back(entities[childIndex]);
                    parents.push_back(kb::ecs::CommandEntity::Existing(rootParent.Entity()));
                }
                continue;
            }
            children.push_back(entities[childIndex]);
            parents.push_back(entities[EntityIndex(instanceIndex, node.parentNode, nodes.size())]);
        }
    }

    worker.SetParentsForNewEntitiesKnownAcyclic(std::span<const kb::ecs::CommandEntity>{ children }, std::span<const kb::ecs::CommandEntity>{ parents });
}

[[nodiscard]] std::vector<kb::ecs::CommandEntity> CreateBakedEntities(
    kb::ecs::CommandBuffer& commandBuffer,
    const ScenePrefabBakedData& baked,
    std::size_t instanceCount,
    std::vector<ScenePrefabArchetypeSpawnPayload>& payloads) {
    std::vector<kb::ecs::CommandEntity> entities(TotalNodeCount(instanceCount, baked.NodeCount()));
    payloads.clear();
    payloads.reserve(baked.Archetypes().size());

    std::size_t archetypeIndex = 0U;
    for (const ScenePrefabBakedArchetype& archetype : baked.Archetypes()) {
        ScenePrefabArchetypeSpawnPayload& payload = payloads.emplace_back();
        kb::ecs::CommandBuffer::WorkerBuffer worker = commandBuffer.Worker(archetypeIndex);
        const std::size_t archetypeNodeCount = archetype.nodeIndices.size();
        const std::size_t archetypeEntityCount = TotalNodeCount(instanceCount, archetypeNodeCount);
        payload.BuildPattern(archetype, instanceCount);

        std::vector<kb::ecs::CommandEntity> created = worker.CreateEntitiesBorrowed(
            archetypeEntityCount,
            std::span<const kb::ecs::CommandBuffer::BulkComponentView>{ payload.views });
        for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
            for (std::size_t archetypeNodeIndex = 0; archetypeNodeIndex < archetypeNodeCount; ++archetypeNodeIndex) {
                const std::size_t createdIndex = EntityIndex(instanceIndex, archetypeNodeIndex, archetypeNodeCount);
                const std::size_t prefabIndex = EntityIndex(instanceIndex, archetype.nodeIndices[archetypeNodeIndex], baked.NodeCount());
                entities[prefabIndex] = created[createdIndex];
            }
        }
        ++archetypeIndex;
    }

    return entities;
}

[[nodiscard]] std::size_t ComponentBytesCopied(std::span<const ScenePrefabArchetypeSpawnPayload> payloads) noexcept {
    std::size_t bytes = 0;
    for (const ScenePrefabArchetypeSpawnPayload& payload : payloads) {
        for (const kb::ecs::World::BulkComponentView& component : payload.worldViews) {
            bytes += component.componentSize * component.componentCount;
        }
    }
    return bytes;
}

[[nodiscard]] std::size_t ComponentSourceBytesRead(std::span<const ScenePrefabArchetypeSpawnPayload> payloads) noexcept {
    std::size_t bytes = 0;
    for (const ScenePrefabArchetypeSpawnPayload& payload : payloads) {
        for (const kb::ecs::World::BulkComponentView& component : payload.worldViews) {
            const std::size_t sourceCount = component.sourceCount == 0U ? component.componentCount : component.sourceCount;
            bytes += component.componentSize * sourceCount;
        }
    }
    return bytes;
}

[[nodiscard]] std::vector<SceneEntity> CreateBakedEntitiesDirect(
    kb::ecs::World& world,
    const ScenePrefabBakedData& baked,
    std::size_t instanceCount,
    std::vector<ScenePrefabArchetypeSpawnPayload>& payloads,
    bool nativeOnly) {
    payloads.clear();
    payloads.reserve(baked.Archetypes().size());
    const std::span<const ScenePrefabBakedArchetype> archetypes = baked.Archetypes();
    std::vector<SceneEntity> entities(TotalNodeCount(instanceCount, baked.NodeCount()));

    for (const ScenePrefabBakedArchetype& archetype : archetypes) {
        ScenePrefabArchetypeSpawnPayload& payload = payloads.emplace_back();
        const std::size_t archetypeNodeCount = archetype.nodeIndices.size();
        const std::size_t archetypeEntityCount = TotalNodeCount(instanceCount, archetypeNodeCount);
        payload.BuildPattern(archetype, instanceCount);

        const std::vector<kb::ecs::Entity> created = nativeOnly
            ? world.CreateEntitiesNativeOnly(archetypeEntityCount, std::span<const kb::ecs::World::BulkComponentView>{ payload.worldViews })
            : world.CreateEntities(archetypeEntityCount, std::span<const kb::ecs::World::BulkComponentView>{ payload.worldViews });
        if (created.size() != archetypeEntityCount) {
            throw std::runtime_error("Scene prefab direct bulk spawn created an unexpected entity count");
        }

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
    std::span<const SceneEntity> resolvedEntities,
    const ScenePrefabInstantiationSettings& settings,
    std::size_t instanceCount,
    bool collectInstances,
    std::uint64_t& hierarchyRecordNanoseconds,
    std::uint64_t& nameAssignmentNanoseconds) {
    SceneState& state = SceneAccess::State(scene);
    if (!collectInstances) {
        const auto hierarchyStart = PrefabStatsClock::now();
        AddPrefabHierarchyDense(state, nodes, resolvedEntities, settings, instanceCount);
        hierarchyRecordNanoseconds = ElapsedNanoseconds(hierarchyStart, PrefabStatsClock::now());
        if (settings.assignNames) {
            const std::vector<std::string> nodeNames = BuildNodeNames(nodes, settings);
            const auto nameStart = PrefabStatsClock::now();
            SceneEntityNaming::SetRepeatedNames(state, resolvedEntities, std::span<const std::string>{ nodeNames });
            nameAssignmentNanoseconds = ElapsedNanoseconds(nameStart, PrefabStatsClock::now());
        }
        return {};
    }

    std::vector<ScenePrefabInstance> instances;
    instances.reserve(instanceCount);

    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        std::vector<SceneObject> objects;
        objects.reserve(nodes.size());

        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const std::size_t entityIndex = EntityIndex(instanceIndex, nodeIndex, nodes.size());
            const kb::ecs::Entity entity = resolvedEntities[entityIndex];
            objects.push_back(SceneAccess::MakeObject(scene, entity));
        }

        instances.emplace_back(std::move(objects));
    }

    const auto hierarchyStart = PrefabStatsClock::now();
    AddPrefabHierarchyDense(state, nodes, resolvedEntities, settings, instanceCount);
    hierarchyRecordNanoseconds = ElapsedNanoseconds(hierarchyStart, PrefabStatsClock::now());
    if (settings.assignNames) {
        const std::vector<std::string> nodeNames = BuildNodeNames(nodes, settings);
        const auto nameStart = PrefabStatsClock::now();
        SceneEntityNaming::SetRepeatedNames(state, resolvedEntities, std::span<const std::string>{ nodeNames });
        nameAssignmentNanoseconds = ElapsedNanoseconds(nameStart, PrefabStatsClock::now());
    }
    return instances;
}

[[nodiscard]] std::vector<ScenePrefabInstance> BuildInstances(
    Scene& scene,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::span<const kb::ecs::CommandEntity> commandEntities,
    const kb::ecs::CommandBufferPlaybackResult& playback,
    const ScenePrefabInstantiationSettings& settings,
    std::size_t instanceCount,
    bool collectInstances,
    std::uint64_t& hierarchyRecordNanoseconds,
    std::uint64_t& nameAssignmentNanoseconds) {
    std::vector<SceneEntity> resolvedEntities(commandEntities.size());
    for (std::size_t index = 0; index < commandEntities.size(); ++index) {
        resolvedEntities[index] = playback.Resolve(commandEntities[index]);
    }
    return BuildInstances(
        scene,
        nodes,
        std::span<const SceneEntity>{ resolvedEntities },
        settings,
        instanceCount,
        collectInstances,
        hierarchyRecordNanoseconds,
        nameAssignmentNanoseconds);
}

[[nodiscard]] std::vector<ScenePrefabInstance> InstantiateInternal(
    Scene& scene,
    const ScenePrefab& prefab,
    std::size_t count,
    const ScenePrefabInstantiationSettings& settings,
    bool collectInstances) {
    SceneState& state = SceneAccess::State(scene);
    state.lastPrefabInstantiationStats = ScenePrefabInstantiationStats{
        .requestedInstances = count,
    };
    if (count == 0 || !ScenePrefabValidator::IsValid(prefab)) {
        return {};
    }

    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    const ScenePrefabBakedData baked = ScenePrefabBakedData::Bake(nodes);
    const std::size_t totalCount = TotalNodeCount(count, baked.NodeCount());
    if (totalCount == 0) {
        return {};
    }

    const kb::ecs::NativeEcsStorageStats beforeStorage = state.world.NativeStorageStats();
    if (!settings.syncWorldHierarchy) {
        std::vector<ScenePrefabArchetypeSpawnPayload> spawnPayloads;
        constexpr bool nativeOnlyBatch = true;
        const auto createStart = PrefabStatsClock::now();
        const std::vector<SceneEntity> entities = CreateBakedEntitiesDirect(state.world, baked, count, spawnPayloads, nativeOnlyBatch);
        const std::uint64_t entityCreateNanoseconds = ElapsedNanoseconds(createStart, PrefabStatsClock::now());
        const kb::ecs::NativeEcsStorageStats afterStorage = state.world.NativeStorageStats();
        std::uint64_t hierarchyRecordNanoseconds = 0;
        std::uint64_t nameAssignmentNanoseconds = 0;
        const std::vector<ScenePrefabInstance> instances =
            BuildInstances(scene, nodes, std::span<const SceneEntity>{ entities }, settings, count, collectInstances, hierarchyRecordNanoseconds, nameAssignmentNanoseconds);

        state.lastPrefabInstantiationStats = ScenePrefabInstantiationStats{
            .requestedInstances = count,
            .instantiatedInstances = collectInstances ? instances.size() : count,
            .nodesPerInstance = baked.NodeCount(),
            .entitiesCreated = entities.size(),
            .prefabArchetypesTouched = baked.Archetypes().size(),
            .bulkCreateCommands = baked.Archetypes().size(),
            .componentSetCommands = 0,
            .parentCommands = 0,
            .componentBytesCopied = ComponentBytesCopied(std::span<const ScenePrefabArchetypeSpawnPayload>{ spawnPayloads }),
            .componentSourceBytesRead = ComponentSourceBytesRead(std::span<const ScenePrefabArchetypeSpawnPayload>{ spawnPayloads }),
            .chunksAllocatedDelta = afterStorage.chunkPoolAllocated >= beforeStorage.chunkPoolAllocated ? afterStorage.chunkPoolAllocated - beforeStorage.chunkPoolAllocated : 0U,
            .chunksReusedDelta = afterStorage.chunkPoolReuseCount >= beforeStorage.chunkPoolReuseCount ? afterStorage.chunkPoolReuseCount - beforeStorage.chunkPoolReuseCount : 0U,
            .entityCreateNanoseconds = entityCreateNanoseconds,
            .hierarchyRecordNanoseconds = hierarchyRecordNanoseconds,
            .nameAssignmentNanoseconds = nameAssignmentNanoseconds,
        };
        return instances;
    }

    const std::size_t hierarchyLane = baked.Archetypes().size();
    const std::size_t commandLaneCount = hierarchyLane + 1U;
    kb::ecs::CommandBuffer commandBuffer{ commandLaneCount };
    std::vector<ScenePrefabArchetypeSpawnPayload> spawnPayloads;
    const auto createStart = PrefabStatsClock::now();
    const auto commandBuildStart = PrefabStatsClock::now();
    std::vector<kb::ecs::CommandEntity> entities = CreateBakedEntities(commandBuffer, baked, count, spawnPayloads);
    kb::ecs::CommandBuffer::WorkerBuffer hierarchyWorker = commandBuffer.Worker(hierarchyLane);
    QueueHierarchy(hierarchyWorker, std::span<const kb::ecs::CommandEntity>{ entities.data(), entities.size() }, nodes, count, settings.parent);
    const std::uint64_t commandBuildNanoseconds = ElapsedNanoseconds(commandBuildStart, PrefabStatsClock::now());

    const auto playbackStart = PrefabStatsClock::now();
    kb::ecs::CommandBufferPlaybackResult playback = commandBuffer.Playback(state.world);
    const std::uint64_t commandPlaybackNanoseconds = ElapsedNanoseconds(playbackStart, PrefabStatsClock::now());
    const std::uint64_t entityCreateNanoseconds = ElapsedNanoseconds(createStart, PrefabStatsClock::now());
    const kb::ecs::NativeEcsStorageStats afterStorage = state.world.NativeStorageStats();
    std::uint64_t hierarchyRecordNanoseconds = 0;
    std::uint64_t nameAssignmentNanoseconds = 0;
    const std::vector<ScenePrefabInstance> instances =
        BuildInstances(scene, nodes, std::span<const kb::ecs::CommandEntity>{ entities.data(), entities.size() }, playback, settings, count, collectInstances, hierarchyRecordNanoseconds, nameAssignmentNanoseconds);

    const kb::ecs::CommandBufferPlaybackResult::Stats& playbackStats = playback.PlaybackStats();
    state.lastPrefabInstantiationStats = ScenePrefabInstantiationStats{
        .requestedInstances = count,
        .instantiatedInstances = collectInstances ? instances.size() : count,
        .nodesPerInstance = baked.NodeCount(),
        .entitiesCreated = playback.CreatedCount(),
        .prefabArchetypesTouched = baked.Archetypes().size(),
        .bulkCreateCommands = playbackStats.bulkCreateCommands,
        .componentSetCommands = playbackStats.componentSetCommands,
        .parentCommands = playbackStats.parentCommands,
        .componentBytesCopied = playbackStats.componentBytesCopied,
        .componentSourceBytesRead = ComponentSourceBytesRead(std::span<const ScenePrefabArchetypeSpawnPayload>{ spawnPayloads }),
        .chunksAllocatedDelta = afterStorage.chunkPoolAllocated >= beforeStorage.chunkPoolAllocated ? afterStorage.chunkPoolAllocated - beforeStorage.chunkPoolAllocated : 0U,
        .chunksReusedDelta = afterStorage.chunkPoolReuseCount >= beforeStorage.chunkPoolReuseCount ? afterStorage.chunkPoolReuseCount - beforeStorage.chunkPoolReuseCount : 0U,
        .entityCreateNanoseconds = entityCreateNanoseconds,
        .commandBuildNanoseconds = commandBuildNanoseconds,
        .commandPlaybackNanoseconds = commandPlaybackNanoseconds,
        .commandPlaybackCreateNanoseconds = playbackStats.createPhaseNanoseconds,
        .commandPlaybackApplyNanoseconds = playbackStats.applyPhaseNanoseconds,
        .commandPlaybackParentNanoseconds = playbackStats.parentApplyNanoseconds,
        .commandPlaybackDestroyNanoseconds = playbackStats.destroyPhaseNanoseconds,
        .hierarchyRecordNanoseconds = hierarchyRecordNanoseconds,
        .nameAssignmentNanoseconds = nameAssignmentNanoseconds,
    };
    return instances;
}

} // namespace

std::vector<ScenePrefabInstance> ScenePrefabBulkInstantiationService::Instantiate(
    Scene& scene,
    const ScenePrefab& prefab,
    std::size_t count,
    const ScenePrefabInstantiationSettings& settings) {
    return InstantiateInternal(scene, prefab, count, settings, true);
}

ScenePrefabInstantiationStats ScenePrefabBulkInstantiationService::InstantiateBatch(
    Scene& scene,
    const ScenePrefab& prefab,
    std::size_t count,
    const ScenePrefabInstantiationSettings& settings) {
    static_cast<void>(InstantiateInternal(scene, prefab, count, settings, false));
    return SceneAccess::State(scene).lastPrefabInstantiationStats;
}

} // namespace kb::scene
