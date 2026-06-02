#include "scene/EcsRenderTransformResolver.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] kb::scene::Vec3 Add(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept {
    return kb::scene::Vec3{ lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

[[nodiscard]] kb::scene::Vec3 Multiply(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept {
    return kb::scene::Vec3{ lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z };
}

[[nodiscard]] float Dot(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] kb::scene::Vec3 Cross(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept {
    return kb::scene::Vec3{
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] kb::scene::Vec3 Scale(kb::scene::Vec3 value, float scale) noexcept {
    return kb::scene::Vec3{ value.x * scale, value.y * scale, value.z * scale };
}

[[nodiscard]] kb::scene::Quat Normalize(kb::scene::Quat value) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (lengthSquared <= 0.0F) {
        return kb::scene::Quat{};
    }

    const float invLength = 1.0F / std::sqrt(lengthSquared);
    return kb::scene::Quat{
        value.x * invLength,
        value.y * invLength,
        value.z * invLength,
        value.w * invLength,
    };
}

[[nodiscard]] kb::scene::Quat Multiply(kb::scene::Quat lhs, kb::scene::Quat rhs) noexcept {
    return kb::scene::Quat{
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    };
}

[[nodiscard]] kb::scene::Vec3 Rotate(kb::scene::Quat rotation, kb::scene::Vec3 value) noexcept {
    const kb::scene::Vec3 u{ rotation.x, rotation.y, rotation.z };
    const float s = rotation.w;
    return Add(
        Add(Scale(u, 2.0F * Dot(u, value)), Scale(value, s * s - Dot(u, u))),
        Scale(Cross(u, value), 2.0F * s));
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
        .worldDirty = false,
    };
}

kb::scene::TransformComponent EcsRenderTransformResolver::Compose(
    const kb::scene::TransformComponent& parent,
    const kb::scene::TransformComponent& local) noexcept {
    kb::scene::TransformComponent result = local;
    result.worldScale = Multiply(parent.worldScale, local.localScale);
    result.worldRotation = Normalize(Multiply(parent.worldRotation, local.localRotation));
    result.worldPosition = Add(parent.worldPosition, Rotate(parent.worldRotation, Multiply(parent.worldScale, local.localPosition)));
    result.worldDirty = false;
    return result;
}

} // namespace kb::render
