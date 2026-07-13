#include "scene/EcsRenderTransformResolver.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <cmath>

namespace kb::render {
namespace {

// LIB-042/LIB-043: kb::scene::Vec3/Quat are now aliases to kb::math::Vec3/
// Quat (see TransformComponent.hpp), which already provides Add
// (operator+), Dot, Cross, Normalize(Quat), quaternion composition
// (operator*), and Rotate — this file's own copies used to be a second
// definition (Rotate's formula is now also exactly kb::math::Rotate's)
// and would be ambiguous overloads via ADL against kb::math's. Only the
// component-wise Vec3*Vec3 Multiply has no kb::math equivalent, so it
// stays local.
using kb::math::Rotate;

[[nodiscard]] kb::scene::Vec3 Multiply(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept {
    return kb::scene::Vec3{ lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z };
}

} // namespace

EcsRenderTransformResolver::EcsRenderTransformResolver(const kb::scene::Scene& scene, TransformCache& cache, ResolvingSet& resolving) noexcept
    : scene_(scene)
    , cache_(cache)
    , resolving_(resolving) {}

kb::scene::TransformComponent EcsRenderTransformResolver::Resolve(kb::scene::SceneEntity entity) {
    if (!entity.IsValid()) {
        return Identity();
    }

    const auto cached = cache_.find(entity.Id());
    if (cached != cache_.end()) {
        return cached->second;
    }

    const kb::scene::TransformComponent* local = scene_.Transforms().TryGet(entity);
    if (local == nullptr) {
        return Identity();
    }

    if (!resolving_.insert(entity.Id()).second) {
        return *local;
    }

    kb::scene::TransformComponent parentTransform = Identity();
    const kb::scene::SceneEntity parent = scene_.Hierarchy().Parent(entity);
    if (parent.IsValid() && scene_.Entities().IsAlive(parent)) {
        parentTransform = Resolve(parent);
    }

    const kb::scene::TransformComponent resolved = Compose(parentTransform, *local);
    resolving_.erase(entity.Id());
    cache_[entity.Id()] = resolved;
    return resolved;
}

kb::scene::TransformComponent EcsRenderTransformResolver::Identity() noexcept {
    return kb::scene::TransformComponent{
        .localScale = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
        .worldScale = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
        .localVersion = 0,
        .parentVersion = 0,
        .worldVersion = 0,
        .worldDirty = false,
    };
}

kb::scene::TransformComponent EcsRenderTransformResolver::Compose(
    const kb::scene::TransformComponent& parent,
    const kb::scene::TransformComponent& local) noexcept {
    kb::scene::TransformComponent result = local;
    result.worldScale = Multiply(parent.worldScale, local.localScale);
    result.worldRotation = Normalize(parent.worldRotation * local.localRotation);
    result.worldPosition = parent.worldPosition + Rotate(parent.worldRotation, Multiply(parent.worldScale, local.localPosition));
    result.parentVersion = parent.worldVersion;
    result.worldVersion = local.worldVersion + 1U;
    result.worldDirty = false;
    return result;
}

} // namespace kb::render
