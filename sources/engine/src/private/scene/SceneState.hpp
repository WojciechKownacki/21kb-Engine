#pragma once

#include "engine/assets/AssetManager.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/World.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorage.hpp"
#include "scene/history/SceneHistoryStack.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"
#include "scene/prefab/ScenePrefabRegistry.hpp"
#include "scene/systems/SceneSystemScheduler.hpp"

#include <cstdint>
#include <unordered_map>

namespace kb::scene {

class SceneState {
public:
    SceneState();
    ~SceneState();

    SceneState(const SceneState&) = delete;
    SceneState& operator=(const SceneState&) = delete;
    SceneState(SceneState&&) = delete;
    SceneState& operator=(SceneState&&) = delete;

    kb::ecs::World world;
    SceneComponentRegistry components;
    SceneComponentStorage componentStorage;
    ScenePrefabRegistry prefabs;
    ScenePrefabInstanceRegistry prefabInstances;
    kb::assets::AssetManager assets;
    kb::input::InputSubsystem inputSubsystem;
    SceneHistoryStack undoHistory;
    SceneHistoryStack redoHistory;
    kb::ecs::SystemScheduler systemScheduler;
    SceneSystemScheduler sceneSystemScheduler;
    std::unordered_map<SceneEntity::IdType, std::uint64_t> hierarchyOrder;
    std::uint64_t nextHierarchyOrder = 1;
};

} // namespace kb::scene
