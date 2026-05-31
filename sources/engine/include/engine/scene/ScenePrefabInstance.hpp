#pragma once

#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/SceneObject.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kb::scene {

class ScenePrefabInstance {
public:
    ScenePrefabInstance() = default;
    explicit ScenePrefabInstance(std::vector<SceneObject> objects) noexcept;
    ScenePrefabInstance(ScenePrefabInstanceHandle handle, std::vector<SceneObject> objects) noexcept;

    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle Handle() const noexcept;
    [[nodiscard]] std::size_t ObjectCount() const noexcept;
    [[nodiscard]] std::span<const SceneObject> Objects() const noexcept;
    [[nodiscard]] SceneObject ObjectAt(std::uint32_t nodeIndex) const noexcept;
    [[nodiscard]] SceneObject RootObject() const noexcept;

private:
    ScenePrefabInstanceHandle handle_{};
    std::vector<SceneObject> objects_;
};

} // namespace kb::scene
