#pragma once

#include "scene/prefab/io/ScenePrefabAssetReader.hpp"

#include <iosfwd>

namespace kb::scene {

class ScenePrefabAssetTemplateReader {
public:
    ScenePrefabAssetTemplateReader() = delete;

    [[nodiscard]] static bool ReadLegacy(std::istream& input, ScenePrefabAssetReadResult& result);
    [[nodiscard]] static bool ReadV2(std::istream& input, ScenePrefabAssetReadResult& result);
};

} // namespace kb::scene
