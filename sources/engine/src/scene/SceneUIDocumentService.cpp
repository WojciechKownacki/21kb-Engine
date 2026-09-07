#include "scene/SceneUIDocumentService.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputTouchPoint.hpp"
#include "engine/library/EngineLibraryParsing.hpp"
#include "engine/ui/UIAssetValidation.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/ui/UIPresentationBuilder.hpp"
#include "scene/ui/UIHitTester.hpp"

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
constexpr std::uint32_t kNoCapturedTouch = std::numeric_limits<std::uint32_t>::max();

bool ValidControl(const UIControlState& control) noexcept {
    return UIAssetValidation::Control(control);
}

UIElementComponents ComponentsOf(const UIDocumentElement& element) {
    return UIElementComponents{
        .rect = element.rect,
        .canvas = element.canvas,
        .layout = element.layout,
        .paint = element.paint,
        .image = element.image,
        .textStyle = element.textStyle,
        .interaction = element.interaction,
        .effects = element.effects,
    };
}

void ApplyComponents(UIDocumentElement& element, const UIElementComponents& components) {
    element.rect = components.rect;
    element.canvas = components.canvas;
    element.layout = components.layout;
    element.paint = components.paint;
    element.image = components.image;
    element.textStyle = components.textStyle;
    element.interaction = components.interaction;
    element.effects = components.effects;
}

void CompleteRuntimeComponents(UIDocumentElement& element) {
    switch (element.control.kind) {
    case UIControlKind::Text:
    case UIControlKind::Button:
    case UIControlKind::InputField:
    case UIControlKind::Dropdown:
        if (!element.textStyle) element.textStyle = UIText{};
        break;
    case UIControlKind::Image:
        if (!element.image) element.image = UIImage{};
        break;
    default:
        break;
    }
    switch (element.control.kind) {
    case UIControlKind::Button:
    case UIControlKind::Toggle:
    case UIControlKind::Slider:
    case UIControlKind::List:
    case UIControlKind::InputField:
    case UIControlKind::ScrollView:
    case UIControlKind::ModalDialog:
    case UIControlKind::Dropdown:
    case UIControlKind::Scrollbar:
    case UIControlKind::WidgetSwitcher:
        if (!element.interaction) element.interaction = UIInteraction{};
        break;
    default:
        break;
    }
    UIContainerLayoutMode requiredMode = UIContainerLayoutMode::None;
    switch (element.control.kind) {
    case UIControlKind::Overlay: requiredMode = UIContainerLayoutMode::Overlay; break;
    case UIControlKind::HorizontalBox: requiredMode = UIContainerLayoutMode::Horizontal; break;
    case UIControlKind::VerticalBox: requiredMode = UIContainerLayoutMode::Vertical; break;
    case UIControlKind::Grid: requiredMode = UIContainerLayoutMode::Grid; break;
    case UIControlKind::Wrap: requiredMode = UIContainerLayoutMode::Wrap; break;
    default: break;
    }
    if (requiredMode != UIContainerLayoutMode::None) {
        if (!element.layout) {
            element.layout = UIContainerLayout{ .mode = requiredMode };
        }
    }
    if (element.control.kind == UIControlKind::ScrollView) {
        if (!element.effects) {
            element.effects = UIEffects{ .clipChildren = true };
        }
    }
}

bool ValidComponents(const UIElementComponents& components) noexcept {
    return UIAssetValidation::RectTransform(components.rect) &&
        (!components.canvas || UIAssetValidation::Canvas(*components.canvas)) &&
        (!components.layout || UIAssetValidation::ContainerLayout(*components.layout)) &&
        (!components.paint || UIAssetValidation::Paint(*components.paint)) &&
        (!components.image || UIAssetValidation::Image(*components.image)) &&
        (!components.textStyle || UIAssetValidation::Text(*components.textStyle)) &&
        (!components.interaction || UIAssetValidation::Interaction(*components.interaction)) &&
        (!components.effects || UIAssetValidation::Effects(*components.effects));
}

void ApplyControl(UIDocumentElement& element, const UIControlState& control) {
    element.control = control;
    CompleteRuntimeComponents(element);
}

std::size_t DirectChildCount(const UIDocumentRuntimeRecord& record, UIElementId parent) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(record.elements,
        [parent](const auto& entry) { return entry.second.parentId == parent; }));
}

UIDocumentElement RuntimeCandidate(
    UIElementId id,
    const UIRuntimeElementDesc& desc) {
    UIDocumentElement element{
        .id = id,
        .parentId = desc.parentId,
        .name = desc.name,
        .styleClass = desc.styleClass,
        .visible = desc.visible,
        .rect = desc.components.rect,
        .canvas = desc.components.canvas,
        .layout = desc.components.layout,
        .paint = desc.components.paint,
        .image = desc.components.image,
        .textStyle = desc.components.textStyle,
        .interaction = desc.components.interaction,
        .effects = desc.components.effects,
        .control = desc.control,
    };
    CompleteRuntimeComponents(element);
    return element;
}

std::optional<UIDocumentElement> ProjectedElement(
    const SceneState& state,
    SceneEntity entity,
    UIElementId elementId) {
    std::optional<UIDocumentElement> projected;
    if (const UIDocumentRuntimeRecord* record = FindRecord(state, entity); record != nullptr) {
        const auto current = record->elements.find(elementId);
        if (current != record->elements.end()) projected = current->second;
    }
    for (const UIRuntimeCommand& command : state.pendingUICommands) {
        if (command.entity != entity || command.elementId != elementId) continue;
        switch (command.kind) {
        case UIRuntimeCommandKind::Create:
            projected = RuntimeCandidate(elementId, command.create);
            break;
        case UIRuntimeCommandKind::Destroy:
            break;
        case UIRuntimeCommandKind::SetVisible:
            if (projected) projected->visible = command.visible;
            break;
        case UIRuntimeCommandKind::SetControl:
            if (projected) ApplyControl(*projected, command.control);
            break;
        case UIRuntimeCommandKind::SetComponents:
            if (projected) {
                ApplyComponents(*projected, command.elementComponents);
                CompleteRuntimeComponents(*projected);
            }
            break;
        case UIRuntimeCommandKind::ConfigureVirtualList:
        case UIRuntimeCommandKind::ScrollVirtualList:
            break;
        }
    }
    return projected;
}

std::size_t ProjectedDirectChildCount(
    const SceneState& state,
    const UIDocumentRuntimeRecord& record,
    SceneEntity entity,
    UIElementId parent) noexcept {
    std::size_t count = DirectChildCount(record, parent);
    for (const UIRuntimeCommand& command : state.pendingUICommands) {
        if (command.entity != entity) continue;
        if (command.kind == UIRuntimeCommandKind::Create &&
            command.create.parentId == parent) {
            ++count;
        } else if (command.kind == UIRuntimeCommandKind::Destroy) {
            const auto child = record.elements.find(command.elementId);
            if (child != record.elements.end() && child->second.parentId == parent && count > 0U) {
                --count;
            }
        }
    }
    return count;
}

bool SameValue(const std::optional<UIBindingValue>& lhs, const std::optional<UIBindingValue>& rhs) noexcept {
    return lhs.has_value() == rhs.has_value() && (!lhs.has_value() || *lhs == *rhs);
}

std::optional<UIBindingValue> ReadBoundControl(const UIDocumentElement& element, const UIBindingDeclaration& binding) {
    UIBindingValue value{ .type = binding.valueType };
    const UIControlState& control = element.control;
    if (binding.property == "text" &&
        (control.kind == UIControlKind::Text || control.kind == UIControlKind::Button ||
            control.kind == UIControlKind::InputField || control.kind == UIControlKind::Dropdown)) {
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
        if (!kb::library::TryParseDouble(control.text, value.number) || !std::isfinite(value.number)) return std::nullopt;
        return value;
    }
    if (binding.property == "toggle" && binding.valueType == UIDataValueType::Boolean && control.kind == UIControlKind::Toggle) {
        value.boolean = control.toggleValue;
        return value;
    }
    if (binding.property == "value" && binding.valueType == UIDataValueType::Number &&
        (control.kind == UIControlKind::Slider || control.kind == UIControlKind::ProgressBar ||
            control.kind == UIControlKind::Scrollbar)) {
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
        (control.kind == UIControlKind::Text || control.kind == UIControlKind::Button ||
            control.kind == UIControlKind::InputField || control.kind == UIControlKind::Dropdown)) {
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
    if (binding.property == "value" && value.type == UIDataValueType::Number &&
        (control.kind == UIControlKind::Slider || control.kind == UIControlKind::ProgressBar ||
            control.kind == UIControlKind::Scrollbar) &&
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
        !std::isfinite(event.value) || event.text.size() > kMaxUIEventTextBytes ||
        event.eventName.size() > kMaxUIActionNameBytes) {
        return false;
    }
    if (event.kind == UIRuntimeEventKind::Navigation) {
        return event.navigation > UINavigationDirection::None && event.navigation <= UINavigationDirection::Right;
    }
    return event.navigation == UINavigationDirection::None;
}

bool Focusable(const UIDocumentElement& element) noexcept {
    if (!element.visible) return false;
    if (element.interaction.has_value()) {
        return element.interaction->interactable &&
            element.interaction->navigationMode != UINavigationMode::None;
    }
    switch (element.control.kind) {
    case UIControlKind::Container:
    case UIControlKind::Canvas:
    case UIControlKind::Overlay:
    case UIControlKind::HorizontalBox:
    case UIControlKind::VerticalBox:
    case UIControlKind::Grid:
    case UIControlKind::Wrap:
    case UIControlKind::Spacer:
    case UIControlKind::SizeBox:
    case UIControlKind::ScaleBox:
    case UIControlKind::WidgetSwitcher:
        return false;
    default:
        // Documents written before interaction metadata existed treated every
        // non-container element as navigable. Preserve that contract while
        // new documents opt out explicitly with navigationMode=None.
        return true;
    }
}

bool VisibleInTree(const UIDocumentRuntimeRecord& record, UIElementId elementId) noexcept {
    UIElementId current = elementId;
    while (current != 0U) {
        const auto element = record.elements.find(current);
        if (element == record.elements.end() || !element->second.visible ||
            (element->second.control.kind == UIControlKind::ModalDialog &&
                !element->second.control.modalOpen)) return false;
        current = element->second.parentId;
    }
    return true;
}

bool FocusableInTree(const UIDocumentRuntimeRecord& record, const UIDocumentElement& element) noexcept {
    return Focusable(element) && VisibleInTree(record, element.id);
}

void AppendFocusableInAuthoredOrder(
    const UIDocumentRuntimeRecord& record,
    UIElementId parent,
    std::vector<UIElementId>& ordered) {
    std::vector<const UIDocumentElement*> children;
    for (const auto& [id, element] : record.elements) {
        static_cast<void>(id);
        if (element.parentId == parent) children.push_back(&element);
    }
    std::ranges::sort(children, [](const UIDocumentElement* lhs, const UIDocumentElement* rhs) {
        return lhs->siblingOrder != rhs->siblingOrder ? lhs->siblingOrder < rhs->siblingOrder
                                                     : lhs->id < rhs->id;
    });
    for (const UIDocumentElement* child : children) {
        if (FocusableInTree(record, *child)) ordered.push_back(child->id);
        AppendFocusableInAuthoredOrder(record, child->id, ordered);
    }
}

std::vector<UIElementId> FocusableInAuthoredOrder(const UIDocumentRuntimeRecord& record) {
    std::vector<UIElementId> ordered;
    ordered.reserve(record.elements.size());
    AppendFocusableInAuthoredOrder(record, record.root, ordered);
    return ordered;
}

UIElementId FirstFocusable(const UIDocumentRuntimeRecord& record) {
    const std::vector<UIElementId> ordered = FocusableInAuthoredOrder(record);
    return ordered.empty() ? 0U : ordered.front();
}

UIElementId NavigateFocusable(const UIDocumentRuntimeRecord& record, UIElementId current, bool forward) {
    const std::vector<UIElementId> ordered = FocusableInAuthoredOrder(record);
    if (ordered.empty()) return 0U;
    const auto position = std::ranges::find(ordered, current);
    if (position == ordered.end()) return ordered.front();
    const std::size_t index = static_cast<std::size_t>(position - ordered.begin());
    if (forward) return ordered[(index + 1U) % ordered.size()];
    return ordered[index == 0U ? ordered.size() - 1U : index - 1U];
}

UIElementId NavigateTarget(
    const UIDocumentRuntimeRecord& record,
    UIElementId current,
    bool forward,
    UINavigationDirection direction) {
    const auto currentElement = record.elements.find(current);
    if (currentElement != record.elements.end() &&
        currentElement->second.interaction.has_value() &&
        currentElement->second.interaction->navigationMode == UINavigationMode::Explicit) {
        const UIInteraction& interaction = *currentElement->second.interaction;
        UIElementId target = 0U;
        switch (direction) {
        case UINavigationDirection::Up: target = interaction.navigationUp; break;
        case UINavigationDirection::Down: target = interaction.navigationDown; break;
        case UINavigationDirection::Left: target = interaction.navigationLeft; break;
        case UINavigationDirection::Right: target = interaction.navigationRight; break;
        case UINavigationDirection::Next: target = interaction.navigationDown; break;
        case UINavigationDirection::Previous: target = interaction.navigationUp; break;
        case UINavigationDirection::None: break;
        }
        const auto explicitTarget = record.elements.find(target);
        return explicitTarget != record.elements.end() &&
            FocusableInTree(record, explicitTarget->second) ? target : 0U;
    }
    return NavigateFocusable(record, current, forward);
}

bool QueueFocusChange(SceneState& state, UIDocumentRuntimeRecord& record, UIElementId focused) {
    if (record.focusedElement == focused) return true;
    const std::size_t eventCount = (record.focusedElement != 0U ? 1U : 0U) + (focused != 0U ? 1U : 0U);
    if (state.pendingUIEvents.size() > kMaxPendingUIEvents - eventCount) return false;
    if (record.focusedElement != 0U) {
        state.pendingUIEvents.push_back(PendingUIRuntimeEvent{ .entity = record.entity,
            .event = UIRuntimeEvent{ .kind = UIRuntimeEventKind::Focus, .elementId = record.focusedElement, .focused = false } });
    }
    record.focusedElement = focused;
    if (focused != 0U) {
        state.pendingUIEvents.push_back(PendingUIRuntimeEvent{ .entity = record.entity,
            .event = UIRuntimeEvent{ .kind = UIRuntimeEventKind::Focus, .elementId = focused, .focused = true } });
    }
    return true;
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
            std::uint32_t siblingOrder = 0U;
            for (const auto& [id, sibling] : record->elements) {
                static_cast<void>(id);
                if (sibling.parentId == command.create.parentId) {
                    siblingOrder = std::max(siblingOrder, sibling.siblingOrder + 1U);
                }
            }
            UIDocumentElement created{
                .id = command.elementId,
                .parentId = command.create.parentId,
                .siblingOrder = siblingOrder,
                .name = command.create.name,
                .styleClass = command.create.styleClass,
                .visible = command.create.visible,
                .rect = command.create.components.rect,
                .canvas = command.create.components.canvas,
                .layout = command.create.components.layout,
                .paint = command.create.components.paint,
                .image = command.create.components.image,
                .textStyle = command.create.components.textStyle,
                .interaction = command.create.components.interaction,
                .effects = command.create.components.effects,
                .control = command.create.control,
            };
            CompleteRuntimeComponents(created);
            record->elements.emplace(command.elementId, std::move(created));
            break;
        }
        case UIRuntimeCommandKind::Destroy:
            if (command.elementId != record->root && record->elements.contains(command.elementId)) {
                const UIElementId parentId = record->elements.find(command.elementId)->second.parentId;
                DestroySubtree(*record, command.elementId);
                const auto parent = record->elements.find(parentId);
                if (parent != record->elements.end() &&
                    parent->second.control.kind == UIControlKind::WidgetSwitcher) {
                    const std::size_t childCount = DirectChildCount(*record, parentId);
                    parent->second.control.selectedIndex = childCount == 0U ? 0U :
                        std::min(parent->second.control.selectedIndex,
                            static_cast<std::uint32_t>(childCount - 1U));
                }
            }
            break;
        case UIRuntimeCommandKind::SetVisible: {
            const auto element = record->elements.find(command.elementId);
            if (element != record->elements.end()) element->second.visible = command.visible;
            break;
        }
        case UIRuntimeCommandKind::SetControl: {
            const auto element = record->elements.find(command.elementId);
            if (element != record->elements.end()) ApplyControl(element->second, command.control);
            break;
        }
        case UIRuntimeCommandKind::SetComponents: {
            const auto element = record->elements.find(command.elementId);
            if (element != record->elements.end()) {
                ApplyComponents(element->second, command.elementComponents);
                CompleteRuntimeComponents(element->second);
            }
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
    const SceneState& state = SceneAccess::State(scene);
    const std::optional<UIDocumentElement> projected = ProjectedElement(state, entity, element);
    return projected ? std::optional<UIControlState>{ projected->control } : std::nullopt;
}
std::optional<UIElementComponents> SceneUIDocumentService::ElementComponents(
    const Scene& scene,
    SceneEntity entity,
    UIElementId element) {
    const SceneState& state = SceneAccess::State(scene);
    const std::optional<UIDocumentElement> projected = ProjectedElement(state, entity, element);
    return projected ? std::optional<UIElementComponents>{ ComponentsOf(*projected) } : std::nullopt;
}
UIElementId SceneUIDocumentService::Focused(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = FindRecord(SceneAccess::State(scene), entity);
    return record != nullptr ? record->focusedElement : 0U;
}
bool SceneUIDocumentService::HasFocusedTextInput(const Scene& scene) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    const UIDocumentRuntimeRecord* record = FindRecord(state, state.activeUIDocument);
    if (record == nullptr) return false;
    const auto focused = record->elements.find(record->focusedElement);
    return focused != record->elements.end() &&
        focused->second.control.kind == UIControlKind::InputField &&
        FocusableInTree(*record, focused->second);
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
    // No name index by design. This deterministic O(n) setup scan returns no
    // handle for duplicate names rather than binding to an arbitrary element.
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
    if (desc.name.empty() || desc.parentId == 0U || desc.control.kind == UIControlKind::Canvas ||
        !ValidControl(desc.control) || !ValidComponents(desc.components) || desc.components.canvas.has_value()) {
        return std::nullopt;
    }
    SceneState& state = SceneAccess::State(scene);
    UIDocumentRuntimeRecord* record = FindMutable(state, entity);
    if (record == nullptr || state.pendingUICommands.size() >= kMaxPendingUICommands ||
        IsPendingDestroyAncestor(state, *record, entity, desc.parentId) ||
        (!record->elements.contains(desc.parentId) && !HasPendingCreate(state, entity, desc.parentId))) return std::nullopt;
    if (record->nextRuntimeElementId == 0U) return std::nullopt;
    UIDocumentElement candidate = RuntimeCandidate(record->nextRuntimeElementId, desc);
    if (!UIAssetValidation::ElementComposition(candidate, false, 0U)) return std::nullopt;
    UIRuntimeElementDesc normalized = desc;
    normalized.components = ComponentsOf(candidate);
    normalized.control = candidate.control;
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
        .create = std::move(normalized),
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
const UIPresentationSnapshot& SceneUIDocumentService::Presentation(const Scene& scene) noexcept {
    return SceneAccess::State(scene).uiPresentation;
}
const UIPresentationSnapshot& SceneUIDocumentService::BuildPresentation(
    Scene& scene,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight) {
    SceneState& state = SceneAccess::State(scene);
    UIPresentationBuilder::Build(state, viewportWidth, viewportHeight);
    return state.uiPresentation;
}

bool SceneUIDocumentService::QueueSetControl(Scene& scene, SceneEntity entity, UIElementId element, const UIControlState& control) {
    if (!ValidControl(control)) return false;
    SceneState& state = SceneAccess::State(scene);
    UIDocumentRuntimeRecord* record = FindMutable(state, entity);
    if (record == nullptr) return false;
    if (!record->elements.contains(element)) {
        for (UIRuntimeCommand& command : state.pendingUICommands) {
            if (command.entity != entity || command.elementId != element ||
                command.kind != UIRuntimeCommandKind::Create) continue;
            if (command.create.control.kind != control.kind) return false;
            UIDocumentElement candidate = RuntimeCandidate(element, command.create);
            ApplyControl(candidate, control);
            if (!UIAssetValidation::ElementComposition(candidate, false,
                ProjectedDirectChildCount(state, *record, entity, element))) return false;
            command.create.control = candidate.control;
            command.create.components = ComponentsOf(candidate);
            return true;
        }
        return false;
    }
    if (IsPendingDestroyAncestor(state, *record, entity, element) ||
        state.pendingUICommands.size() >= kMaxPendingUICommands) return false;
    const auto current = record->elements.find(element);
    if (current->second.control.kind != control.kind) return false;
    UIDocumentElement candidate = current->second;
    ApplyControl(candidate, control);
    if (!UIAssetValidation::ElementComposition(candidate, element == record->root,
        ProjectedDirectChildCount(state, *record, entity, element))) return false;
    state.pendingUICommands.push_back(UIRuntimeCommand{
        .kind = UIRuntimeCommandKind::SetControl,
        .entity = entity,
        .elementId = element,
        .control = control,
    });
    return true;
}

bool SceneUIDocumentService::QueueSetComponents(
    Scene& scene,
    SceneEntity entity,
    UIElementId element,
    const UIElementComponents& components) {
    if (!ValidComponents(components)) return false;
    SceneState& state = SceneAccess::State(scene);
    UIDocumentRuntimeRecord* record = FindMutable(state, entity);
    if (record == nullptr) return false;
    if (!record->elements.contains(element)) {
        if (components.canvas.has_value()) return false;
        for (UIRuntimeCommand& command : state.pendingUICommands) {
            if (command.entity != entity || command.elementId != element ||
                command.kind != UIRuntimeCommandKind::Create) continue;
            UIDocumentElement candidate = RuntimeCandidate(element, command.create);
            ApplyComponents(candidate, components);
            CompleteRuntimeComponents(candidate);
            if (!UIAssetValidation::ElementComposition(candidate, false,
                ProjectedDirectChildCount(state, *record, entity, element))) return false;
            command.create.control = candidate.control;
            command.create.components = ComponentsOf(candidate);
            return true;
        }
        return false;
    }
    if (IsPendingDestroyAncestor(state, *record, entity, element) ||
        state.pendingUICommands.size() >= kMaxPendingUICommands) return false;
    const bool isRoot = element == record->root;
    if (components.canvas.has_value() != isRoot) return false;
    UIDocumentElement candidate = record->elements.find(element)->second;
    ApplyComponents(candidate, components);
    CompleteRuntimeComponents(candidate);
    if (!UIAssetValidation::ElementComposition(candidate, isRoot,
        ProjectedDirectChildCount(state, *record, entity, element))) return false;
    state.pendingUICommands.push_back(UIRuntimeCommand{
        .kind = UIRuntimeCommandKind::SetComponents,
        .entity = entity,
        .elementId = element,
        .elementComponents = components,
    });
    return true;
}

bool SceneUIDocumentService::QueueFocus(Scene& scene, SceneEntity entity, UIElementId element) noexcept {
    SceneState& state = SceneAccess::State(scene);
    UIDocumentRuntimeRecord* record = FindMutable(state, entity);
    if (record == nullptr) return false;
    const auto target = record->elements.find(element);
    if (target == record->elements.end() || !FocusableInTree(*record, target->second) ||
        IsPendingDestroyAncestor(state, *record, entity, element)) return false;
    if (!QueueFocusChange(state, *record, element)) return false;
    state.activeUIDocument = entity;
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
    if (element == record->elements.end() ||
        !VisibleInTree(*record, event.elementId)) return false;
    UIRuntimeEvent queued = event;
    if (queued.eventName.empty() && (queued.kind == UIRuntimeEventKind::Click ||
        queued.kind == UIRuntimeEventKind::Submit) && element->second.interaction.has_value()) {
        queued.eventName = element->second.interaction->eventName;
    }
    state.pendingUIEvents.push_back(PendingUIRuntimeEvent{ .entity = entity, .event = std::move(queued) });
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
        // ScriptRuntimeSceneSystem drains UI events before the UI scene
        // system reaches its command boundary. A Destroy queued in the same
        // prior script frame must therefore suppress the event here as well:
        // otherwise a callback can observe an element that is already
        // scheduled to die later in this frame.
        if (element != record->elements.end() && VisibleInTree(*record, queued.event.elementId) &&
            !IsPendingDestroyAncestor(state, *record, queued.entity, queued.event.elementId)) {
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
        state.uiPresentation.Clear();
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
    if (state.uiPresentation.viewportWidth > 0U &&
        state.uiPresentation.viewportHeight > 0U) {
        UIPresentationBuilder::Build(state, state.uiPresentation.viewportWidth,
            state.uiPresentation.viewportHeight);
    }
}

namespace {

[[nodiscard]] const UIPresentationHitTarget* FindPresentedTarget(
    const UIPresentationSnapshot& snapshot,
    SceneEntity owner,
    UIElementId elementId) noexcept {
    for (const UIPresentationHitTarget& target : snapshot.hitTargets) {
        if (target.owner == owner && target.elementId == elementId) return &target;
    }
    return nullptr;
}

[[nodiscard]] std::string ElementActionName(const UIDocumentElement& element) {
    return element.interaction.has_value() ? element.interaction->eventName : std::string{};
}

[[nodiscard]] bool AppendRuntimeEvent(
    SceneState& state,
    const UIDocumentRuntimeRecord& record,
    const UIDocumentElement& element,
    UIRuntimeEvent event) {
    if (state.pendingUIEvents.size() >= kMaxPendingUIEvents) return false;
    event.elementId = element.id;
    if (event.eventName.empty() && (event.kind == UIRuntimeEventKind::Click ||
        event.kind == UIRuntimeEventKind::Submit)) {
        event.eventName = ElementActionName(element);
    }
    state.pendingUIEvents.push_back(PendingUIRuntimeEvent{
        .entity = record.entity,
        .event = std::move(event),
    });
    return true;
}

[[nodiscard]] bool AppendUtf8(std::string& text, char32_t codePoint) {
    char bytes[4]{};
    std::size_t count = 0U;
    if (codePoint <= 0x7FU) {
        bytes[0] = static_cast<char>(codePoint);
        count = 1U;
    } else if (codePoint <= 0x7FFU) {
        bytes[0] = static_cast<char>(0xC0U | (codePoint >> 6U));
        bytes[1] = static_cast<char>(0x80U | (codePoint & 0x3FU));
        count = 2U;
    } else if (codePoint <= 0xFFFFU) {
        bytes[0] = static_cast<char>(0xE0U | (codePoint >> 12U));
        bytes[1] = static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
        bytes[2] = static_cast<char>(0x80U | (codePoint & 0x3FU));
        count = 3U;
    } else if (codePoint <= 0x10FFFFU) {
        bytes[0] = static_cast<char>(0xF0U | (codePoint >> 18U));
        bytes[1] = static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU));
        bytes[2] = static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
        bytes[3] = static_cast<char>(0x80U | (codePoint & 0x3FU));
        count = 4U;
    }
    if (count == 0U || text.size() > kMaxUIEventTextBytes - count) return false;
    text.append(bytes, count);
    return true;
}

void RemoveLastUtf8(std::string& text) noexcept {
    if (text.empty()) return;
    std::size_t index = text.size() - 1U;
    while (index > 0U && (static_cast<unsigned char>(text[index]) & 0xC0U) == 0x80U) --index;
    text.erase(index);
}

[[nodiscard]] bool SetRangeValueFromPointer(
    SceneState& state,
    UIDocumentRuntimeRecord& record,
    UIDocumentElement& element,
    const UIPresentationHitTarget& target,
    float pointerX,
    float pointerY) {
    if ((element.control.kind != UIControlKind::Slider &&
        element.control.kind != UIControlKind::Scrollbar) ||
        target.rect.Width() <= 0.0F) return false;
    const float ratio = std::clamp((pointerX - target.rect.left) /
        target.rect.Width(), 0.0F, 1.0F);
    const float value = element.control.sliderMinimum + ratio *
        (element.control.sliderMaximum - element.control.sliderMinimum);
    if (value == element.control.sliderValue) return true;
    element.control.sliderValue = value;
    return AppendRuntimeEvent(state, record, element, UIRuntimeEvent{
        .kind = UIRuntimeEventKind::Changed,
        .pointerX = pointerX,
        .pointerY = pointerY,
        .value = value,
    });
}

[[nodiscard]] UIDocumentElement* FindScrollOwner(
    UIDocumentRuntimeRecord& record,
    UIElementId elementId) noexcept {
    auto element = record.elements.find(elementId);
    while (element != record.elements.end()) {
        if (element->second.control.kind == UIControlKind::ScrollView) return &element->second;
        if (element->second.parentId == 0U) return nullptr;
        element = record.elements.find(element->second.parentId);
    }
    return nullptr;
}

void ApplyDiscreteControlState(
    SceneState& state,
    UIDocumentRuntimeRecord& record,
    UIDocumentElement& element) {
    if (element.control.kind == UIControlKind::Toggle) {
        element.control.toggleValue = !element.control.toggleValue;
        static_cast<void>(AppendRuntimeEvent(state, record, element,
            UIRuntimeEvent{ .kind = UIRuntimeEventKind::Changed,
                .value = element.control.toggleValue ? 1.0F : 0.0F }));
    }
    if (element.control.kind == UIControlKind::Dropdown &&
        !element.control.listItems.empty()) {
        element.control.selectedIndex = (element.control.selectedIndex + 1U) %
            static_cast<std::uint32_t>(element.control.listItems.size());
        static_cast<void>(AppendRuntimeEvent(state, record, element,
            UIRuntimeEvent{ .kind = UIRuntimeEventKind::Changed,
                .value = static_cast<float>(element.control.selectedIndex),
                .text = element.control.listItems[element.control.selectedIndex] }));
    }
    if (element.control.kind == UIControlKind::WidgetSwitcher) {
        const std::size_t childCount = DirectChildCount(record, element.id);
        if (childCount > 0U) {
            element.control.selectedIndex = (element.control.selectedIndex + 1U) %
                static_cast<std::uint32_t>(childCount);
            static_cast<void>(AppendRuntimeEvent(state, record, element,
                UIRuntimeEvent{ .kind = UIRuntimeEventKind::Changed,
                    .value = static_cast<float>(element.control.selectedIndex) }));
        }
    }
}

} // namespace

void SceneUIDocumentService::RouteInput(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    const kb::input::InputDeviceState& device = scene.Input().DeviceState();
    if (device.PointerViewportWidth() > 0U &&
        device.PointerViewportHeight() > 0U &&
        (state.uiPresentation.viewportWidth != device.PointerViewportWidth() ||
            state.uiPresentation.viewportHeight != device.PointerViewportHeight())) {
        UIPresentationBuilder::Build(state, device.PointerViewportWidth(),
            device.PointerViewportHeight());
    }
    float pointerX = device.PointerX();
    float pointerY = device.PointerY();
    std::uint32_t touchId = kNoCapturedTouch;
    bool touchDown = false;
    for (const kb::input::InputTouchPoint& touch : device.TouchPoints()) {
        if (touch.phase == kb::input::InputTouchPhase::Ended) {
            if (touch.id == state.uiCapturedTouchId) {
                pointerX = touch.x;
                pointerY = touch.y;
            }
            continue;
        }
        if (!touchDown || touch.id == state.uiCapturedTouchId) {
            pointerX = touch.x;
            pointerY = touch.y;
            touchId = touch.id;
            touchDown = true;
        }
    }
    const bool mouseDown = device.IsKeyDown(kb::input::InputKey::MouseLeft);
    const bool pointerDown = mouseDown || touchDown;
    const bool submitDown = device.IsKeyDown(kb::input::InputKey::GamepadFaceBottom) ||
        device.IsKeyDown(kb::input::InputKey::Enter);
    const bool nextDown = device.IsKeyDown(kb::input::InputKey::GamepadDPadDown) ||
        device.IsKeyDown(kb::input::InputKey::GamepadDPadRight) ||
        device.IsKeyDown(kb::input::InputKey::Tab);
    const bool previousDown = device.IsKeyDown(kb::input::InputKey::GamepadDPadUp) ||
        device.IsKeyDown(kb::input::InputKey::GamepadDPadLeft);
    const bool backspaceDown = device.IsKeyDown(kb::input::InputKey::Backspace) ||
        device.IsKeyDown(kb::input::InputKey::Delete);
    bool consumedByUI = false;

    if (!device.HasFocus()) {
        for (auto& [id, record] : state.uiDocuments) {
            static_cast<void>(id);
            static_cast<void>(QueueFocusChange(state, record, 0U));
        }
        state.uiCapturedDocument = {};
        state.uiCapturedElement = 0U;
        state.uiCapturedTouchId = kNoCapturedTouch;
    } else {
        UIDocumentRuntimeRecord* active = FindMutable(state, state.activeUIDocument);
        if (active == nullptr && !state.uiDocuments.empty()) {
            active = &state.uiDocuments.begin()->second;
            state.activeUIDocument = active->entity;
        }
        if (active != nullptr) {
            const auto focused = active->elements.find(active->focusedElement);
            if (focused == active->elements.end() ||
                !FocusableInTree(*active, focused->second)) {
                static_cast<void>(QueueFocusChange(state, *active, FirstFocusable(*active)));
            }
            const auto queueNavigation = [&](bool forward, UINavigationDirection direction) {
                const UIElementId target = NavigateTarget(*active, active->focusedElement, forward, direction);
                if (target == 0U || !QueueFocusChange(state, *active, target)) return;
                const auto element = active->elements.find(target);
                if (element == active->elements.end() || !AppendRuntimeEvent(state, *active,
                    element->second, UIRuntimeEvent{ .kind = UIRuntimeEventKind::Navigation,
                        .navigation = direction })) return;
                consumedByUI = true;
            };
            if (!state.uiNextWasDown && nextDown) {
                const UINavigationDirection direction = device.IsKeyDown(kb::input::InputKey::GamepadDPadDown) ?
                    UINavigationDirection::Down :
                    (device.IsKeyDown(kb::input::InputKey::GamepadDPadRight) ?
                        UINavigationDirection::Right : UINavigationDirection::Next);
                queueNavigation(true, direction);
            }
            if (!state.uiPreviousWasDown && previousDown) {
                const UINavigationDirection direction = device.IsKeyDown(kb::input::InputKey::GamepadDPadUp) ?
                    UINavigationDirection::Up : UINavigationDirection::Left;
                queueNavigation(false, direction);
            }

            if (active->focusedElement != 0U) {
                auto focusedElement = active->elements.find(active->focusedElement);
                if (focusedElement != active->elements.end() &&
                    focusedElement->second.control.kind == UIControlKind::InputField) {
                    bool changed = false;
                    for (const char32_t codePoint : device.TextInput()) {
                        if (codePoint < U' ' ||
                            (codePoint >= U'\u007F' && codePoint <= U'\u009F')) continue;
                        changed = AppendUtf8(focusedElement->second.control.text, codePoint) || changed;
                    }
                    if (!state.uiBackspaceWasDown && backspaceDown &&
                        !focusedElement->second.control.text.empty()) {
                        RemoveLastUtf8(focusedElement->second.control.text);
                        changed = true;
                    }
                    if (changed) {
                        consumedByUI = AppendRuntimeEvent(state, *active,
                            focusedElement->second, UIRuntimeEvent{
                                .kind = UIRuntimeEventKind::Changed,
                                .value = static_cast<float>(focusedElement->second.control.text.size()),
                                .text = focusedElement->second.control.text,
                            }) || consumedByUI;
                    }
                }
                if (!state.uiSubmitWasDown && submitDown) {
                    focusedElement = active->elements.find(active->focusedElement);
                    if (focusedElement != active->elements.end()) {
                        ApplyDiscreteControlState(state, *active, focusedElement->second);
                        consumedByUI = AppendRuntimeEvent(state, *active,
                            focusedElement->second, UIRuntimeEvent{
                                .kind = UIRuntimeEventKind::Submit,
                                .text = focusedElement->second.control.text,
                            }) || consumedByUI;
                    }
                }
            }
        }

        if (!state.uiPointerWasDown && pointerDown) {
            const UIPresentationHitTarget* target = UIHitTester::Topmost(
                state.uiPresentation, pointerX, pointerY);
            if (target != nullptr) {
                UIDocumentRuntimeRecord* record = FindMutable(state, target->owner);
                if (record != nullptr) {
                    auto element = record->elements.find(target->elementId);
                    if (element != record->elements.end()) {
                        state.activeUIDocument = record->entity;
                        state.uiCapturedDocument = record->entity;
                        state.uiCapturedElement = element->first;
                        state.uiCapturedTouchId = touchId;
                        if (FocusableInTree(*record, element->second)) {
                            static_cast<void>(QueueFocusChange(state, *record, element->first));
                        }
                        static_cast<void>(AppendRuntimeEvent(state, *record, element->second,
                            UIRuntimeEvent{ .kind = UIRuntimeEventKind::Pointer,
                                .pointerX = pointerX, .pointerY = pointerY }));
                        static_cast<void>(SetRangeValueFromPointer(state, *record,
                            element->second, *target, pointerX, pointerY));
                        consumedByUI = true;
                    }
                }
            } else if (active != nullptr && active->focusedElement != 0U &&
                state.uiPresentation.viewportWidth == 0U) {
                // Headless callers without a viewport retain deterministic
                // keyboard-style activation of the focused control.
                state.uiCapturedDocument = active->entity;
                state.uiCapturedElement = active->focusedElement;
                state.uiCapturedTouchId = touchId;
                consumedByUI = true;
            }
        } else if (pointerDown && state.uiCapturedElement != 0U) {
            UIDocumentRuntimeRecord* record = FindMutable(state, state.uiCapturedDocument);
            if (record != nullptr) {
                auto element = record->elements.find(state.uiCapturedElement);
                const UIPresentationHitTarget* target = FindPresentedTarget(
                    state.uiPresentation, record->entity, state.uiCapturedElement);
                if (element != record->elements.end() && target != nullptr) {
                    static_cast<void>(SetRangeValueFromPointer(state, *record,
                        element->second, *target, pointerX, pointerY));
                }
                consumedByUI = true;
            }
        } else if (state.uiPointerWasDown && !pointerDown &&
            state.uiCapturedElement != 0U) {
            UIDocumentRuntimeRecord* record = FindMutable(state, state.uiCapturedDocument);
            if (record != nullptr) {
                auto element = record->elements.find(state.uiCapturedElement);
                const UIPresentationHitTarget* releasedOver = UIHitTester::Topmost(
                    state.uiPresentation, pointerX, pointerY);
                const bool headless = state.uiPresentation.viewportWidth == 0U;
                if (element != record->elements.end() && (headless ||
                    (releasedOver != nullptr && releasedOver->owner == record->entity &&
                        releasedOver->elementId == element->first))) {
                    ApplyDiscreteControlState(state, *record, element->second);
                    static_cast<void>(AppendRuntimeEvent(state, *record, element->second,
                        UIRuntimeEvent{ .kind = UIRuntimeEventKind::Click,
                            .pointerX = pointerX, .pointerY = pointerY }));
                }
                consumedByUI = true;
            }
            state.uiCapturedDocument = {};
            state.uiCapturedElement = 0U;
            state.uiCapturedTouchId = kNoCapturedTouch;
        }

        const float wheel = device.GetValue(kb::input::InputKey::MouseWheel);
        if (wheel != 0.0F) {
            const UIPresentationHitTarget* target = UIHitTester::Topmost(
                state.uiPresentation, pointerX, pointerY);
            if (target != nullptr) {
                UIDocumentRuntimeRecord* record = FindMutable(state, target->owner);
                UIDocumentElement* scroll = record != nullptr ?
                    FindScrollOwner(*record, target->elementId) : nullptr;
                if (record != nullptr && scroll != nullptr) {
                    scroll->control.scrollOffset = std::max(0.0F,
                        scroll->control.scrollOffset - wheel * 32.0F);
                    static_cast<void>(AppendRuntimeEvent(state, *record, *scroll,
                        UIRuntimeEvent{ .kind = UIRuntimeEventKind::Changed,
                            .value = scroll->control.scrollOffset }));
                    consumedByUI = true;
                }
            }
        }
    }
    scene.Input().SetGameplayInputConsumed(consumedByUI);
    state.uiPointerWasDown = pointerDown;
    state.uiSubmitWasDown = submitDown;
    state.uiNextWasDown = nextDown;
    state.uiPreviousWasDown = previousDown;
    state.uiBackspaceWasDown = backspaceDown;
}

} // namespace kb::scene
