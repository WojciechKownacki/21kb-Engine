#include "scene/prefab/ScenePrefabOverrideValueFormatter.hpp"

#include <sstream>

namespace kb::scene {

bool ScenePrefabOverrideValueFormatter::Equal(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool ScenePrefabOverrideValueFormatter::Equal(Quat lhs, Quat rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

std::string ScenePrefabOverrideValueFormatter::ToString(bool value) {
    return value ? "true" : "false";
}

std::string ScenePrefabOverrideValueFormatter::ToString(std::uint64_t value) {
    return std::to_string(value);
}

std::string ScenePrefabOverrideValueFormatter::ToString(float value) {
    std::ostringstream output;
    output << value;
    return output.str();
}

std::string ScenePrefabOverrideValueFormatter::ToString(Vec3 value) {
    std::ostringstream output;
    output << value.x << ' ' << value.y << ' ' << value.z;
    return output.str();
}

std::string ScenePrefabOverrideValueFormatter::ToString(Quat value) {
    std::ostringstream output;
    output << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w;
    return output.str();
}

} // namespace kb::scene
