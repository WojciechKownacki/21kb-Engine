#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "scene/prefab/ScenePrefabNestedNodeMapping.hpp"

#include <cstdint>

namespace kb::scene {

class ScenePrefabOuterChildAppender {
public:
    ScenePrefabOuterChildAppender() = delete;

    static void Append(const ScenePrefab& source, ScenePrefab& output, ScenePrefabNestedNodeMapping& mapping, std::uint32_t outputParent);
};

} // namespace kb::scene
