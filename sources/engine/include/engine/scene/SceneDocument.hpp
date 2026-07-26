#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

struct SceneDocument {
    // v4 (LIB-123): JointComponent is persisted with a stable prefab-node reference.
    // v3 (LIB-147): AudioSourceComponent gained the outputBus mixer-routing token.
    static constexpr std::uint32_t CurrentFileVersion = 4U;

    std::uint32_t fileVersion = CurrentFileVersion;
    std::string guid;
    std::string name = "Main";
    std::string worldType = "Editor";
    ScenePrefab worldPrefab;
};

} // namespace kb::scene
