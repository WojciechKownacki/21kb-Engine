#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::scene {

class ScenePrefabOverrideValueFormatter {
public:
    ScenePrefabOverrideValueFormatter() = delete;

    [[nodiscard]] static bool Equal(Vec3 lhs, Vec3 rhs) noexcept;
    [[nodiscard]] static bool Equal(Quat lhs, Quat rhs) noexcept;
    [[nodiscard]] static bool Equal(const kb::math::DVec3& lhs, const kb::math::DVec3& rhs) noexcept;
    [[nodiscard]] static std::string ToString(bool value);
    [[nodiscard]] static std::string ToString(std::uint64_t value);
    [[nodiscard]] static std::string ToString(float value);
    [[nodiscard]] static std::string ToString(Vec3 value);
    [[nodiscard]] static std::string ToString(Quat value);
    // A translation as the shortest text that reads back as exactly the same doubles.
    [[nodiscard]] static std::string ToString(const kb::math::DVec3& value);
    // Reads three decimal numbers (ToString(DVec3)'s text, or the float text older files hold).
    [[nodiscard]] static bool Parse(std::string_view text, kb::math::DVec3& output);
};

} // namespace kb::scene
