#include "scene/transform_edit/EditorSceneTransformEditController.hpp"

#include "engine/math/EngineMath.hpp"
#include "scene/transform_edit/EditorSceneTransformMath.hpp"
#include "scene/transform_edit/EditorTransformProperty.hpp"

#include <algorithm>

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
        kb::scene::TransformComponent next = change.before;
        next.localPosition = change.before.localPosition + delta;
        return next;
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
        return next;
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
        return next;
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
        return next;
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
        return next;
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
