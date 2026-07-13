#include "engine/scene/SceneLoadedContent.hpp"

#include "scene/SceneLoadedContentService.hpp"

namespace kb::scene {

SceneLoadedContentQueries::SceneLoadedContentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

std::uint64_t SceneLoadedContentQueries::Find(std::string_view name) const noexcept {
    return SceneLoadedContentService::Find(scene_, name);
}

bool SceneLoadedContentQueries::Exists(std::uint64_t id) const noexcept {
    return SceneLoadedContentService::Exists(scene_, id);
}

float SceneLoadedContentQueries::Progress(std::uint64_t id) const noexcept {
    return SceneLoadedContentService::Progress(scene_, id);
}

std::uint64_t SceneLoadedContentQueries::ActiveScene() const noexcept {
    return SceneLoadedContentService::ActiveScene(scene_);
}

SceneLoadedContent::SceneLoadedContent(Scene& scene) noexcept
    : scene_(scene) {}

std::uint64_t SceneLoadedContent::Load(const std::filesystem::path& path, bool additive) {
    return SceneLoadedContentService::Load(scene_, path, additive);
}

bool SceneLoadedContent::Unload(std::uint64_t id) noexcept {
    return SceneLoadedContentService::Unload(scene_, id);
}

std::uint64_t SceneLoadedContent::Find(std::string_view name) const noexcept {
    return SceneLoadedContentService::Find(scene_, name);
}

bool SceneLoadedContent::Exists(std::uint64_t id) const noexcept {
    return SceneLoadedContentService::Exists(scene_, id);
}

float SceneLoadedContent::Progress(std::uint64_t id) const noexcept {
    return SceneLoadedContentService::Progress(scene_, id);
}

bool SceneLoadedContent::SetActive(std::uint64_t id) noexcept {
    return SceneLoadedContentService::SetActive(scene_, id);
}

std::uint64_t SceneLoadedContent::ActiveScene() const noexcept {
    return SceneLoadedContentService::ActiveScene(scene_);
}

std::vector<SceneLifecycleEventRecord> SceneLoadedContent::DrainPendingLifecycleEvents() {
    return SceneLoadedContentService::DrainPendingLifecycleEvents(scene_);
}

} // namespace kb::scene
