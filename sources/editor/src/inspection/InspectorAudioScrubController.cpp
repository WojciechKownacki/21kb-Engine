#include "inspection/InspectorAudioScrubController.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "inspection/InspectorAudioComponentModel.hpp"

#include <cmath>
#include <limits>

namespace kb::editor {

bool InspectorAudioScrubController::Prepare(
    const kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    InspectorPropertyId property,
    InspectorAudioScrubState& state) noexcept {
    Reset(state);
    if (!scene.Entities().IsAlive(entity)) {
        return false;
    }
    if (InspectorAudioComponentModel::IsIntegerProperty(property)) {
        if (!InspectorAudioComponentModel::ReadInteger(scene, entity, property, state.startInteger)) {
            return false;
        }
        state.integer = true;
    } else if (!InspectorAudioComponentModel::IsFloatProperty(property)
        || !InspectorAudioComponentModel::ReadFloat(scene, entity, property, state.startFloat)) {
        return false;
    }
    state.entity = entity;
    state.property = property;
    state.active = true;
    return true;
}

InspectorAudioScrubUpdate InspectorAudioScrubController::Update(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity selectedEntity,
    std::int64_t pixelDelta,
    InspectorAudioScrubState& state) noexcept {
    if (!state.active || selectedEntity != state.entity || !scene.Entities().IsAlive(state.entity)) {
        return InspectorAudioScrubUpdate::LostTarget;
    }
    if (state.integer) {
        const std::int64_t delta = static_cast<std::int64_t>(std::llround(static_cast<double>(pixelDelta) / 6.0));
        if ((delta > 0 && state.startInteger > std::numeric_limits<std::int64_t>::max() - delta)
            || (delta < 0 && state.startInteger < std::numeric_limits<std::int64_t>::min() - delta)) {
            return InspectorAudioScrubUpdate::Invalid;
        }
        const std::int64_t candidate = state.startInteger + delta;
        std::int64_t current = 0;
        if (!InspectorAudioComponentModel::ReadInteger(scene, state.entity, state.property, current)) {
            return InspectorAudioScrubUpdate::LostTarget;
        }
        if (candidate == current) {
            return InspectorAudioScrubUpdate::Unchanged;
        }
        return InspectorAudioComponentModel::ApplyInteger(scene, state.entity, state.property, candidate)
            ? InspectorAudioScrubUpdate::Changed
            : InspectorAudioScrubUpdate::Invalid;
    }

    const float delta = std::round(static_cast<float>(pixelDelta) / 6.0F) * 0.1F;
    const float candidate = state.startFloat + delta;
    float current = 0.0F;
    if (!InspectorAudioComponentModel::ReadFloat(scene, state.entity, state.property, current)) {
        return InspectorAudioScrubUpdate::LostTarget;
    }
    if (candidate == current) {
        return InspectorAudioScrubUpdate::Unchanged;
    }
    return InspectorAudioComponentModel::ApplyFloat(scene, state.entity, state.property, candidate)
        ? InspectorAudioScrubUpdate::Changed
        : InspectorAudioScrubUpdate::Invalid;
}

bool InspectorAudioScrubController::HasNetChange(
    const kb::scene::Scene& scene,
    kb::scene::SceneEntity selectedEntity,
    const InspectorAudioScrubState& state) noexcept {
    if (!state.active || selectedEntity != state.entity || !scene.Entities().IsAlive(state.entity)) {
        return false;
    }
    if (state.integer) {
        std::int64_t current = 0;
        return InspectorAudioComponentModel::ReadInteger(scene, state.entity, state.property, current)
            && current != state.startInteger;
    }
    float current = 0.0F;
    return InspectorAudioComponentModel::ReadFloat(scene, state.entity, state.property, current)
        && current != state.startFloat;
}

void InspectorAudioScrubController::Reset(InspectorAudioScrubState& state) noexcept {
    state = {};
}

} // namespace kb::editor
