#pragma once

#include <string_view>

namespace kb::scene {

class ScenePrefabPropertyPath {
public:
    ScenePrefabPropertyPath() = delete;

    [[nodiscard]] static bool StartsWith(std::string_view value, std::string_view prefix) noexcept {
        return value.substr(0, prefix.size()) == prefix;
    }

    [[nodiscard]] static bool IsComponent(std::string_view propertyPath) noexcept {
        return StartsWith(propertyPath, "camera")
            || StartsWith(propertyPath, "meshRenderer")
            || StartsWith(propertyPath, "light")
            || StartsWith(propertyPath, "input")
            || StartsWith(propertyPath, "rigidbody")
            || StartsWith(propertyPath, "collider")
            || StartsWith(propertyPath, "tags")
            || StartsWith(propertyPath, "behaviour")
            || StartsWith(propertyPath, "audioSource")
            || StartsWith(propertyPath, "audioListener");
    }

    [[nodiscard]] static bool IsTransform(std::string_view propertyPath) noexcept {
        return StartsWith(propertyPath, "transform.");
    }
};

} // namespace kb::scene
