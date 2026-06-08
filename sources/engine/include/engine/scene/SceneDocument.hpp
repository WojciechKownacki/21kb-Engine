#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

struct SceneDocument {
    static constexpr std::uint32_t CurrentFileVersion = 1U;

    std::uint32_t fileVersion = CurrentFileVersion;
    std::string guid;
    std::string name = "Main";
    std::string worldType = "Editor";
    ScenePrefab worldPrefab;
};

} // namespace kb::scene
