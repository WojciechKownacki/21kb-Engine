#pragma once

#include <cstdint>
#include <string_view>

namespace kb::editor {

enum class EditorCommandHistoryKind : std::uint8_t {
    Scene,
    MaterialAsset,
};

struct EditorCommandHistoryKey {
    EditorCommandHistoryKind kind = EditorCommandHistoryKind::Scene;
    std::uint64_t documentId = 0U;

    [[nodiscard]] static constexpr EditorCommandHistoryKey Scene() noexcept {
        return {};
    }

    [[nodiscard]] static constexpr EditorCommandHistoryKey MaterialAsset(std::uint64_t assetId) noexcept {
        return EditorCommandHistoryKey{
            .kind = EditorCommandHistoryKind::MaterialAsset,
            .documentId = assetId,
        };
    }

    [[nodiscard]] constexpr bool IsMaterialAsset() const noexcept {
        return kind == EditorCommandHistoryKind::MaterialAsset && documentId != 0U;
    }

    [[nodiscard]] constexpr bool operator==(const EditorCommandHistoryKey&) const noexcept = default;
};

class IEditorCommand {
public:
    virtual ~IEditorCommand() = default;

    [[nodiscard]] virtual std::string_view Label() const noexcept = 0;
    [[nodiscard]] virtual bool AffectsSceneDocument() const noexcept {
        return true;
    }
    [[nodiscard]] virtual bool AffectsHierarchySelection() const noexcept {
        return true;
    }
    [[nodiscard]] virtual bool AffectsOpenMaterialSource() const noexcept {
        return false;
    }
    [[nodiscard]] virtual EditorCommandHistoryKey HistoryKey() const noexcept {
        return EditorCommandHistoryKey::Scene();
    }
    [[nodiscard]] virtual bool Execute() = 0;
    [[nodiscard]] virtual bool Undo() = 0;
    [[nodiscard]] virtual bool Redo() = 0;
};

} // namespace kb::editor
