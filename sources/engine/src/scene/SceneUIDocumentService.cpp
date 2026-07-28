#include "scene/SceneUIDocumentService.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <map>
#include <stdexcept>

namespace kb::scene {
namespace {

const UIDocumentRuntimeRecord* Find(const SceneState& state, SceneEntity entity) {
    const auto it = state.uiDocuments.find(entity.Id());
    return it != state.uiDocuments.end() && it->second.entity == entity ? &it->second : nullptr;
}

bool Attach(Scene& scene, SceneEntity entity, std::uint64_t assetId) {
    if (assetId == 0U) return false;
    kb::assets::AssetHandle<UIDocument> document = scene.Assets().Manager().Load<UIDocument>(kb::assets::AssetId{ assetId });
    if (!document.IsLoaded()) return false;
    UIDocumentRuntimeRecord record{};
    record.entity = entity;
    record.document = std::move(document);
    record.documentLoadGeneration = scene.Assets().Manager().LoadGeneration(record.document.Id());
    for (const UIDocumentElement& element : record.document->elements) {
        record.elements.emplace(element.id, element);
        if (element.parentId == 0U) record.root = element.id;
    }
    if (record.document->styleAssetId != 0U) {
        record.style = scene.Assets().Manager().Load<UIStyleAsset>(kb::assets::AssetId{ record.document->styleAssetId });
        if (!record.style.IsLoaded()) return false;
        record.styleLoadGeneration = scene.Assets().Manager().LoadGeneration(record.style.Id());
    }
    SceneAccess::State(scene).uiDocuments.insert_or_assign(entity.Id(), std::move(record));
    return true;
}

} // namespace

bool SceneUIDocumentService::Exists(const Scene& scene, SceneEntity entity) noexcept { return Find(SceneAccess::State(scene), entity) != nullptr; }
std::uint64_t SceneUIDocumentService::Asset(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = Find(SceneAccess::State(scene), entity);
    return record != nullptr ? record->document.Id().value : 0U;
}
UIElementId SceneUIDocumentService::Root(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = Find(SceneAccess::State(scene), entity);
    return record != nullptr ? record->root : 0U;
}
bool SceneUIDocumentService::HasElement(const Scene& scene, SceneEntity entity, UIElementId element) noexcept {
    const auto* record = Find(SceneAccess::State(scene), entity);
    return record != nullptr && record->elements.contains(element);
}
bool SceneUIDocumentService::StyleIsResolved(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = Find(SceneAccess::State(scene), entity);
    return record != nullptr && (record->document->styleAssetId == 0U || record->style.IsLoaded());
}
std::size_t SceneUIDocumentService::ElementCount(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = Find(SceneAccess::State(scene), entity);
    return record != nullptr ? record->elements.size() : 0U;
}

void SceneUIDocumentService::SyncComponents(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    if (state.mode == SceneMode::PrefabPrivate) {
        state.uiDocuments.clear();
        return;
    }
    std::map<std::uint64_t, std::pair<SceneEntity, UIDocumentComponent>> authored;
    std::vector<SceneEntity> pending = scene.Hierarchy().RootEntities();
    while (!pending.empty()) {
        const SceneEntity entity = pending.back();
        pending.pop_back();
        const auto children = scene.Hierarchy().ChildEntities(entity);
        pending.insert(pending.end(), children.begin(), children.end());
        if (const UIDocumentComponent* component = scene.Components().UIDocuments().TryGet(entity);
            component != nullptr && component->enabled) {
            authored.emplace(entity.Id(), std::pair{ entity, *component });
        }
    }
    for (const auto& [id, authoredValue] : authored) {
        const SceneEntity entity = authoredValue.first;
        const UIDocumentComponent& component = authoredValue.second;
        UIDocumentRuntimeRecord* current = nullptr;
        const auto currentIt = state.uiDocuments.find(id);
        if (currentIt != state.uiDocuments.end() && currentIt->second.entity == entity) current = &currentIt->second;
        const bool stale = current == nullptr || current->document.Id().value != component.documentAssetId ||
            current->documentLoadGeneration != scene.Assets().Manager().LoadGeneration(current->document.Id()) ||
            (current->document->styleAssetId != 0U && current->styleLoadGeneration != scene.Assets().Manager().LoadGeneration(current->style.Id()));
        if (stale) {
            state.uiDocuments.erase(id);
            if (!Attach(scene, entity, component.documentAssetId)) {
                throw std::runtime_error("Enabled UIDocument component could not load its document or style asset");
            }
        }
    }
    for (auto it = state.uiDocuments.begin(); it != state.uiDocuments.end();) {
        const UIDocumentComponent* component = scene.Components().UIDocuments().TryGet(it->second.entity);
        if (!scene.Entities().IsAlive(it->second.entity) || component == nullptr || !component->enabled ||
            !authored.contains(it->first)) it = state.uiDocuments.erase(it);
        else ++it;
    }
}

} // namespace kb::scene
