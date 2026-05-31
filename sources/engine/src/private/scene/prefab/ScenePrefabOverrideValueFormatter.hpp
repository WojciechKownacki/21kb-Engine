#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

class ScenePrefabOverrideValueFormatter {
public:
    ScenePrefabOverrideValueFormatter() = delete;

    [[nodiscard]] static bool Equal(Vec3 lhs, Vec3 rhs) noexcept;
    [[nodiscard]] static bool Equal(Quat lhs, Quat rhs) noexcept;
    [[nodiscard]] static std::string ToString(bool value);
    [[nodiscard]] static std::string ToString(std::uint64_t value);
    [[nodiscard]] static std::string ToString(float value);
    [[nodiscard]] static std::string ToString(Vec3 value);
    [[nodiscard]] static std::string ToString(Quat value);
};

} // namespace kb::scene
