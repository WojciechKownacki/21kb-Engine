#include "scene/prefab/ScenePrefabAppliedPropertyBuilder.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <string>

namespace kb::scene {
namespace {

[[nodiscard]] bool StartsWith(std::string_view value, std::string_view prefix) noexcept {
    return value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::string ToString(bool value) {
    return value ? "true" : "false";
}

[[nodiscard]] std::string ToString(Vec3 value) {
    return std::to_string(value.x) + " " + std::to_string(value.y) + " " + std::to_string(value.z);
}

[[nodiscard]] std::string ToString(Quat value) {
    return std::to_string(value.x) + " " + std::to_string(value.y) + " " + std::to_string(value.z) + " " + std::to_string(value.w);
}

[[nodiscard]] ScenePrefabOverrideFlag FlagForProperty(std::string_view propertyPath) noexcept {
    if (propertyPath == "name") {
        return ScenePrefabOverrideFlag::Name;
    }
    if (propertyPath == "parent") {
        return ScenePrefabOverrideFlag::Parent;
    }
    if (StartsWith(propertyPath, "transform.")) {
        return ScenePrefabOverrideFlag::Transform;
    }
    if (propertyPath == "visibility.visible") {
        return ScenePrefabOverrideFlag::Visibility;
    }
    if (StartsWith(propertyPath, "camera")) {
        return ScenePrefabOverrideFlag::Camera;
    }
    if (StartsWith(propertyPath, "meshRenderer")) {
        return ScenePrefabOverrideFlag::MeshRenderer;
    }
    if (StartsWith(propertyPath, "light")) {
        return ScenePrefabOverrideFlag::Light;
    }
    if (propertyPath == "children") {
        return ScenePrefabOverrideFlag::AddedChild;
    }
    return ScenePrefabOverrideFlag::None;
}

} // namespace

bool ScenePrefabAppliedPropertyBuilder::Build(Scene& scene, std::uint32_t nodeIndex, SceneObject object, std::string_view propertyPath, ScenePrefabPropertyOverride& property) {
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        return false;
    }

    property = ScenePrefabPropertyOverride{
        .nodeIndex = nodeIndex,
        .target = object,
        .propertyPath = std::string{ propertyPath },
        .flag = FlagForProperty(propertyPath),
    };
    if (propertyPath == "name") {
        property.value = scene.Entities().Name(object);
        return true;
    }
    if (StartsWith(propertyPath, "transform.")) {
        const TransformComponent transform = scene.Transforms().Get(object);
        if (propertyPath == "transform.localPosition") {
            property.value = ToString(transform.localPosition);
            return true;
        }
        if (propertyPath == "transform.localRotation") {
            property.value = ToString(transform.localRotation);
            return true;
        }
        if (propertyPath == "transform.localScale") {
            property.value = ToString(transform.localScale);
            return true;
        }
    }
    if (propertyPath == "visibility.visible") {
        property.value = ToString(scene.Components().Visibility().Get(object.Entity()).visible);
        return true;
    }
    return false;
}

} // namespace kb::scene
