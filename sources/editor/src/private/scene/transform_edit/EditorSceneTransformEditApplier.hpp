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

// What an edit sets on an entity: its transform and its local translation in double precision (whose float view
// is transform.localPosition).
struct EditorSceneTransformTarget {
    kb::scene::TransformComponent transform{};
    kb::math::DVec3 translation{};
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

            const EditorSceneTransformTarget next = buildNext(change);
            change.after = next.transform;
            change.afterTranslation = next.translation;
            if (!Write(scene, change.entity, next.transform, next.translation)) {
                continue;
            }
            result.touched.push_back(change.entity);
            result.changed = true;
        }
        return result;
    }

    [[nodiscard]] static EditorSceneTransformEditApplyResult RestoreBefore(
        kb::scene::Scene& scene,
        std::span<const EditorSceneObjectTransformChange> changes);
    // Sets the transform and its double-precision local translation; false when the entity already had both.
    static bool Write(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform,
        const kb::math::DVec3& translation);
};

} // namespace kb::editor
