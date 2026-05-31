#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

class Scene;
struct ScenePrefabInstanceRecord;
struct ScenePrefabRecord;
class ScenePrefabRegistry;

class ScenePrefabVariantOverrideService {
public:
    ScenePrefabVariantOverrideService() = delete;

    [[nodiscard]] static bool ApplyAll(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, ScenePrefabRecord& variantRecord);
    [[nodiscard]] static bool ApplyProperty(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, std::uint32_t nodeIndex, SceneObject object, std::string_view propertyPath);
};

} // namespace kb::scene
