#pragma once

#include "engine/scene/SceneTransforms.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

class ScenePrefabHashBuilder {
public:
    ScenePrefabHashBuilder() = delete;

    static void Mix(std::uint64_t& hash, std::uint64_t value) noexcept;
    static void MixString(std::uint64_t& hash, std::string_view value) noexcept;
    static void MixFloat(std::uint64_t& hash, float value) noexcept;
    static void MixVec3(std::uint64_t& hash, const Vec3& value) noexcept;
    static void MixQuat(std::uint64_t& hash, const Quat& value) noexcept;
    static void MixTransform(std::uint64_t& hash, const TransformComponent& transform) noexcept;
};

} // namespace kb::scene
