#pragma once

#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/SceneObject.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace kb::scene {

class ScenePrefabInstance {
public:
    ScenePrefabInstance() = default;
    explicit ScenePrefabInstance(std::vector<SceneObject> objects) noexcept;
    explicit ScenePrefabInstance(std::shared_ptr<const std::vector<SceneObject>> objects) noexcept;
    ScenePrefabInstance(ScenePrefabInstanceHandle handle, std::vector<SceneObject> objects) noexcept;
    ScenePrefabInstance(ScenePrefabInstanceHandle handle, std::shared_ptr<const std::vector<SceneObject>> objects) noexcept;

    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle Handle() const noexcept;
    [[nodiscard]] std::size_t ObjectCount() const noexcept;
    [[nodiscard]] std::span<const SceneObject> Objects() const noexcept;
    [[nodiscard]] std::vector<SceneObject> TakeObjects() noexcept;
    [[nodiscard]] SceneObject ObjectAt(std::uint32_t nodeIndex) const noexcept;
    [[nodiscard]] SceneObject RootObject() const noexcept;
    [[nodiscard]] std::shared_ptr<const std::vector<SceneObject>> SharedObjects() const;
    void AssignHandle(ScenePrefabInstanceHandle handle) noexcept;

private:
    [[nodiscard]] std::span<const SceneObject> ActiveObjects() const noexcept;

    ScenePrefabInstanceHandle handle_{};
    std::vector<SceneObject> objects_;
    std::shared_ptr<const std::vector<SceneObject>> sharedObjects_;
};

} // namespace kb::scene
