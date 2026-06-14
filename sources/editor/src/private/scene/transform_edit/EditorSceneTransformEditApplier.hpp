#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/transform_edit/EditorSceneTransformChange.hpp"
#include "scene/transform_edit/EditorSceneTransformEditSession.hpp"
#include "scene/transform_edit/EditorSceneTransformEquality.hpp"

#include <span>
#include <vector>

namespace kb::editor {

struct EditorSceneTransformEditApplyResult {
    bool changed = false;
    std::vector<kb::scene::SceneEntity> touched;
};

class EditorSceneTransformEditApplier {
public:
    EditorSceneTransformEditApplier() = delete;

    template <typename NextTransformBuilder>
    [[nodiscard]] static EditorSceneTransformEditApplyResult Apply(
        kb::scene::Scene& scene,
        EditorSceneTransformEditSession& session,
        NextTransformBuilder&& buildNext) {
        EditorSceneTransformEditApplyResult result{};
        if (!session.Active()) {
            return result;
        }

        std::vector<EditorSceneObjectTransformChange>& changes = session.Changes();
        result.touched.reserve(changes.size());
        for (EditorSceneObjectTransformChange& change : changes) {
            if (!scene.Entities().IsAlive(change.entity)) {
                continue;
            }

            const kb::scene::TransformComponent next = buildNext(change);
            change.after = next;

            const kb::scene::TransformComponent current = scene.Transforms().Get(change.entity);
            if (EditorSceneTransformEquality::Same(current, next)) {
                continue;
            }

            scene.Transforms().Set(change.entity, next);
            result.touched.push_back(change.entity);
            result.changed = true;
        }
        return result;
    }

    [[nodiscard]] static EditorSceneTransformEditApplyResult RestoreBefore(
        kb::scene::Scene& scene,
        std::span<const EditorSceneObjectTransformChange> changes);
};

} // namespace kb::editor
