#pragma once

#include "engine/scene/SceneUIComponents.hpp"
#include "engine/ui/UIComponentSet.hpp"

namespace kb::scene {

[[nodiscard]] UIComponentSet CaptureSceneUIComponents(const SceneUIComponentQueries& components, SceneEntity entity);
[[nodiscard]] UIComponentSet CaptureSceneUIComponents(const SceneUIComponents& components, SceneEntity entity);
void ApplySceneUIComponents(SceneUIComponents components, SceneEntity entity, const UIComponentSet& values);
void SynchronizeSceneUIComponents(SceneUIComponents components, SceneEntity entity, const UIComponentSet& values);
[[nodiscard]] bool AreUIComponentSetsEqual(const UIComponentSet& lhs, const UIComponentSet& rhs) noexcept;
[[nodiscard]] std::uint64_t HashUIComponentSet(const UIComponentSet& components) noexcept;

} // namespace kb::scene
