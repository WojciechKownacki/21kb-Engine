#include "scene/user_widget/UserWidgetEditorDocument.hpp"

#include "engine/scene/UIAssetIO.hpp"
#include "engine/ui/UIAssetValidation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <unordered_set>

namespace kb::editor {
namespace {

[[nodiscard]] kb::scene::UIDocumentElement MakeElement(kb::scene::UIElementId id, kb::scene::UIElementId parentId,
                                                       std::uint32_t siblingOrder, kb::scene::UIControlKind kind) {
    kb::scene::UIDocumentElement element;
    element.id = id;
    element.parentId = parentId;
    element.siblingOrder = siblingOrder;
    element.name = std::string{kb::scene::UIControlKindName(kind)};
    element.control.kind = kind;

    if (kind == kb::scene::UIControlKind::Canvas) {
        element.canvas = kb::scene::UICanvas{};
        element.rect.anchorMax = {1.0F, 1.0F};
        element.rect.offsetMax = {};
    }
    if (kind == kb::scene::UIControlKind::Image) {
        element.image = kb::scene::UIImage{};
    }
    if (kind == kb::scene::UIControlKind::Text || kind == kb::scene::UIControlKind::Button ||
        kind == kb::scene::UIControlKind::InputField || kind == kb::scene::UIControlKind::Dropdown) {
        element.textStyle = kb::scene::UIText{};
    }
    if (kind == kb::scene::UIControlKind::Button || kind == kb::scene::UIControlKind::Toggle ||
        kind == kb::scene::UIControlKind::Slider || kind == kb::scene::UIControlKind::List ||
        kind == kb::scene::UIControlKind::InputField || kind == kb::scene::UIControlKind::ScrollView ||
        kind == kb::scene::UIControlKind::ModalDialog || kind == kb::scene::UIControlKind::Dropdown ||
        kind == kb::scene::UIControlKind::Scrollbar) {
        element.interaction = kb::scene::UIInteraction{};
    }

    std::optional<kb::scene::UIContainerLayoutMode> layoutMode;
    switch (kind) {
    case kb::scene::UIControlKind::Overlay:
        layoutMode = kb::scene::UIContainerLayoutMode::Overlay;
        break;
    case kb::scene::UIControlKind::HorizontalBox:
        layoutMode = kb::scene::UIContainerLayoutMode::Horizontal;
        break;
    case kb::scene::UIControlKind::VerticalBox:
        layoutMode = kb::scene::UIContainerLayoutMode::Vertical;
        break;
    case kb::scene::UIControlKind::Grid:
        layoutMode = kb::scene::UIContainerLayoutMode::Grid;
        break;
    case kb::scene::UIControlKind::Wrap:
        layoutMode = kb::scene::UIContainerLayoutMode::Wrap;
        break;
    default:
        break;
    }
    if (layoutMode) {
        element.layout = kb::scene::UIContainerLayout{};
        element.layout->mode = *layoutMode;
    }
    return element;
}

[[nodiscard]] std::size_t DirectChildCount(const kb::scene::UIDocument& document,
                                           kb::scene::UIElementId parentId) noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(document.elements, [parentId](const kb::scene::UIDocumentElement& element) {
            return element.parentId == parentId;
        }));
}

[[nodiscard]] bool NavigationTargetsExist(const kb::scene::UIDocument& document,
                                          const kb::scene::UIDocumentElement& element) noexcept {
    if (!element.interaction || element.interaction->navigationMode != kb::scene::UINavigationMode::Explicit) {
        return true;
    }
    for (const kb::scene::UIElementId target :
         {element.interaction->navigationUp, element.interaction->navigationDown, element.interaction->navigationLeft,
          element.interaction->navigationRight}) {
        if (target == 0U)
            continue;
        if (target == element.id ||
            !std::ranges::any_of(document.elements, [target](const kb::scene::UIDocumentElement& candidate) {
                return candidate.id == target;
            })) {
            return false;
        }
    }
    return true;
}

} // namespace

kb::scene::UIDocument UserWidgetEditorDocument::CreateCanvasDocument() {
    kb::scene::UIDocument document;
    document.elements.push_back(MakeElement(1U, 0U, 0U, kb::scene::UIControlKind::Canvas));
    return document;
}

bool UserWidgetEditorDocument::Open(kb::assets::AssetId assetId, const std::filesystem::path& path) {
    if (!assetId.IsValid() || path.empty())
        return false;
    std::optional<kb::scene::UIDocument> loaded = kb::scene::UIAssetIO::LoadDocument(path);
    if (!loaded)
        return false;
    assetId_ = assetId;
    path_ = path;
    document_ = std::move(*loaded);
    selectedElementId_ = document_->elements.front().id;
    revision_ = 0U;
    savedRevision_ = 0U;
    nextRevision_ = 1U;
    undo_.clear();
    redo_.clear();
    previewDrag_.reset();
    return true;
}

bool UserWidgetEditorDocument::Save() {
    if (!document_ || path_.empty() || !kb::scene::UIAssetIO::SaveDocument(path_, *document_)) {
        return false;
    }
    savedRevision_ = revision_;
    return true;
}

void UserWidgetEditorDocument::Close() noexcept {
    assetId_ = {};
    path_.clear();
    document_.reset();
    selectedElementId_ = 0U;
    revision_ = 0U;
    savedRevision_ = 0U;
    nextRevision_ = 1U;
    undo_.clear();
    redo_.clear();
    previewDrag_.reset();
}

bool UserWidgetEditorDocument::HasOpenDocument() const noexcept {
    return document_.has_value();
}

bool UserWidgetEditorDocument::IsDirty() const noexcept {
    return document_ && revision_ != savedRevision_;
}

kb::assets::AssetId UserWidgetEditorDocument::AssetId() const noexcept {
    return assetId_;
}

const std::filesystem::path& UserWidgetEditorDocument::Path() const noexcept {
    return path_;
}

const kb::scene::UIDocument* UserWidgetEditorDocument::Document() const noexcept {
    return document_ ? &*document_ : nullptr;
}

kb::scene::UIElementId UserWidgetEditorDocument::SelectedElementId() const noexcept {
    return selectedElementId_;
}

const kb::scene::UIDocumentElement* UserWidgetEditorDocument::SelectedElement() const noexcept {
    return FindElement(selectedElementId_);
}

bool UserWidgetEditorDocument::SelectElement(kb::scene::UIElementId elementId) noexcept {
    if (FindElement(elementId) == nullptr)
        return false;
    selectedElementId_ = elementId;
    return true;
}

std::optional<kb::scene::UIElementId> UserWidgetEditorDocument::AddElement(kb::scene::UIControlKind kind,
                                                                           kb::scene::UIElementId parentId) {
    if (!document_ || kind == kb::scene::UIControlKind::Canvas) {
        return std::nullopt;
    }
    if (parentId == 0U)
        parentId = selectedElementId_;
    if (FindElement(parentId) == nullptr)
        return std::nullopt;

    kb::scene::UIElementId nextId = 1U;
    for (const kb::scene::UIDocumentElement& element : document_->elements) {
        if (element.id < nextId)
            continue;
        if (element.id == std::numeric_limits<kb::scene::UIElementId>::max()) {
            return std::nullopt;
        }
        nextId = element.id + 1U;
    }
    const auto siblingOrder = static_cast<std::uint32_t>(DirectChildCount(*document_, parentId));
    kb::scene::UIDocumentElement element = MakeElement(nextId, parentId, siblingOrder, kind);
    if (!kb::scene::UIAssetValidation::ElementComposition(element, false, 0U)) {
        return std::nullopt;
    }
    BeginMutation();
    document_->elements.push_back(std::move(element));
    selectedElementId_ = nextId;
    return nextId;
}

bool UserWidgetEditorDocument::RemoveElement(kb::scene::UIElementId elementId) {
    const kb::scene::UIDocumentElement* element = FindElement(elementId);
    if (!document_ || element == nullptr || element->parentId == 0U) {
        return false;
    }
    const kb::scene::UIElementId previousParent = element->parentId;
    std::unordered_set<kb::scene::UIElementId> removed{elementId};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const kb::scene::UIDocumentElement& candidate : document_->elements) {
            if (removed.contains(candidate.parentId) && removed.insert(candidate.id).second) {
                changed = true;
            }
        }
    }
    BeginMutation();
    std::erase_if(document_->elements,
                  [&removed](const kb::scene::UIDocumentElement& candidate) { return removed.contains(candidate.id); });
    std::erase_if(document_->bindings, [&removed](const kb::scene::UIBindingDeclaration& binding) {
        return removed.contains(binding.elementId);
    });
    NormalizeSiblingOrder(previousParent);
    NormalizeWidgetSwitcherSelection(previousParent);
    selectedElementId_ = previousParent;
    return true;
}

bool UserWidgetEditorDocument::ReparentElement(kb::scene::UIElementId elementId, kb::scene::UIElementId parentId) {
    kb::scene::UIDocumentElement* element = FindElement(elementId);
    if (!document_ || element == nullptr || element->parentId == 0U || FindElement(parentId) == nullptr ||
        elementId == parentId || IsDescendant(parentId, elementId)) {
        return false;
    }
    if (element->parentId == parentId)
        return true;
    const kb::scene::UIElementId previousParent = element->parentId;
    const auto newOrder = static_cast<std::uint32_t>(DirectChildCount(*document_, parentId));
    BeginMutation();
    element = FindElement(elementId);
    element->parentId = parentId;
    element->siblingOrder = newOrder;
    NormalizeSiblingOrder(previousParent);
    NormalizeWidgetSwitcherSelection(previousParent);
    NormalizeWidgetSwitcherSelection(parentId);
    return true;
}

bool UserWidgetEditorDocument::EditElement(kb::scene::UIElementId elementId,
                                           const kb::scene::UIDocumentElement& edited) {
    kb::scene::UIDocumentElement* current = FindElement(elementId);
    if (!document_ || current == nullptr || edited.name.empty() || edited.control.kind != current->control.kind) {
        return false;
    }
    kb::scene::UIDocumentElement candidate = edited;
    candidate.id = current->id;
    candidate.parentId = current->parentId;
    candidate.siblingOrder = current->siblingOrder;
    const bool isRoot = current->parentId == 0U;
    if (!kb::scene::UIAssetValidation::ElementComposition(candidate, isRoot, DirectChildCount(*document_, elementId)) ||
        !NavigationTargetsExist(*document_, candidate)) {
        return false;
    }
    BeginMutation();
    *FindElement(elementId) = std::move(candidate);
    return true;
}

bool UserWidgetEditorDocument::CanUndo() const noexcept {
    return !undo_.empty();
}

bool UserWidgetEditorDocument::CanRedo() const noexcept {
    return !redo_.empty();
}

bool UserWidgetEditorDocument::Undo() {
    if (!document_ || undo_.empty())
        return false;
    if (redo_.size() == kMaxHistoryStates)
        redo_.erase(redo_.begin());
    redo_.push_back(HistoryState{
        .document = std::move(*document_),
        .selectedElementId = selectedElementId_,
        .revision = revision_,
    });
    HistoryState previous = std::move(undo_.back());
    undo_.pop_back();
    document_ = std::move(previous.document);
    selectedElementId_ = previous.selectedElementId;
    revision_ = previous.revision;
    return true;
}

bool UserWidgetEditorDocument::Redo() {
    if (!document_ || redo_.empty())
        return false;
    if (undo_.size() == kMaxHistoryStates)
        undo_.erase(undo_.begin());
    undo_.push_back(HistoryState{
        .document = std::move(*document_),
        .selectedElementId = selectedElementId_,
        .revision = revision_,
    });
    HistoryState next = std::move(redo_.back());
    redo_.pop_back();
    document_ = std::move(next.document);
    selectedElementId_ = next.selectedElementId;
    revision_ = next.revision;
    return true;
}

bool UserWidgetEditorDocument::BeginPreviewDrag(kb::scene::UIElementId elementId, float pointerX, float pointerY,
                                                float canvasScale) noexcept {
    const kb::scene::UIDocumentElement* element = FindElement(elementId);
    if (element == nullptr || element->parentId == 0U || !std::isfinite(pointerX) || !std::isfinite(pointerY) ||
        !std::isfinite(canvasScale) || canvasScale <= 0.0F) {
        return false;
    }
    selectedElementId_ = elementId;
    previewDrag_ = PreviewDragState{
        .elementId = elementId,
        .pointerX = pointerX,
        .pointerY = pointerY,
        .canvasScale = canvasScale,
    };
    return true;
}

bool UserWidgetEditorDocument::UpdatePreviewDrag(float pointerX, float pointerY) {
    if (!previewDrag_ || !std::isfinite(pointerX) || !std::isfinite(pointerY)) {
        return false;
    }
    const float deltaX = (pointerX - previewDrag_->pointerX) / previewDrag_->canvasScale;
    const float deltaY = (pointerY - previewDrag_->pointerY) / previewDrag_->canvasScale;
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || (deltaX == 0.0F && deltaY == 0.0F)) {
        return false;
    }
    kb::scene::UIDocumentElement* element = FindElement(previewDrag_->elementId);
    if (element == nullptr)
        return false;
    const float offsetMinX = element->rect.offsetMin.x + deltaX;
    const float offsetMinY = element->rect.offsetMin.y + deltaY;
    const float offsetMaxX = element->rect.offsetMax.x + deltaX;
    const float offsetMaxY = element->rect.offsetMax.y + deltaY;
    if (!std::isfinite(offsetMinX) || !std::isfinite(offsetMinY) || !std::isfinite(offsetMaxX) ||
        !std::isfinite(offsetMaxY)) {
        return false;
    }
    if (!previewDrag_->changed) {
        BeginMutation();
        previewDrag_->changed = true;
    }
    previewDrag_->pointerX = pointerX;
    previewDrag_->pointerY = pointerY;
    element->rect.offsetMin = {offsetMinX, offsetMinY};
    element->rect.offsetMax = {offsetMaxX, offsetMaxY};
    return true;
}

bool UserWidgetEditorDocument::EndPreviewDrag() noexcept {
    if (!previewDrag_)
        return false;
    const bool changed = previewDrag_->changed;
    previewDrag_.reset();
    return changed;
}

bool UserWidgetEditorDocument::PreviewDragActive() const noexcept {
    return previewDrag_.has_value();
}

kb::scene::UIDocumentElement* UserWidgetEditorDocument::FindElement(kb::scene::UIElementId elementId) noexcept {
    if (!document_)
        return nullptr;
    const auto found = std::ranges::find(document_->elements, elementId, &kb::scene::UIDocumentElement::id);
    return found == document_->elements.end() ? nullptr : &*found;
}

const kb::scene::UIDocumentElement*
UserWidgetEditorDocument::FindElement(kb::scene::UIElementId elementId) const noexcept {
    if (!document_)
        return nullptr;
    const auto found = std::ranges::find(document_->elements, elementId, &kb::scene::UIDocumentElement::id);
    return found == document_->elements.end() ? nullptr : &*found;
}

void UserWidgetEditorDocument::BeginMutation() {
    if (undo_.size() == kMaxHistoryStates)
        undo_.erase(undo_.begin());
    undo_.push_back(HistoryState{
        .document = *document_,
        .selectedElementId = selectedElementId_,
        .revision = revision_,
    });
    redo_.clear();
    revision_ = nextRevision_++;
}

void UserWidgetEditorDocument::NormalizeSiblingOrder(kb::scene::UIElementId parentId) noexcept {
    std::vector<kb::scene::UIDocumentElement*> children;
    for (kb::scene::UIDocumentElement& element : document_->elements) {
        if (element.parentId == parentId)
            children.push_back(&element);
    }
    std::ranges::sort(children, {}, [](const auto* child) { return child->siblingOrder; });
    for (std::size_t index = 0U; index < children.size(); ++index) {
        children[index]->siblingOrder = static_cast<std::uint32_t>(index);
    }
}

void UserWidgetEditorDocument::NormalizeWidgetSwitcherSelection(kb::scene::UIElementId elementId) noexcept {
    kb::scene::UIDocumentElement* element = FindElement(elementId);
    if (element == nullptr || element->control.kind != kb::scene::UIControlKind::WidgetSwitcher) {
        return;
    }
    const std::size_t childCount = DirectChildCount(*document_, elementId);
    element->control.selectedIndex =
        childCount == 0U ? 0U : std::min(element->control.selectedIndex, static_cast<std::uint32_t>(childCount - 1U));
}

bool UserWidgetEditorDocument::IsDescendant(kb::scene::UIElementId candidate,
                                            kb::scene::UIElementId ancestor) const noexcept {
    const kb::scene::UIDocumentElement* current = FindElement(candidate);
    while (current != nullptr && current->parentId != 0U) {
        if (current->parentId == ancestor)
            return true;
        current = FindElement(current->parentId);
    }
    return false;
}

} // namespace kb::editor
