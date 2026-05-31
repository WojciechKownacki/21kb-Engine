#pragma once

#include "engine/scene/ScenePrefabNode.hpp"

#include <iosfwd>

namespace kb::scene {

class ScenePrefabAssetComponentWriter {
public:
    ScenePrefabAssetComponentWriter() = delete;

    static void Write(std::ostream& output, const ScenePrefabNodeComponents& components);
};

} // namespace kb::scene
