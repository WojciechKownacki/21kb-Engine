#pragma once

#include "scene/prefab/io/ScenePrefabAssetWriter.hpp"

#include <iosfwd>

namespace kb::scene {

class ScenePrefabAssetVariantWriter {
public:
    ScenePrefabAssetVariantWriter() = delete;

    [[nodiscard]] static bool CanWrite(const ScenePrefabAssetWriteDesc& asset);
    static void WriteBody(std::ostream& output, const ScenePrefabAssetWriteDesc& asset);
};

} // namespace kb::scene
