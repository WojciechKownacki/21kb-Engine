#pragma once

#include "engine/scene/ScenePrefabNode.hpp"

#include <iosfwd>

namespace kb::scene {

class ScenePrefabAssetFieldWriter {
public:
    ScenePrefabAssetFieldWriter() = delete;

    static void WriteNode(std::ostream& output, const ScenePrefabNodeDesc& node);
};

} // namespace kb::scene
