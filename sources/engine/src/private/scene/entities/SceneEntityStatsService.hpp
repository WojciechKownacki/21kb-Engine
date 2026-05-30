#pragma once

#include <cstddef>

namespace kb::scene {

class Scene;

class SceneEntityStatsService {
public:
    SceneEntityStatsService() = delete;

    [[nodiscard]] static std::size_t Count(const Scene& scene);
};

} // namespace kb::scene
