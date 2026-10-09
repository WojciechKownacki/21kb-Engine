#include "scene/transform_edit/EditorSceneTransformEditController.hpp"

#include "engine/math/EngineMath.hpp"
#include "scene/transform_edit/EditorSceneTransformMath.hpp"
#include "scene/transform_edit/EditorTransformProperty.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

[[nodiscard]] kb::scene::Vec3 Difference(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept {
    return kb::scene::Vec3{
        lhs.x - rhs.x,
        lhs.y - rhs.y,
        lhs.z - rhs.z,
    };
}

[[nodiscard]] kb::scene::Vec3 RotationVector(kb::scene::Quat rotation) noexcept {
    return kb::scene::Vec3{ rotation.x, rotation.y, rotation.z };
}

// The change's transform with only `next` replacing it, at its starting translation.
[[nodiscard]] EditorSceneTransformTarget Unmoved(const EditorSceneObjectTransformChange& change, const kb::scene::TransformComponent& next) noexcept {
    return EditorSceneTransformTarget{ .transform = next, .translation = change.beforeTranslation };
}

// The change's transform at `translation`: below this distance from the origin a translation without a residual
// stays a float, so edits near the origin never start keeping residuals.
[[nodiscard]] EditorSceneTransformTarget MovedTo(const EditorSceneObjectTransformChange& change, const kb::math::DVec3& translation) noexcept {
    constexpr double kFloatTranslationLimit = 2048.0;
    const bool floatBefore = change.beforeTranslation == kb::math::ToDVec3(change.before.localPosition);
    const bool near = std::abs(translation.x) < kFloatTranslationLimit && std::abs(translation.y) < kFloatTranslationLimit &&
        std::abs(translation.z) < kFloatTranslationLimit;
    EditorSceneTransformTarget target{ .transform = change.before, .translation = translation };
    target.transform.localPosition = kb::math::ToVec3(translation);
    if (floatBefore && near) target.translation = kb::math::ToDVec3(target.transform.localPosition);
    return target;
}

} // namespace

EditorSceneTransformEditController::EditorSceneTransformEditController(
    kb::scene::Scene& scene,
    EditorSceneTransformEditSession& session) noexcept
    : scene_(scene)
    , session_(session) {}

EditorSceneTransformEditApplyResult EditorSceneTransformEditController::ApplyPrimaryPosition(kb::scene::Vec3 position) {
    if (!session_.Active()) {
        return {};
    }

    const kb::scene::Vec3 delta = Difference(position, session_.TargetStart());
    return EditorSceneTransformEditApplier::Apply(scene_, session_, [delta](const EditorSceneObjectTransformChange& change) {
        if (change.beforeTranslation == kb::math::ToDVec3(change.before.localPosition)) {
            kb::scene::TransformComponent next = change.before;
            next.localPosition = change.before.localPosition + delta;
            return EditorSceneTransformTarget{ .transform = next, .translation = kb::math::ToDVec3(next.localPosition) };
        }
        return MovedTo(change, change.beforeTranslation + delta);
    });
}

EditorSceneTransformEditApplyResult EditorSceneTransformEditController::ApplyPositionDelta(const kb::math::DVec3& delta) {
    if (!session_.Active()) {
        return {};
    }
    return EditorSceneTransformEditApplier::Apply(scene_, session_, [&delta](const EditorSceneObjectTransformChange& change) {
        return MovedTo(change, change.beforeTranslation + delta);
    });
}

EditorSceneTransformEditApplyResult EditorSceneTransformEditController::ApplyPrimaryRotation(kb::scene::Vec3 rotation) {
    if (!session_.Active()) {
        return {};
    }

    const EditorSceneObjectTransformChange* primaryChange = session_.PrimaryChange();
    if (primaryChange == nullptr) {
        return {};
    }

    const kb::scene::Vec3 delta = Difference(rotation, RotationVector(primaryChange->before.localRotation));
    return EditorSceneTransformEditApplier::Apply(scene_, session_, [delta](const EditorSceneObjectTransformChange& change) {
        kb::scene::TransformComponent next = change.before;
        next.localRotation.x = change.before.localRotation.x + delta.x;
        next.localRotation.y = change.before.localRotation.y + delta.y;
        next.localRotation.z = change.before.localRotation.z + delta.z;
        return Unmoved(change, next);
    });
}

EditorSceneTransformEditApplyResult EditorSceneTransformEditController::ApplyRotationDelta(kb::scene::Quat delta) {
    if (!session_.Active()) {
        return {};
    }

    const kb::scene::Quat normalizedDelta = EditorSceneTransformMath::Normalize(delta);
    return EditorSceneTransformEditApplier::Apply(scene_, session_, [normalizedDelta](const EditorSceneObjectTransformChange& change) {
        kb::scene::TransformComponent next = change.before;
        next.localRotation = EditorSceneTransformMath::Normalize(EditorSceneTransformMath::Multiply(normalizedDelta, change.before.localRotation));
        return Unmoved(change, next);
    });
}

EditorSceneTransformEditApplyResult EditorSceneTransformEditController::ApplyPrimaryScale(kb::scene::Vec3 scale) {
    if (!session_.Active()) {
        return {};
    }

    const EditorSceneObjectTransformChange* primaryChange = session_.PrimaryChange();
    if (primaryChange == nullptr) {
        return {};
    }

    const kb::scene::Vec3 delta = Difference(scale, primaryChange->before.localScale);
    return EditorSceneTransformEditApplier::Apply(scene_, session_, [delta](const EditorSceneObjectTransformChange& change) {
        kb::scene::TransformComponent next = change.before;
        next.localScale.x = std::max(0.01F, change.before.localScale.x + delta.x);
        next.localScale.y = std::max(0.01F, change.before.localScale.y + delta.y);
        next.localScale.z = std::max(0.01F, change.before.localScale.z + delta.z);
        return Unmoved(change, next);
    });
}

EditorSceneTransformEditApplyResult EditorSceneTransformEditController::ApplyProperty(InspectorPropertyId property, float value) {
    if (!session_.Active() || !EditorTransformProperty::IsTransform(property)) {
        return {};
    }

    switch (EditorTransformProperty::Group(property)) {
    case EditorTransformPropertyGroup::Position:
        return ApplyPrimaryPosition(EditorTransformProperty::WithAxis(session_.TargetStart(), property, value));
    case EditorTransformPropertyGroup::Rotation: {
        const EditorSceneObjectTransformChange* primaryChange = session_.PrimaryChange();
        if (primaryChange == nullptr) {
            return {};
        }
        return ApplyPrimaryRotation(EditorTransformProperty::WithAxis(RotationVector(primaryChange->before.localRotation), property, value));
    }
    case EditorTransformPropertyGroup::Scale: {
        const EditorSceneObjectTransformChange* primaryChange = session_.PrimaryChange();
        if (primaryChange == nullptr) {
            return {};
        }
        return ApplyPrimaryScale(EditorTransformProperty::WithAxis(primaryChange->before.localScale, property, value));
    }
    case EditorTransformPropertyGroup::None:
    default:
        break;
    }

    const EditorSceneObjectTransformChange* primaryChange = session_.PrimaryChange();
    if (primaryChange == nullptr) {
        return {};
    }

    const float delta = value - EditorTransformProperty::Read(primaryChange->before, property);
    return EditorSceneTransformEditApplier::Apply(scene_, session_, [property, delta](const EditorSceneObjectTransformChange& change) {
        kb::scene::TransformComponent next = change.before;
        EditorTransformProperty::Write(next, property, EditorTransformProperty::Read(change.before, property) + delta);
        return Unmoved(change, next);
    });
}

float EditorSceneTransformEditController::PropertyStart(const EditorSceneTransformEditSession& session, InspectorPropertyId property) noexcept {
    if (!session.Active() || !EditorTransformProperty::IsTransform(property)) {
        return 0.0F;
    }
    if (EditorTransformProperty::IsPosition(property)) {
        return EditorTransformProperty::ReadAxis(session.TargetStart(), property);
    }
    const EditorSceneObjectTransformChange* primaryChange = session.PrimaryChange();
    return primaryChange == nullptr ? 0.0F : EditorTransformProperty::Read(primaryChange->before, property);
}

} // namespace kb::editor
