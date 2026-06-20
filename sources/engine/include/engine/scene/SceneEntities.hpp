#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

class SceneEntityQueries {
public:
    explicit SceneEntityQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool IsAlive(SceneObject object) const noexcept;
    [[nodiscard]] bool IsAlive(SceneEntity entity) const noexcept;
    [[nodiscard]] std::string Name(SceneObject object) const;
    [[nodiscard]] std::string Name(SceneEntity entity) const;
    [[nodiscard]] std::size_t Count() const;

private:
    const Scene& scene_;
};

class SceneEntities {
public:
    explicit SceneEntities(Scene& scene) noexcept;

    [[nodiscard]] SceneObject CreateObject();
    [[nodiscard]] SceneObject CreateObject(SceneObjectDesc desc);
    // Bulk spawn: creates one object per descriptor in a single call. Structural
    // changes batch through the world's version-based query-plan invalidation, so
    // the query cache rebuilds lazily once rather than per spawned object.
    [[nodiscard]] std::vector<SceneObject> CreateObjects(std::span<const SceneObjectDesc> descs);
    [[nodiscard]] SceneEntity CreateEntity();
    [[nodiscard]] SceneEntity CreateEntity(SceneObjectDesc desc);
    [[nodiscard]] SceneObject Duplicate(SceneObject object);
    [[nodiscard]] std::vector<SceneObject> Duplicate(std::span<const SceneObject> objects);
    void Destroy(SceneObject object) noexcept;
    void Destroy(SceneEntity entity) noexcept;
    void Destroy(std::span<const SceneObject> objects) noexcept;
    [[nodiscard]] bool SetParent(std::span<const SceneObject> objects, SceneObject parent) noexcept;

    [[nodiscard]] bool IsAlive(SceneObject object) const noexcept;
    [[nodiscard]] bool IsAlive(SceneEntity entity) const noexcept;
    [[nodiscard]] SceneObject Object(SceneEntity entity) const noexcept;
    [[nodiscard]] std::string Name(SceneObject object) const;
    [[nodiscard]] std::string Name(SceneEntity entity) const;
    void SetName(SceneObject object, std::string_view name);
    void SetName(SceneEntity entity, std::string_view name);
    [[nodiscard]] std::size_t Count() const;

private:
    Scene& scene_;
};

} // namespace kb::scene
