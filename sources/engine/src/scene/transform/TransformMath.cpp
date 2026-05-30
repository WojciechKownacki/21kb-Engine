#include "scene/transform/TransformMath.hpp"

#include "scene/transform/QuatMath.hpp"
#include "scene/transform/Vec3Math.hpp"

namespace kb::scene {

TransformComponent TransformMath::Identity() noexcept {
    return TransformComponent{
        .localScale = Vec3{ 1.0F, 1.0F, 1.0F },
        .worldScale = Vec3{ 1.0F, 1.0F, 1.0F },
        .worldDirty = false,
    };
}

TransformComponent TransformMath::Compose(const TransformComponent& parent, const TransformComponent& local) noexcept {
    TransformComponent result = local;
    result.worldScale = Vec3Math::Multiply(parent.worldScale, local.localScale);
    result.worldRotation = QuatMath::Normalize(QuatMath::Multiply(parent.worldRotation, local.localRotation));
    result.worldPosition = Vec3Math::Add(parent.worldPosition, Vec3Math::Rotate(parent.worldRotation, Vec3Math::Multiply(parent.worldScale, local.localPosition)));
    result.worldDirty = false;
    return result;
}

} // namespace kb::scene
