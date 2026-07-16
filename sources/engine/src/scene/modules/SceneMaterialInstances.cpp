#include "engine/scene/SceneMaterialInstances.hpp"

#include "scene/SceneMaterialInstanceService.hpp"

namespace kb::scene {

SceneMaterialInstanceQueries::SceneMaterialInstanceQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneMaterialInstanceQueries::Exists(std::uint64_t id) const noexcept {
    return SceneMaterialInstanceService::Exists(scene_, id);
}

std::uint64_t SceneMaterialInstanceQueries::Parent(std::uint64_t id) const noexcept {
    return SceneMaterialInstanceService::Parent(scene_, id);
}

SceneMaterialInstances::SceneMaterialInstances(Scene& scene) noexcept
    : scene_(scene) {}

std::uint64_t SceneMaterialInstances::Create(std::uint64_t parentMaterialAssetId) noexcept {
    return SceneMaterialInstanceService::Create(scene_, parentMaterialAssetId);
}

bool SceneMaterialInstances::Release(std::uint64_t id) noexcept {
    return SceneMaterialInstanceService::Release(scene_, id);
}

bool SceneMaterialInstances::Exists(std::uint64_t id) const noexcept {
    return SceneMaterialInstanceService::Exists(scene_, id);
}

std::uint64_t SceneMaterialInstances::Parent(std::uint64_t id) const noexcept {
    return SceneMaterialInstanceService::Parent(scene_, id);
}

} // namespace kb::scene
