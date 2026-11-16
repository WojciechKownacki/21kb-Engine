#pragma once

#include "docking/DockPanelCollection.hpp"
#include "docking/DockNode.hpp"
#include "docking/EditorDockModelCommands.hpp"
#include "docking/EditorDockModelQueries.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace kb::editor {

class EditorDockModel {
public:
    EditorDockModel();
    ~EditorDockModel();

    EditorDockModel(const EditorDockModel&) = delete;
    EditorDockModel& operator=(const EditorDockModel&) = delete;
    EditorDockModel(EditorDockModel&&) = delete;
    EditorDockModel& operator=(EditorDockModel&&) = delete;

    [[nodiscard]] EditorDockModelQueries Queries() const noexcept;
    [[nodiscard]] EditorDockModelCommands Commands() noexcept;

private:
    friend class EditorWorkspaceArrangement;
    DockPanelCollection panels_;
    std::unique_ptr<DockNode> root_;
    // The floating windows: each holds a tab group of its own, apart from the docked tree.
    std::vector<DockNode> floating_;
    std::uint32_t nextNodeId_ = 1;
    std::uint32_t maximizedLeafId_ = 0;
    // The bottom drawer (the leaf holding Project Files) is shown as its tab strip only.
    bool drawerCollapsed_ = false;
};

} // namespace kb::editor
