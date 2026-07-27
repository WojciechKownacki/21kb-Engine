#include "engine/scene/SceneMaterialInstances.hpp"

#include "scene/SceneMaterialInstanceService.hpp"

#include <utility>

namespace kb::scene {

SceneMaterialInstanceQueries::SceneMaterialInstanceQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneMaterialInstanceQueries::Exists(std::uint64_t id) const noexcept {
    return SceneMaterialInstanceService::Exists(scene_, id);
}

std::uint64_t SceneMaterialInstanceQueries::Parent(std::uint64_t id) const noexcept {
    return SceneMaterialInstanceService::Parent(scene_, id);
}

std::span<const MaterialParameterOverride> SceneMaterialInstanceQueries::Parameters(std::uint64_t id) const noexcept {
    return SceneMaterialInstanceService::Parameters(scene_, id);
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

std::span<const MaterialParameterOverride> SceneMaterialInstances::Parameters(std::uint64_t id) const noexcept {
    return SceneMaterialInstanceService::Parameters(scene_, id);
}

void SceneMaterialInstances::SetParameterSchemaValidator(std::shared_ptr<const MaterialParameterSchemaValidator> validator) noexcept {
    SceneMaterialInstanceService::SetParameterSchemaValidator(scene_, std::move(validator));
}

bool SceneMaterialInstances::HasParameterSchemaValidator() const noexcept {
    return SceneMaterialInstanceService::HasParameterSchemaValidator(scene_);
}

bool SceneMaterialInstances::SetParameterScalar(std::uint64_t id, std::string_view name, float value) noexcept {
    return SceneMaterialInstanceService::SetParameterScalar(scene_, id, name, value);
}

bool SceneMaterialInstances::SetParameterBool(std::uint64_t id, std::string_view name, bool value) noexcept {
    return SceneMaterialInstanceService::SetParameterBool(scene_, id, name, value);
}

bool SceneMaterialInstances::ClearParameter(std::uint64_t id, std::string_view name) noexcept {
    return SceneMaterialInstanceService::ClearParameter(scene_, id, name);
}

} // namespace kb::scene
