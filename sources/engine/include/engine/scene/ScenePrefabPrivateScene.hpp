#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace kb::scene {

class ScenePrefabPrivateScene {
public:
    ScenePrefabPrivateScene() = default;
    ScenePrefabPrivateScene(Scene& sourceScene, ScenePrefabHandle sourcePrefab, std::unique_ptr<Scene> editScene, ScenePrefabHandle editPrefab, ScenePrefabInstance editInstance) noexcept;
    ~ScenePrefabPrivateScene();

    ScenePrefabPrivateScene(const ScenePrefabPrivateScene&) = delete;
    ScenePrefabPrivateScene& operator=(const ScenePrefabPrivateScene&) = delete;
    ScenePrefabPrivateScene(ScenePrefabPrivateScene&&) noexcept;
    ScenePrefabPrivateScene& operator=(ScenePrefabPrivateScene&&) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] Scene& EditScene() noexcept;
    [[nodiscard]] const Scene& EditScene() const noexcept;
    [[nodiscard]] ScenePrefabHandle SourcePrefab() const noexcept;
    [[nodiscard]] ScenePrefabHandle EditPrefab() const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle EditInstance() const noexcept;
    [[nodiscard]] std::size_t ObjectCount() const noexcept;
    [[nodiscard]] SceneObject ObjectAt(std::uint32_t nodeIndex) const noexcept;
    [[nodiscard]] SceneObject RootObject() const noexcept;
    [[nodiscard]] ScenePrefabOverrideReport Overrides() const;
    [[nodiscard]] bool Revert();
    [[nodiscard]] bool Apply();

private:
    Scene* sourceScene_ = nullptr;
    std::unique_ptr<Scene> editScene_;
    ScenePrefabHandle sourcePrefab_{};
    ScenePrefabHandle editPrefab_{};
    ScenePrefabInstance editInstance_{};
};

} // namespace kb::scene
