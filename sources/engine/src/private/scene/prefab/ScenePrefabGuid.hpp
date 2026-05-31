#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::scene {

class ScenePrefabGuid {
public:
    ScenePrefabGuid() = delete;

    [[nodiscard]] static std::string Create(std::string_view name, const ScenePrefab& prefab, std::uint64_t localId);
};

} // namespace kb::scene
