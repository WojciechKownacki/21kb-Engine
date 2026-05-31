#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstddef>
#include <iosfwd>

namespace kb::scene {

class ScenePrefabAssetNodeListReader {
public:
    ScenePrefabAssetNodeListReader() = delete;

    [[nodiscard]] static bool Read(std::istream& input, std::size_t nodeCount, ScenePrefab& prefab);
};

} // namespace kb::scene
