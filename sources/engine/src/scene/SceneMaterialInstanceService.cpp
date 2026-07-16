#include "scene/SceneMaterialInstanceService.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>

namespace kb::scene {
namespace {

// LIB-139: this is the ticket's "limit wariantów" (variant limit) - a hard
// cap on how many runtime MaterialInstance objects a single scene may have
// alive at once. Deliberately smaller than SceneTimerService's
// kMaxLiveTimers (4096) - a material instance conceptually stands in for a
// GPU-adjacent resource once LIB-140 adds real parameter overrides, so a
// much lower ceiling catches runaway create-without-release script bugs far
// sooner than a timer-scale limit would. Duplicated as a plain constant
// rather than shared from kb::library for the same reason
// SceneTimerService.cpp duplicates kDefaultLibraryInputLimits.
// maxCollectionSize: kb::scene must never depend on kb::library.
constexpr std::size_t kMaxLiveMaterialInstances = 512U;

} // namespace

std::uint64_t SceneMaterialInstanceService::Create(Scene& scene, std::uint64_t parentMaterialAssetId) noexcept {
    if (parentMaterialAssetId == 0U) {
        return 0U;
    }
    SceneState& state = SceneAccess::State(scene);
    if (state.materialInstances.size() >= kMaxLiveMaterialInstances) {
        return 0U;
    }
    const std::uint64_t id = state.nextMaterialInstanceId++;
    state.materialInstances.push_back(SceneState::MaterialInstanceRecord{
        .id = id,
        .parentMaterialAssetId = parentMaterialAssetId,
    });
    return id;
}

bool SceneMaterialInstanceService::Release(Scene& scene, std::uint64_t id) noexcept {
    SceneState& state = SceneAccess::State(scene);
    const auto iterator = std::find_if(state.materialInstances.begin(), state.materialInstances.end(), [id](const SceneState::MaterialInstanceRecord& instance) {
        return instance.id == id;
    });
    if (iterator == state.materialInstances.end()) {
        return false;
    }
    state.materialInstances.erase(iterator);
    return true;
}

bool SceneMaterialInstanceService::Exists(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    return std::any_of(state.materialInstances.begin(), state.materialInstances.end(), [id](const SceneState::MaterialInstanceRecord& instance) {
        return instance.id == id;
    });
}

std::uint64_t SceneMaterialInstanceService::Parent(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    const auto iterator = std::find_if(state.materialInstances.begin(), state.materialInstances.end(), [id](const SceneState::MaterialInstanceRecord& instance) {
        return instance.id == id;
    });
    return iterator == state.materialInstances.end() ? 0U : iterator->parentMaterialAssetId;
}

std::span<const MaterialParameterOverride> SceneMaterialInstanceService::Parameters(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    const auto iterator = std::find_if(state.materialInstances.begin(), state.materialInstances.end(), [id](const SceneState::MaterialInstanceRecord& instance) {
        return instance.id == id;
    });
    if (iterator == state.materialInstances.end()) {
        return {};
    }
    return iterator->parameterOverrides;
}

namespace {

[[nodiscard]] SceneState::MaterialInstanceRecord* FindLiveInstance(SceneState& state, std::uint64_t id) noexcept {
    const auto iterator = std::find_if(state.materialInstances.begin(), state.materialInstances.end(), [id](const SceneState::MaterialInstanceRecord& instance) {
        return instance.id == id;
    });
    return iterator == state.materialInstances.end() ? nullptr : &(*iterator);
}

// Upsert-by-name (last-write-wins), mirrors ScriptEventBus::RegisterEvent's own established
// upsert-by-key convention.
void UpsertParameter(std::vector<MaterialParameterOverride>& overrides, MaterialParameterOverride value) {
    for (MaterialParameterOverride& existing : overrides) {
        if (existing.name == value.name) {
            existing = std::move(value);
            return;
        }
    }
    overrides.push_back(std::move(value));
}

} // namespace

bool SceneMaterialInstanceService::SetParameterScalar(Scene& scene, std::uint64_t id, std::string_view name, float value) noexcept {
    if (name.empty()) {
        return false;
    }
    SceneState::MaterialInstanceRecord* instance = FindLiveInstance(SceneAccess::State(scene), id);
    if (instance == nullptr) {
        return false;
    }
    UpsertParameter(instance->parameterOverrides, MaterialParameterOverride{
                                                       .name = std::string{ name },
                                                       .type = MaterialParameterType::Scalar,
                                                       .scalarValue = value,
                                                   });
    return true;
}

bool SceneMaterialInstanceService::SetParameterBool(Scene& scene, std::uint64_t id, std::string_view name, bool value) noexcept {
    if (name.empty()) {
        return false;
    }
    SceneState::MaterialInstanceRecord* instance = FindLiveInstance(SceneAccess::State(scene), id);
    if (instance == nullptr) {
        return false;
    }
    UpsertParameter(instance->parameterOverrides, MaterialParameterOverride{
                                                       .name = std::string{ name },
                                                       .type = MaterialParameterType::Bool,
                                                       .boolValue = value,
                                                   });
    return true;
}

bool SceneMaterialInstanceService::ClearParameter(Scene& scene, std::uint64_t id, std::string_view name) noexcept {
    SceneState::MaterialInstanceRecord* instance = FindLiveInstance(SceneAccess::State(scene), id);
    if (instance == nullptr) {
        return false;
    }
    const auto iterator = std::find_if(instance->parameterOverrides.begin(), instance->parameterOverrides.end(), [name](const MaterialParameterOverride& override_) {
        return override_.name == name;
    });
    if (iterator == instance->parameterOverrides.end()) {
        return false;
    }
    instance->parameterOverrides.erase(iterator);
    return true;
}

} // namespace kb::scene
