#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kb::scene {

class Scene;

class SceneHierarchyQueries {
public:
    explicit SceneHierarchyQueries(const Scene& scene) noexcept;

    [[nodiscard]] SceneEntity Parent(SceneEntity entity) const noexcept;
    [[nodiscard]] std::vector<SceneEntity> ChildEntities(SceneEntity entity) const;
    // LIB-087: unlike ChildEntities() above, neither of these allocates —
    // both index directly into the hierarchy's already-materialized child
    // storage (SceneHierarchyCache, incrementally maintained on every
    // reparent) rather than copying it into a fresh vector.
    [[nodiscard]] std::size_t ChildCount(SceneEntity entity) const noexcept;
    [[nodiscard]] SceneEntity ChildAt(SceneEntity entity, std::size_t index) const noexcept;
    [[nodiscard]] std::vector<SceneEntity> RootEntities() const;
    // The epoch changes when a topology or name edit can alter existing rows.
    // Fresh root appends preserve it, allowing an editor to append only those rows.
    [[nodiscard]] std::size_t RootCount() const noexcept;
    [[nodiscard]] SceneEntity RootAt(std::size_t index) const noexcept;
    [[nodiscard]] std::uint64_t RootAppendEpoch() const noexcept;

private:
    const Scene& scene_;
};

class SceneHierarchyAccess {
public:
    explicit SceneHierarchyAccess(Scene& scene) noexcept;

    [[nodiscard]] SceneObject Parent(SceneObject object);
    [[nodiscard]] SceneEntity Parent(SceneEntity entity) const noexcept;
    [[nodiscard]] std::vector<SceneObject> Children(SceneObject object);
    [[nodiscard]] std::vector<SceneEntity> ChildEntities(SceneEntity entity) const;
    [[nodiscard]] std::size_t ChildCount(SceneEntity entity) const noexcept;
    [[nodiscard]] SceneEntity ChildAt(SceneEntity entity, std::size_t index) const noexcept;
    [[nodiscard]] std::vector<SceneObject> RootObjects();
    [[nodiscard]] std::vector<SceneEntity> RootEntities() const;
    [[nodiscard]] std::size_t RootCount() const noexcept;
    [[nodiscard]] SceneEntity RootAt(std::size_t index) const noexcept;
    [[nodiscard]] std::uint64_t RootAppendEpoch() const noexcept;
    [[nodiscard]] bool SetParent(SceneObject child, SceneObject parent) noexcept;
    [[nodiscard]] bool SetParent(SceneEntity child, SceneEntity parent) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
