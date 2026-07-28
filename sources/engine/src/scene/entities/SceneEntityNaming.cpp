#include "scene/entities/SceneEntityNaming.hpp"

#include "ecs/FlecsEntityIds.hpp"
#include "scene/SceneState.hpp"

#include <flecs.h>

#include <cstddef>
#include <stdexcept>

namespace kb::scene {
namespace {

[[nodiscard]] std::uint32_t DenseIndex(SceneEntity entity) noexcept {
    return kb::ecs::GeneratedEntityIndex(entity);
}

void StoreCachedName(SceneState& state, SceneEntity entity, std::string_view name) {
    if (name.empty()) {
        SceneEntityNaming::ClearName(state, entity);
        return;
    }

    const std::uint32_t denseIndex = DenseIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
        const std::size_t required = static_cast<std::size_t>(denseIndex) + 1U;
        if (state.denseEntityNames.size() < required) {
            state.denseEntityNames.resize(required);
        }
        state.denseEntityNames[denseIndex] = std::string{ name };
        return;
    }
    state.entityNames[entity.Id()] = std::string{ name };
}

void SetBackendName(kb::ecs::World& world, SceneEntity entity, std::string_view name) {
    if (!world.BackendEntityAlive(entity)) {
        return;
    }
    const std::string ownedName{ name };
    ecs_set_name(world.NativeHandle(), kb::ecs::FlecsEntityId(entity), ownedName.empty() ? nullptr : ownedName.c_str());
}

void MarkNameTopologyDirty(SceneState& state) noexcept {
    ++state.hierarchyTopologyVersion;
    if (state.hierarchyTopologyVersion == 0U) {
        state.hierarchyTopologyVersion = 1U;
    }
}

} // namespace

std::string SceneEntityNaming::Name(const SceneState& state, SceneEntity entity) {
    const std::uint32_t denseIndex = DenseIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseEntityNames.size()) {
        const std::string& name = state.denseEntityNames[denseIndex];
        if (!name.empty()) {
            return name;
        }
    }

    const auto found = state.entityNames.find(entity.Id());
    if (found != state.entityNames.end()) {
        return found->second;
    }
    return state.world.Name(entity);
}

void SceneEntityNaming::SetName(SceneState& state, SceneEntity entity, std::string_view name) {
    StoreCachedName(state, entity, name);
    SetBackendName(state.world, entity, name);
    MarkNameTopologyDirty(state);
}

void SceneEntityNaming::SetNames(SceneState& state, std::span<const SceneEntity> entities, std::span<const std::string> names) {
    if (entities.size() != names.size()) {
        throw std::invalid_argument("Scene entity bulk naming requires matching entity and name counts");
    }
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const std::string& name = names[index];
        if (!name.empty()) {
            StoreCachedName(state, entities[index], name);
            SetBackendName(state.world, entities[index], name);
        }
    }
    if (!entities.empty()) {
        MarkNameTopologyDirty(state);
    }
}

void SceneEntityNaming::SetRepeatedNames(SceneState& state, std::span<const SceneEntity> entities, std::span<const std::string> namesByNode) {
    if (!entities.empty() && namesByNode.empty()) {
        throw std::invalid_argument("Scene entity repeated naming requires at least one node name");
    }
    if (!namesByNode.empty() && (entities.size() % namesByNode.size()) != 0U) {
        throw std::invalid_argument("Scene entity repeated naming requires complete node name cycles");
    }

    std::uint32_t maxDenseIndex = kb::ecs::kInvalidGeneratedEntityIndex;
    bool denseOnly = true;
    for (SceneEntity entity : entities) {
        const std::uint32_t denseIndex = DenseIndex(entity);
        if (denseIndex == kb::ecs::kInvalidGeneratedEntityIndex) {
            denseOnly = false;
            break;
        }
        maxDenseIndex = maxDenseIndex == kb::ecs::kInvalidGeneratedEntityIndex || denseIndex > maxDenseIndex ? denseIndex : maxDenseIndex;
    }
    if (denseOnly && maxDenseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
        const std::size_t required = static_cast<std::size_t>(maxDenseIndex) + 1U;
        if (state.denseEntityNames.size() < required) {
            state.denseEntityNames.resize(required);
        }
        for (std::size_t index = 0; index < entities.size(); ++index) {
            const std::string& name = namesByNode[index % namesByNode.size()];
            if (!name.empty()) {
                state.denseEntityNames[DenseIndex(entities[index])] = name;
            }
        }
        MarkNameTopologyDirty(state);
        return;
    }

    for (std::size_t index = 0; index < entities.size(); ++index) {
        const std::string& name = namesByNode[index % namesByNode.size()];
        if (!name.empty()) {
            StoreCachedName(state, entities[index], name);
        }
    }
    if (!entities.empty()) {
        MarkNameTopologyDirty(state);
    }
}

void SceneEntityNaming::SetRepeatedNamesForCreatedDenseEntities(
    SceneState& state,
    std::span<const SceneEntity> entities,
    std::span<const std::string> namesByNode,
    std::uint32_t maxDenseIndex) {
    if (!entities.empty() && namesByNode.empty()) {
        throw std::invalid_argument("Scene entity repeated naming requires at least one node name");
    }
    if (!namesByNode.empty() && (entities.size() % namesByNode.size()) != 0U) {
        throw std::invalid_argument("Scene entity repeated naming requires complete node name cycles");
    }
    if (entities.empty()) {
        return;
    }
    if (maxDenseIndex == kb::ecs::kInvalidGeneratedEntityIndex) {
        SetRepeatedNames(state, entities, namesByNode);
        return;
    }

    const std::size_t required = static_cast<std::size_t>(maxDenseIndex) + 1U;
    if (state.denseEntityNames.size() < required) {
        state.denseEntityNames.resize(required);
    }

    const std::size_t nodeCount = namesByNode.size();
    for (std::size_t baseIndex = 0; baseIndex < entities.size(); baseIndex += nodeCount) {
        for (std::size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
            const std::string& name = namesByNode[nodeIndex];
            if (name.empty()) {
                continue;
            }
            const std::uint32_t denseIndex = DenseIndex(entities[baseIndex + nodeIndex]);
            if (denseIndex == kb::ecs::kInvalidGeneratedEntityIndex || denseIndex > maxDenseIndex) {
                SetRepeatedNames(state, entities, namesByNode);
                return;
            }
            state.denseEntityNames[denseIndex] = name;
        }
    }
    MarkNameTopologyDirty(state);
}

void SceneEntityNaming::ClearName(SceneState& state, SceneEntity entity) noexcept {
    MarkNameTopologyDirty(state);
    const std::uint32_t denseIndex = DenseIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseEntityNames.size()) {
        state.denseEntityNames[denseIndex].clear();
        return;
    }
    state.entityNames.erase(entity.Id());
}

} // namespace kb::scene
