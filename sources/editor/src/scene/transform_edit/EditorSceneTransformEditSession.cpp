#include "scene/transform_edit/EditorSceneTransformEditSession.hpp"

#include <algorithm>
#include <utility>

namespace kb::editor {

bool EditorSceneTransformEditSession::Active() const noexcept {
    return primary_.IsValid() && !changes_.empty();
}

const std::string& EditorSceneTransformEditSession::LabelOrDefault() const noexcept {
    static const std::string kDefaultLabel{ "Move Entity" };
    return label_.empty() ? kDefaultLabel : label_;
}

kb::scene::SceneEntity EditorSceneTransformEditSession::Primary() const noexcept {
    return primary_;
}

kb::scene::Vec3 EditorSceneTransformEditSession::TargetStart() const noexcept {
    return targetStart_;
}

std::vector<EditorSceneObjectTransformChange>& EditorSceneTransformEditSession::Changes() noexcept {
    return changes_;
}

const std::vector<EditorSceneObjectTransformChange>& EditorSceneTransformEditSession::Changes() const noexcept {
    return changes_;
}

EditorSceneObjectTransformChange* EditorSceneTransformEditSession::PrimaryChange() noexcept {
    const auto iter = std::ranges::find_if(changes_, [primary = primary_](const EditorSceneObjectTransformChange& change) {
        return change.entity == primary;
    });
    return iter == changes_.end() ? nullptr : &*iter;
}

const EditorSceneObjectTransformChange* EditorSceneTransformEditSession::PrimaryChange() const noexcept {
    const auto iter = std::ranges::find_if(changes_, [primary = primary_](const EditorSceneObjectTransformChange& change) {
        return change.entity == primary;
    });
    return iter == changes_.end() ? nullptr : &*iter;
}

void EditorSceneTransformEditSession::Begin(
    std::string label,
    kb::scene::SceneEntity primary,
    kb::scene::Vec3 targetStart,
    std::vector<EditorSceneObjectTransformChange> changes) {
    label_ = std::move(label);
    primary_ = primary;
    targetStart_ = targetStart;
    changes_ = std::move(changes);
}

void EditorSceneTransformEditSession::Clear() noexcept {
    label_.clear();
    primary_ = {};
    targetStart_ = {};
    changes_.clear();
}

} // namespace kb::editor
