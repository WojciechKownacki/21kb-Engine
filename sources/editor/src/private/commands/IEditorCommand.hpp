#pragma once

#include <string_view>

namespace kb::editor {

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
    [[nodiscard]] virtual bool Execute() = 0;
    [[nodiscard]] virtual bool Undo() = 0;
    [[nodiscard]] virtual bool Redo() = 0;
};

} // namespace kb::editor
