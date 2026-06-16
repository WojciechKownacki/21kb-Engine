#include "engine/scene/ScenePrefabPrivateScene.hpp"

#include "engine/scene/ScenePrefabs.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabInstanceSynchronizer.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"

#include <utility>

namespace kb::scene {

ScenePrefabPrivateScene::ScenePrefabPrivateScene(
    Scene& sourceScene,
    ScenePrefabHandle sourcePrefab,
    std::unique_ptr<Scene> editScene,
    ScenePrefabHandle editPrefab,
    ScenePrefabInstance editInstance) noexcept
    : sourceScene_(&sourceScene)
    , editScene_(std::move(editScene))
    , sourcePrefab_(sourcePrefab)
    , editPrefab_(editPrefab)
    , editInstance_(std::move(editInstance)) {}

ScenePrefabPrivateScene::~ScenePrefabPrivateScene() = default;

ScenePrefabPrivateScene::ScenePrefabPrivateScene(ScenePrefabPrivateScene&&) noexcept = default;

ScenePrefabPrivateScene& ScenePrefabPrivateScene::operator=(ScenePrefabPrivateScene&&) noexcept = default;

bool ScenePrefabPrivateScene::IsValid() const noexcept {
    return sourceScene_ != nullptr &&
        editScene_ != nullptr &&
        sourcePrefab_.IsValid() &&
        editPrefab_.IsValid() &&
        editInstance_.Handle().IsValid();
}

Scene& ScenePrefabPrivateScene::EditScene() noexcept {
    return *editScene_;
}

const Scene& ScenePrefabPrivateScene::EditScene() const noexcept {
    return *editScene_;
}

ScenePrefabHandle ScenePrefabPrivateScene::SourcePrefab() const noexcept {
    return sourcePrefab_;
}

ScenePrefabHandle ScenePrefabPrivateScene::EditPrefab() const noexcept {
    return editPrefab_;
}

ScenePrefabInstanceHandle ScenePrefabPrivateScene::EditInstance() const noexcept {
    return editInstance_.Handle();
}

std::size_t ScenePrefabPrivateScene::ObjectCount() const noexcept {
    return editInstance_.ObjectCount();
}

SceneObject ScenePrefabPrivateScene::ObjectAt(std::uint32_t nodeIndex) const noexcept {
    return editInstance_.ObjectAt(nodeIndex);
}

SceneObject ScenePrefabPrivateScene::RootObject() const noexcept {
    return editInstance_.RootObject();
}

ScenePrefabOverrideReport ScenePrefabPrivateScene::Overrides() const {
    return IsValid() ? editScene_->Prefabs().Overrides(editInstance_.Handle()) : ScenePrefabOverrideReport{};
}

bool ScenePrefabPrivateScene::Revert() {
    return IsValid() && editScene_->Prefabs().RevertOverrides(editInstance_.Handle());
}

bool ScenePrefabPrivateScene::Apply() {
    if (!IsValid()) {
        return false;
    }

    SceneState& sourceState = SceneAccess::State(*sourceScene_);
    ScenePrefabRecord* sourceRecord = sourceState.prefabs.FindMutableRecord(sourcePrefab_);
    if (sourceRecord == nullptr || sourceRecord->kind != ScenePrefabRecordKind::Template) {
        return false;
    }
    if (!editScene_->Prefabs().ApplyOverrides(editInstance_.Handle())) {
        return false;
    }

    ScenePrefab updatedPrefab = editScene_->Prefabs().Get(editPrefab_);
    if (updatedPrefab.Empty()) {
        return false;
    }

    sourceRecord->prefab = std::move(updatedPrefab);
    sourceState.prefabs.RefreshContentHash(sourcePrefab_);
    sourceState.prefabs.RefreshDerivedPrefabs(sourcePrefab_);
    static_cast<void>(ScenePrefabInstanceSynchronizer::Refresh(*sourceScene_, sourcePrefab_));
    return true;
}

} // namespace kb::scene
