#pragma once

#include "engine/scene/ScenePrefabNode.hpp"

#include <cstdint>

namespace kb::scene {

class ScenePrefabComponentHasher {
public:
    ScenePrefabComponentHasher() = delete;

    static void Mix(std::uint64_t& hash, const ScenePrefabNodeComponents& components) noexcept;
};

} // namespace kb::scene
