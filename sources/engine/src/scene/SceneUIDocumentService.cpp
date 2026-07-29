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
#include <charconv>
#include <cmath>
#include <map>
#include <limits>
#include <stdexcept>
#include <utility>

namespace kb::scene {
namespace {

const UIDocumentRuntimeRecord* FindRecord(const SceneState& state, SceneEntity entity) {
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

bool SameValue(const std::optional<UIBindingValue>& lhs, const std::optional<UIBindingValue>& rhs) noexcept {
    return lhs.has_value() == rhs.has_value() && (!lhs.has_value() || *lhs == *rhs);
}

std::optional<UIBindingValue> ReadBoundControl(const UIDocumentElement& element, const UIBindingDeclaration& binding) {
    UIBindingValue value{ .type = binding.valueType };
    const UIControlState& control = element.control;
    if (binding.property == "text" &&
        (control.kind == UIControlKind::Text || control.kind == UIControlKind::Button || control.kind == UIControlKind::InputField)) {
        if (binding.valueType == UIDataValueType::String) {
            value.string = control.text;
            return value;
        }
        if (binding.valueType == UIDataValueType::Boolean) {
            if (control.text == "true") value.boolean = true;
            else if (control.text == "false") value.boolean = false;
            else return std::nullopt;
            return value;
        }
        const char* first = control.text.data();
        const char* const last = first + control.text.size();
        const auto parsed = std::from_chars(first, last, value.number, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != last || !std::isfinite(value.number)) return std::nullopt;
        return value;
    }
    if (binding.property == "toggle" && binding.valueType == UIDataValueType::Boolean && control.kind == UIControlKind::Toggle) {
        value.boolean = control.toggleValue;
        return value;
    }
    if (binding.property == "value" && binding.valueType == UIDataValueType::Number && control.kind == UIControlKind::Slider) {
        value.number = control.sliderValue;
        return value;
    }
    if (binding.property == "scroll" && binding.valueType == UIDataValueType::Number && control.kind == UIControlKind::ScrollView) {
        value.number = control.scrollOffset;
        return value;
    }
    if (binding.property == "modal" && binding.valueType == UIDataValueType::Boolean && control.kind == UIControlKind::ModalDialog) {
        value.boolean = control.modalOpen;
        return value;
    }
    return std::nullopt;
}

bool ApplyBoundValue(const UIBindingDeclaration& binding, const UIBindingValue& value, UIControlState& control) {
    if (value.type != binding.valueType) return false;
    if (binding.property == "text" &&
        (control.kind == UIControlKind::Text || control.kind == UIControlKind::Button || control.kind == UIControlKind::InputField)) {
        if (value.type == UIDataValueType::String) {
            control.text = value.string;
            return true;
        }
        if (value.type == UIDataValueType::Boolean) {
            control.text = value.boolean ? "true" : "false";
            return true;
        }
        if (!std::isfinite(value.number)) return false;
        char buffer[64]{};
        const auto written = std::to_chars(std::begin(buffer), std::end(buffer), value.number, std::chars_format::general);
        if (written.ec != std::errc{}) return false;
        control.text.assign(buffer, written.ptr);
        return true;
    }
    if (binding.property == "toggle" && value.type == UIDataValueType::Boolean && control.kind == UIControlKind::Toggle) {
        control.toggleValue = value.boolean;
        return true;
    }
    if (binding.property == "value" && value.type == UIDataValueType::Number && control.kind == UIControlKind::Slider &&
        std::isfinite(value.number) && value.number >= control.sliderMinimum && value.number <= control.sliderMaximum) {
        control.sliderValue = static_cast<float>(value.number);
        return true;
    }
    if (binding.property == "scroll" && value.type == UIDataValueType::Number && control.kind == UIControlKind::ScrollView &&
        std::isfinite(value.number) && value.number >= 0.0) {
        control.scrollOffset = static_cast<float>(value.number);
        return true;
    }
    if (binding.property == "modal" && value.type == UIDataValueType::Boolean && control.kind == UIControlKind::ModalDialog) {
        control.modalOpen = value.boolean;
        return true;
    }
    return false;
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

void RefreshVirtualLists(UIDocumentRuntimeRecord& record) {
    for (auto it = record.virtualLists.begin(); it != record.virtualLists.end();) {
        const auto element = record.elements.find(it->first);
        if (element == record.elements.end() || element->second.control.kind != UIControlKind::List) {
            it = record.virtualLists.erase(it);
            continue;
        }
        UIDocumentRuntimeRecord::VirtualListState& state = it->second;
        const std::uint32_t total = static_cast<std::uint32_t>(element->second.control.listItems.size());
        if (total == 0U) {
            state.firstVisibleIndex = 0U;
            state.activeItemCount = 0U;
            ++it;
            continue;
        }
        state.firstVisibleIndex = std::min(state.firstVisibleIndex, total - 1U);
        const std::uint32_t firstPooled = state.firstVisibleIndex > state.overscan ? state.firstVisibleIndex - state.overscan : 0U;
        const std::uint32_t afterVisible = std::min(total, state.firstVisibleIndex + state.viewportItems);
        const std::uint32_t afterPooled = std::min(total, afterVisible + state.overscan);
        const std::uint32_t active = afterPooled - firstPooled;
        if (state.pool.size() < active) state.pool.resize(active);
        for (std::uint32_t slot = 0U; slot < active; ++slot) {
            const std::uint32_t index = firstPooled + slot;
            state.pool[slot] = UIVirtualListItem{ .index = index, .text = element->second.control.listItems[index] };
        }
        state.activeItemCount = active;
        ++it;
    }
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
        case UIRuntimeCommandKind::ConfigureVirtualList: {
            const auto element = record->elements.find(command.elementId);
            if (element != record->elements.end() && element->second.control.kind == UIControlKind::List) {
                record->virtualLists.insert_or_assign(command.elementId, UIDocumentRuntimeRecord::VirtualListState{
                    .viewportItems = command.viewportItems,
                    .overscan = command.overscan,
                });
            }
            break;
        }
        case UIRuntimeCommandKind::ScrollVirtualList: {
            const auto list = record->virtualLists.find(command.elementId);
            if (list != record->virtualLists.end()) list->second.firstVisibleIndex = command.firstVisibleIndex;
            break;
        }
        }
    }
    for (auto& [id, record] : state.uiDocuments) {
        static_cast<void>(id);
        RefreshVirtualLists(record);
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
    record.bindings.reserve(record.document->bindings.size());
    for (const UIBindingDeclaration& binding : record.document->bindings) {
        record.bindings.push_back(UIDocumentRuntimeRecord::BindingState{ .declaration = binding });
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

bool SceneUIDocumentService::Exists(const Scene& scene, SceneEntity entity) noexcept { return FindRecord(SceneAccess::State(scene), entity) != nullptr; }
std::uint64_t SceneUIDocumentService::Asset(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = FindRecord(SceneAccess::State(scene), entity);
    return record != nullptr ? record->document.Id().value : 0U;
}
UIElementId SceneUIDocumentService::Root(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = FindRecord(SceneAccess::State(scene), entity);
    return record != nullptr ? record->root : 0U;
}
bool SceneUIDocumentService::HasElement(const Scene& scene, SceneEntity entity, UIElementId element) noexcept {
    const auto* record = FindRecord(SceneAccess::State(scene), entity);
    return record != nullptr && record->elements.contains(element);
}
bool SceneUIDocumentService::Visible(const Scene& scene, SceneEntity entity, UIElementId element) noexcept {
    const auto* record = FindRecord(SceneAccess::State(scene), entity);
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
    const auto* record = FindRecord(SceneAccess::State(scene), entity);
    if (record == nullptr) return std::nullopt;
    const auto current = record->elements.find(element);
    return current == record->elements.end() ? std::nullopt : std::optional<UIControlState>{ current->second.control };
}
std::optional<UIVirtualListView> SceneUIDocumentService::VirtualList(const Scene& scene, SceneEntity entity, UIElementId element) noexcept {
    const auto* record = FindRecord(SceneAccess::State(scene), entity);
    if (record == nullptr) return std::nullopt;
    const auto list = record->virtualLists.find(element);
    const auto control = record->elements.find(element);
    if (list == record->virtualLists.end() || control == record->elements.end() || control->second.control.kind != UIControlKind::List) return std::nullopt;
    return UIVirtualListView{
        .totalItemCount = static_cast<std::uint32_t>(control->second.control.listItems.size()),
        .firstVisibleIndex = list->second.firstVisibleIndex,
        .pooledItems = std::span<const UIVirtualListItem>{ list->second.pool.data(), list->second.activeItemCount },
    };
}
std::optional<UIElementId> SceneUIDocumentService::Find(const Scene& scene, SceneEntity entity, std::string_view name) noexcept {
    // LIB-177: no name index by design. This deterministic O(n) setup scan
    // returns no handle for duplicate names rather than silently binding a
    // cached caller to an arbitrary element.
    if (name.empty()) return std::nullopt;
    const auto* record = FindRecord(SceneAccess::State(scene), entity);
    if (record == nullptr) return std::nullopt;
    std::optional<UIElementId> result;
    for (const auto& [id, element] : record->elements) {
        if (element.name != name) continue;
        if (result.has_value()) return std::nullopt;
        result = id;
    }
    return result;
}
bool SceneUIDocumentService::StyleIsResolved(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = FindRecord(SceneAccess::State(scene), entity);
    return record != nullptr && (record->document->styleAssetId == 0U || record->style.IsLoaded());
}
std::size_t SceneUIDocumentService::ElementCount(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = FindRecord(SceneAccess::State(scene), entity);
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

bool SceneUIDocumentService::QueueConfigureVirtualList(Scene& scene, SceneEntity entity, UIElementId element,
    std::uint32_t viewportItems, std::uint32_t overscan) noexcept {
    if (viewportItems == 0U || viewportItems > kMaxUIVirtualListViewportItems || overscan > kMaxUIVirtualListOverscanItems) return false;
    SceneState& state = SceneAccess::State(scene);
    UIDocumentRuntimeRecord* record = FindMutable(state, entity);
    if (record == nullptr) return false;
    const auto current = record->elements.find(element);
    if (current == record->elements.end() || current->second.control.kind != UIControlKind::List ||
        IsPendingDestroyAncestor(state, *record, entity, element) || state.pendingUICommands.size() >= kMaxPendingUICommands) return false;
    state.pendingUICommands.push_back(UIRuntimeCommand{
        .kind = UIRuntimeCommandKind::ConfigureVirtualList,
        .entity = entity,
        .elementId = element,
        .viewportItems = viewportItems,
        .overscan = overscan,
    });
    return true;
}

bool SceneUIDocumentService::QueueScrollVirtualListTo(Scene& scene, SceneEntity entity, UIElementId element,
    std::uint32_t firstVisibleIndex) noexcept {
    SceneState& state = SceneAccess::State(scene);
    UIDocumentRuntimeRecord* record = FindMutable(state, entity);
    if (record == nullptr || !record->virtualLists.contains(element) || IsPendingDestroyAncestor(state, *record, entity, element) ||
        state.pendingUICommands.size() >= kMaxPendingUICommands) return false;
    state.pendingUICommands.push_back(UIRuntimeCommand{
        .kind = UIRuntimeCommandKind::ScrollVirtualList,
        .entity = entity,
        .elementId = element,
        .firstVisibleIndex = firstVisibleIndex,
    });
    return true;
}

void SceneUIDocumentService::SynchronizeBindings(Scene& scene, UIBindingDataSource& source) {
    SceneState& state = SceneAccess::State(scene);
    for (auto& [id, record] : state.uiDocuments) {
        static_cast<void>(id);
        for (UIDocumentRuntimeRecord::BindingState& binding : record.bindings) {
            const auto element = record.elements.find(binding.declaration.elementId);
            if (element == record.elements.end()) continue;
            const std::optional<UIBindingValue> controlValue = ReadBoundControl(element->second, binding.declaration);
            if (!controlValue.has_value()) continue;
            const std::optional<UIBindingValue> sourceValue = source.Read(binding.declaration.sourcePath, binding.declaration.valueType);

            const auto queueSourceToControl = [&]() {
                if (!sourceValue.has_value()) return false;
                UIControlState updated = element->second.control;
                if (!ApplyBoundValue(binding.declaration, *sourceValue, updated)) return false;
                return *controlValue == *sourceValue || QueueSetControl(scene, record.entity, binding.declaration.elementId, updated);
            };

            if (!binding.initialized) {
                if (sourceValue.has_value()) {
                    if (!queueSourceToControl()) continue;
                    binding.lastSourceValue = sourceValue;
                    binding.lastControlValue = sourceValue;
                } else {
                    binding.lastSourceValue.reset();
                    binding.lastControlValue = controlValue;
                    if (binding.declaration.direction == UIBindingDirection::TwoWay && source.Write(binding.declaration.sourcePath, *controlValue)) {
                        binding.lastSourceValue = controlValue;
                    }
                }
                binding.initialized = true;
                continue;
            }

            if (sourceValue.has_value() && !SameValue(sourceValue, binding.lastSourceValue)) {
                // Model changes win if both sides changed before the same sync.
                // Record the desired control value before its queued write reaches
                // the next UI boundary, preventing the write from reflecting back.
                if (!queueSourceToControl()) continue;
                binding.lastSourceValue = sourceValue;
                binding.lastControlValue = sourceValue;
                continue;
            }

            if (!sourceValue.has_value()) {
                binding.lastSourceValue.reset();
            }
            if (binding.declaration.direction == UIBindingDirection::TwoWay &&
                !SameValue(controlValue, binding.lastControlValue) &&
                source.Write(binding.declaration.sourcePath, *controlValue)) {
                binding.lastSourceValue = controlValue;
                binding.lastControlValue = controlValue;
            }
        }
    }
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
        const UIDocumentRuntimeRecord* record = FindRecord(state, queued.entity);
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
