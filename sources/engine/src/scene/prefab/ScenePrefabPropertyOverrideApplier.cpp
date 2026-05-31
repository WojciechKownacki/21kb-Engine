#include "scene/prefab/ScenePrefabPropertyOverrideApplier.hpp"

#include <sstream>
#include <string_view>

namespace kb::scene {
namespace {

[[nodiscard]] bool ParseVec3(std::string_view value, Vec3& output) {
    std::istringstream stream{ std::string{ value } };
    std::string extra;
    return static_cast<bool>(stream >> output.x >> output.y >> output.z) && !(stream >> extra);
}

[[nodiscard]] bool ParseQuat(std::string_view value, Quat& output) {
    std::istringstream stream{ std::string{ value } };
    std::string extra;
    return static_cast<bool>(stream >> output.x >> output.y >> output.z >> output.w) && !(stream >> extra);
}

[[nodiscard]] bool ParseBool(std::string_view value, bool& output) noexcept {
    if (value == "true" || value == "1") {
        output = true;
        return true;
    }
    if (value == "false" || value == "0") {
        output = false;
        return true;
    }
    return false;
}

} // namespace

bool ScenePrefabPropertyOverrideApplier::Apply(ScenePrefabNodeDesc& node, const ScenePrefabPropertyOverride& property) {
    if (property.propertyPath == "name") {
        node.name = property.value;
        return true;
    }
    if (property.propertyPath == "transform.localPosition") {
        return ParseVec3(property.value, node.transform.localPosition);
    }
    if (property.propertyPath == "transform.localRotation") {
        return ParseQuat(property.value, node.transform.localRotation);
    }
    if (property.propertyPath == "transform.localScale") {
        return ParseVec3(property.value, node.transform.localScale);
    }
    if (property.propertyPath == "visibility.visible") {
        return ParseBool(property.value, node.visibility.visible);
    }
    return false;
}

} // namespace kb::scene
