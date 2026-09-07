#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/UIAssets.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace kb::editor {

// Transient editor session over the canonical UIDocument asset. The working
// copy, undo states and saved file all contain that exact engine type; there is
// no parallel hierarchy or layout representation.
class UserWidgetEditorDocument final {
  public:
    [[nodiscard]] static kb::scene::UIDocument CreateCanvasDocument();

    [[nodiscard]] bool Open(kb::assets::AssetId assetId, const std::filesystem::path& path);
    [[nodiscard]] bool Save();
    void Close() noexcept;

    [[nodiscard]] bool HasOpenDocument() const noexcept;
    [[nodiscard]] bool IsDirty() const noexcept;
    [[nodiscard]] kb::assets::AssetId AssetId() const noexcept;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;
    [[nodiscard]] const kb::scene::UIDocument* Document() const noexcept;
    [[nodiscard]] kb::scene::UIElementId SelectedElementId() const noexcept;
    [[nodiscard]] const kb::scene::UIDocumentElement* SelectedElement() const noexcept;
    [[nodiscard]] bool SelectElement(kb::scene::UIElementId elementId) noexcept;

    [[nodiscard]] std::optional<kb::scene::UIElementId> AddElement(kb::scene::UIControlKind kind,
                                                                   kb::scene::UIElementId parentId = 0U);
    [[nodiscard]] bool RemoveElement(kb::scene::UIElementId elementId);
    [[nodiscard]] bool ReparentElement(kb::scene::UIElementId elementId, kb::scene::UIElementId parentId);
    // Replaces every authored property/component of an element while keeping
    // its identity and hierarchy placement owned by the document.
    [[nodiscard]] bool EditElement(kb::scene::UIElementId elementId, const kb::scene::UIDocumentElement& edited);

    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] bool Undo();
    [[nodiscard]] bool Redo();

    [[nodiscard]] bool BeginPreviewDrag(kb::scene::UIElementId elementId, float pointerX, float pointerY,
                                        float canvasScale) noexcept;
    [[nodiscard]] bool UpdatePreviewDrag(float pointerX, float pointerY);
    [[nodiscard]] bool EndPreviewDrag() noexcept;
    [[nodiscard]] bool PreviewDragActive() const noexcept;

  private:
    struct HistoryState {
        kb::scene::UIDocument document;
        kb::scene::UIElementId selectedElementId = 0U;
        std::uint64_t revision = 0U;
    };

    struct PreviewDragState {
        kb::scene::UIElementId elementId = 0U;
        float pointerX = 0.0F;
        float pointerY = 0.0F;
        float canvasScale = 1.0F;
        bool changed = false;
    };

    [[nodiscard]] kb::scene::UIDocumentElement* FindElement(kb::scene::UIElementId elementId) noexcept;
    [[nodiscard]] const kb::scene::UIDocumentElement* FindElement(kb::scene::UIElementId elementId) const noexcept;
    void BeginMutation();
    void NormalizeSiblingOrder(kb::scene::UIElementId parentId) noexcept;
    void NormalizeWidgetSwitcherSelection(kb::scene::UIElementId elementId) noexcept;
    [[nodiscard]] bool IsDescendant(kb::scene::UIElementId candidate, kb::scene::UIElementId ancestor) const noexcept;

    static constexpr std::size_t kMaxHistoryStates = 128U;
    kb::assets::AssetId assetId_{};
    std::filesystem::path path_;
    std::optional<kb::scene::UIDocument> document_;
    kb::scene::UIElementId selectedElementId_ = 0U;
    std::uint64_t revision_ = 0U;
    std::uint64_t savedRevision_ = 0U;
    std::uint64_t nextRevision_ = 1U;
    std::vector<HistoryState> undo_;
    std::vector<HistoryState> redo_;
    std::optional<PreviewDragState> previewDrag_;
};

} // namespace kb::editor
