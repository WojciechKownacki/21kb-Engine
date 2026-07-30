#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

struct SceneDocument {
    // v9: VisibilityComponent persists explicit gate mode and render mask.
    // v8 (LIB-183): NavAgent and NavObstacle persist their authored policy.
    // v7 (LIB-173): UIDocumentComponent persists its document asset reference.
    // v6 (LIB-169): Animator persists its explicit root-motion owner.
    // v5 (LIB-167): Animator is a persisted scene/prefab component.
    // v4 (LIB-123): JointComponent is persisted with a stable prefab-node reference.
    // v3 (LIB-147): AudioSourceComponent gained the outputBus mixer-routing token.
    static constexpr std::uint32_t CurrentFileVersion = 9U;

    std::uint32_t fileVersion = CurrentFileVersion;
    std::string guid;
    std::string name = "Main";
    std::string worldType = "Editor";
    ScenePrefab worldPrefab;
};

} // namespace kb::scene
