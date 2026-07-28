#include "scene/SceneUIDocumentService.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <stdexcept>
#include <utility>

namespace kb::scene {
namespace {

const UIDocumentRuntimeRecord* Find(const SceneState& state, SceneEntity entity) {
    const auto it = state.uiDocuments.find(entity.Id());
    return it != state.uiDocuments.end() && it->second.entity == entity ? &it->second : nullptr;
}

UIDocumentRuntimeRecord* FindMutable(SceneState& state, SceneEntity entity) {
    const auto it = state.uiDocuments.find(entity.Id());
    return it != state.uiDocuments.end() && it->second.entity == entity ? &it->second : nullptr;
}

constexpr std::size_t kMaxPendingUICommands = 4096U;
constexpr std::size_t kMaxPendingUIEvents = 4096U;

bool ValidControl(const UIControlState& control) noexcept {
    if (!std::isfinite(control.sliderValue) || !std::isfinite(control.sliderMinimum) ||
        !std::isfinite(control.sliderMaximum) || !std::isfinite(control.scrollOffset) ||
        control.sliderMinimum > control.sliderMaximum || control.sliderValue < control.sliderMinimum ||
        control.sliderValue > control.sliderMaximum || control.scrollOffset < 0.0F) return false;
    if (control.listItems.size() > kMaxUIListItems) return false;
    for (const std::string& item : control.listItems) if (item.empty()) return false;
    return control.kind <= UIControlKind::ModalDialog;
}

bool ValidEvent(const UIRuntimeEvent& event) noexcept {
    if (event.elementId == 0U || event.kind > UIRuntimeEventKind::Navigation ||
        !std::isfinite(event.pointerX) || !std::isfinite(event.pointerY) ||
        !std::isfinite(event.value) || event.text.size() > kMaxUIEventTextBytes) {
        return false;
    }
    if (event.kind == UIRuntimeEventKind::Navigation) {
        return event.navigation > UINavigationDirection::None && event.navigation <= UINavigationDirection::Right;
    }
    return event.navigation == UINavigationDirection::None;
}

bool HasQueuedDestroy(const SceneState& state, SceneEntity entity, UIElementId element) noexcept {
    for (const UIRuntimeCommand& command : state.pendingUICommands) {
        if (command.entity == entity && command.kind == UIRuntimeCommandKind::Destroy && command.elementId == element) {
            return true;
        }
    }
    return false;
}

bool HasPendingCreate(const SceneState& state, SceneEntity entity, UIElementId element) noexcept {
    for (const UIRuntimeCommand& command : state.pendingUICommands) {
        if (command.entity == entity && command.kind == UIRuntimeCommandKind::Create && command.elementId == element) {
            return true;
        }
    }
    return false;
}

bool IsPendingDestroyAncestor(const SceneState& state, const UIDocumentRuntimeRecord& record,
    SceneEntity entity, UIElementId element) noexcept {
    UIElementId current = element;
    while (current != 0U) {
        if (HasQueuedDestroy(state, entity, current)) return true;
        const auto found = record.elements.find(current);
        if (found == record.elements.end() || current == record.root) return false;
        current = found->second.parentId;
    }
    return false;
}

void DiscardCommands(SceneState& state, SceneEntity entity) {
    const auto first = std::remove_if(state.pendingUICommands.begin(), state.pendingUICommands.end(), [entity](const UIRuntimeCommand& command) {
        return command.entity == entity;
    });
    state.pendingUICommands.erase(first, state.pendingUICommands.end());
}

void DestroySubtree(UIDocumentRuntimeRecord& record, UIElementId element) {
    std::vector<UIElementId> children;
    for (const auto& [id, candidate] : record.elements) {
        if (candidate.parentId == element) children.push_back(id);
    }
    for (const UIElementId child : children) DestroySubtree(record, child);
    record.elements.erase(element);
}

void ApplyCommands(SceneState& state) {
    std::vector<UIRuntimeCommand> commands;
    commands.swap(state.pendingUICommands);
    for (const UIRuntimeCommand& command : commands) {
        UIDocumentRuntimeRecord* record = FindMutable(state, command.entity);
        if (record == nullptr) continue;
        switch (command.kind) {
        case UIRuntimeCommandKind::Create: {
            if (command.create.parentId == 0U || record->elements.contains(command.elementId) ||
                !record->elements.contains(command.create.parentId)) {
                continue;
            }
            record->elements.emplace(command.elementId, UIDocumentElement{
                .id = command.elementId,
                .parentId = command.create.parentId,
                .name = command.create.name,
                .styleClass = command.create.styleClass,
                .visible = command.create.visible,
                .control = command.create.control,
            });
            break;
        }
        case UIRuntimeCommandKind::Destroy:
            if (command.elementId != record->root && record->elements.contains(command.elementId)) {
                DestroySubtree(*record, command.elementId);
            }
            break;
        case UIRuntimeCommandKind::SetVisible: {
            const auto element = record->elements.find(command.elementId);
            if (element != record->elements.end()) element->second.visible = command.visible;
            break;
        }
        case UIRuntimeCommandKind::SetControl: {
            const auto element = record->elements.find(command.elementId);
            if (element != record->elements.end()) element->second.control = command.control;
            break;
        }
        }
    }
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
        if (element.id == std::numeric_limits<UIElementId>::max()) return false;
        record.nextRuntimeElementId = std::max(record.nextRuntimeElementId, element.id + 1U);
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
bool SceneUIDocumentService::Visible(const Scene& scene, SceneEntity entity, UIElementId element) noexcept {
    const auto* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr) return false;
    const auto current = record->elements.find(element);
    return current != record->elements.end() && current->second.visible;
}

void DiscardEvents(SceneState& state, SceneEntity entity) {
    const auto first = std::remove_if(state.pendingUIEvents.begin(), state.pendingUIEvents.end(), [entity](const PendingUIRuntimeEvent& event) {
        return event.entity == entity;
    });
    state.pendingUIEvents.erase(first, state.pendingUIEvents.end());
}
std::optional<UIControlState> SceneUIDocumentService::Control(const Scene& scene, SceneEntity entity, UIElementId element) {
    const auto* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr) return std::nullopt;
    const auto current = record->elements.find(element);
    return current == record->elements.end() ? std::nullopt : std::optional<UIControlState>{ current->second.control };
}
bool SceneUIDocumentService::StyleIsResolved(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = Find(SceneAccess::State(scene), entity);
    return record != nullptr && (record->document->styleAssetId == 0U || record->style.IsLoaded());
}
std::size_t SceneUIDocumentService::ElementCount(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = Find(SceneAccess::State(scene), entity);
    return record != nullptr ? record->elements.size() : 0U;
}

std::optional<UIElementId> SceneUIDocumentService::QueueCreate(Scene& scene, SceneEntity entity, const UIRuntimeElementDesc& desc) {
    if (desc.name.empty() || desc.parentId == 0U || !ValidControl(desc.control)) return std::nullopt;
    SceneState& state = SceneAccess::State(scene);
    UIDocumentRuntimeRecord* record = FindMutable(state, entity);
    if (record == nullptr || state.pendingUICommands.size() >= kMaxPendingUICommands ||
        IsPendingDestroyAncestor(state, *record, entity, desc.parentId) ||
        (!record->elements.contains(desc.parentId) && !HasPendingCreate(state, entity, desc.parentId))) return std::nullopt;
    if (record->nextRuntimeElementId == 0U) return std::nullopt;
    const UIElementId id = record->nextRuntimeElementId;
    if (id == std::numeric_limits<UIElementId>::max()) {
        record->nextRuntimeElementId = 0U;
    } else {
        ++record->nextRuntimeElementId;
    }
    state.pendingUICommands.push_back(UIRuntimeCommand{
        .kind = UIRuntimeCommandKind::Create,
        .entity = entity,
        .elementId = id,
        .create = desc,
    });
    return id;
}

bool SceneUIDocumentService::QueueDestroy(Scene& scene, SceneEntity entity, UIElementId element) noexcept {
    SceneState& state = SceneAccess::State(scene);
    UIDocumentRuntimeRecord* record = FindMutable(state, entity);
    if (record == nullptr || element == record->root || !record->elements.contains(element) ||
        IsPendingDestroyAncestor(state, *record, entity, element) || state.pendingUICommands.size() >= kMaxPendingUICommands) return false;
    state.pendingUICommands.push_back(UIRuntimeCommand{ .kind = UIRuntimeCommandKind::Destroy, .entity = entity, .elementId = element });
    return true;
}

bool SceneUIDocumentService::QueueVisibility(Scene& scene, SceneEntity entity, UIElementId element, bool visible) noexcept {
    SceneState& state = SceneAccess::State(scene);
    UIDocumentRuntimeRecord* record = FindMutable(state, entity);
    if (record == nullptr || !record->elements.contains(element) || IsPendingDestroyAncestor(state, *record, entity, element) ||
        state.pendingUICommands.size() >= kMaxPendingUICommands) return false;
    state.pendingUICommands.push_back(UIRuntimeCommand{ .kind = UIRuntimeCommandKind::SetVisible, .entity = entity, .elementId = element, .visible = visible });
    return true;
}

bool SceneUIDocumentService::QueueSetControl(Scene& scene, SceneEntity entity, UIElementId element, const UIControlState& control) {
    if (!ValidControl(control)) return false;
    SceneState& state = SceneAccess::State(scene);
    UIDocumentRuntimeRecord* record = FindMutable(state, entity);
    if (record == nullptr || !record->elements.contains(element) ||
        IsPendingDestroyAncestor(state, *record, entity, element) || state.pendingUICommands.size() >= kMaxPendingUICommands) return false;
    state.pendingUICommands.push_back(UIRuntimeCommand{
        .kind = UIRuntimeCommandKind::SetControl,
        .entity = entity,
        .elementId = element,
        .control = control,
    });
    return true;
}

bool SceneUIDocumentService::QueueEvent(Scene& scene, SceneEntity entity, const UIRuntimeEvent& event) {
    if (!ValidEvent(event)) return false;
    SceneState& state = SceneAccess::State(scene);
    UIDocumentRuntimeRecord* record = FindMutable(state, entity);
    if (record == nullptr || state.pendingUIEvents.size() >= kMaxPendingUIEvents ||
        IsPendingDestroyAncestor(state, *record, entity, event.elementId)) {
        return false;
    }
    const auto element = record->elements.find(event.elementId);
    if (element == record->elements.end() || !element->second.visible) return false;
    state.pendingUIEvents.push_back(PendingUIRuntimeEvent{ .entity = entity, .event = event });
    return true;
}

std::vector<UIRuntimeEventRecord> SceneUIDocumentService::DrainEvents(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    std::vector<PendingUIRuntimeEvent> pending;
    pending.swap(state.pendingUIEvents);
    std::vector<UIRuntimeEventRecord> drained;
    drained.reserve(pending.size());
    for (PendingUIRuntimeEvent& queued : pending) {
        const UIDocumentRuntimeRecord* record = Find(state, queued.entity);
        if (record == nullptr) continue;
        const auto element = record->elements.find(queued.event.elementId);
        if (element != record->elements.end() && element->second.visible) {
            drained.push_back(UIRuntimeEventRecord{ .owner = queued.entity, .event = std::move(queued.event) });
        }
    }
    return drained;
}

void SceneUIDocumentService::SyncComponents(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    if (state.mode == SceneMode::PrefabPrivate) {
        state.uiDocuments.clear();
        state.pendingUICommands.clear();
        state.pendingUIEvents.clear();
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
            DiscardCommands(state, entity);
            DiscardEvents(state, entity);
            state.uiDocuments.erase(id);
            if (!Attach(scene, entity, component.documentAssetId)) {
                throw std::runtime_error("Enabled UIDocument component could not load its document or style asset");
            }
        }
    }
    for (auto it = state.uiDocuments.begin(); it != state.uiDocuments.end();) {
        const UIDocumentComponent* component = scene.Components().UIDocuments().TryGet(it->second.entity);
        if (!scene.Entities().IsAlive(it->second.entity) || component == nullptr || !component->enabled ||
            !authored.contains(it->first)) {
            DiscardCommands(state, it->second.entity);
            DiscardEvents(state, it->second.entity);
            it = state.uiDocuments.erase(it);
        }
        else ++it;
    }
    ApplyCommands(state);
}

} // namespace kb::scene
