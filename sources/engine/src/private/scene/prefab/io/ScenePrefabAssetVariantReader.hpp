#pragma once

#include "scene/prefab/io/ScenePrefabAssetReader.hpp"

#include <iosfwd>

namespace kb::scene {

class ScenePrefabAssetVariantReader {
public:
    ScenePrefabAssetVariantReader() = delete;

    [[nodiscard]] static bool ReadV2(std::istream& input, ScenePrefabAssetReadResult& result);
};

} // namespace kb::scene
