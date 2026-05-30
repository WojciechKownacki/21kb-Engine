#include "docking/EditorDockModel.hpp"

#include "docking/DefaultDockWorkspace.hpp"

namespace kb::editor {

EditorDockModel::EditorDockModel()
    : panels_(DefaultDockWorkspace{}.CreatePanels()) {
    root_ = DefaultDockWorkspace{}.CreateRoot(nextNodeId_);
}

EditorDockModel::~EditorDockModel() = default;

EditorDockModelQueries EditorDockModel::Queries() const noexcept {
    return EditorDockModelQueries{ panels_, root_.get() };
}

EditorDockModelCommands EditorDockModel::Commands() noexcept {
    return EditorDockModelCommands{ panels_, root_, nextNodeId_ };
}

} // namespace kb::editor
