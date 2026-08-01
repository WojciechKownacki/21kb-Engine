#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

struct SceneDocument {
    // v9: VisibilityComponent persists explicit gate mode and render mask.
    // v8 (LIB-183): NavAgent and NavObstacle persist their authored policy.
    // v7 (LIB-173): UIDocumentComponent persists its document asset reference.
    // v6 (LIB-169): Animator persists its explicit root-motion owner.
    // v5 (LIB-167): Animator is a persisted scene/prefab component.
    // v4 (LIB-123): JointComponent is persisted with a stable prefab-node reference.
    // v3 (LIB-147): AudioSourceComponent gained the outputBus mixer-routing token.
    // v14: WorldBackdropComponent persists visible world background policy.
    // v15: AmbientRadianceComponent persists authored scene-global indirect lighting.
    // v16: SceneDetailSwitchComponent persists coordinated mesh LOD policy.
    // v17: SceneVisibilityBlockerComponent persists non-renderable culling geometry.
    // v18: VisibilityCellComponent persists region-membership visibility policy.
    // v19: SceneRegionPortalComponent persists stable source/target cell links.
    // v20: AuxFrameComponent persists secondary-camera output configuration.
    // v21: GeometrySwarmComponent persists compact GPU-instancing authoring data.
    // v24: SpaceStrokeComponent persists curve rendering policy.
    // v25: HistoryRibbonComponent persists renderer sampling policy.
    // v26: LensEchoComponent and 64-bit component-presence mask persist source-linked optical policy.
    // v27: scene-owned tag definitions persist independently from assignments.
    static constexpr std::uint32_t CurrentFileVersion = 27U;

    std::uint32_t fileVersion = CurrentFileVersion;
    std::string guid;
    std::string name = "Main";
    std::string worldType = "Editor";
    std::vector<std::string> tagDefinitions{ "Player", "Enemy", "Monster", "AI", "NPC", "Collision" };
    ScenePrefab worldPrefab;
};

} // namespace kb::scene
