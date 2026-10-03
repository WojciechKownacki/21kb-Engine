#pragma once

#include "engine/ecs/Query.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <utility>

namespace kb::scene {

class SceneUIGroupTransitions {
public:
    [[nodiscard]] static bool Update(Scene& scene, float deltaSeconds) {
        auto& shownGroups = SceneAccess::State(scene).uiGroupShown;
        const auto ui = std::as_const(scene).Components().UI();
        // Prune only derived animation state, including removed components and
        // destroyed entities. Unrelated world entities never enter this path.
        std::erase_if(shownGroups, [&ui](const auto& entry) {
            return ui.TryGet<UICanvasGroup>(SceneEntity{entry.first}) == nullptr;
        });
        bool changed = false;
        struct Context {
            decltype(shownGroups) shownGroups;
            float deltaSeconds;
            bool& changed;
        } context{shownGroups, deltaSeconds, changed};
        scene.Runtime().EcsWorld().CreateQuery<UICanvasGroup>().ForEach(
            [](SceneEntity entity, const UICanvasGroup& group, void* raw) {
                auto& context = *static_cast<Context*>(raw);
                const float target = group.visible ? 1.0F : 0.0F;
                const auto [entry, inserted] = context.shownGroups.try_emplace(entity.Id(), target);
                float& shown = entry->second;
                if (inserted || shown == target) return;
                const float step = group.transitionSeconds <= 0.0F ? 1.0F : context.deltaSeconds / group.transitionSeconds;
                shown = target > shown ? std::min(target, shown + step) : std::max(target, shown - step);
                context.changed = true;
            }, &context);
        return changed;
    }
};

} // namespace kb::scene
