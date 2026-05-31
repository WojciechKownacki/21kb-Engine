#pragma once

#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstddef>
#include <iosfwd>
#include <vector>

namespace kb::scene {

class ScenePrefabAssetOverrideReader {
public:
    ScenePrefabAssetOverrideReader() = delete;

    [[nodiscard]] static bool Read(std::istream& input, std::size_t overrideCount, std::vector<ScenePrefabPropertyOverride>& output);
};

} // namespace kb::scene
