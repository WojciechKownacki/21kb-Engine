#pragma once

#include "docking/DockPanelCollection.hpp"
#include "docking/DockNode.hpp"
#include "docking/EditorDockModelCommands.hpp"
#include "docking/EditorDockModelQueries.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>

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
    DockPanelCollection panels_;
    std::unique_ptr<DockNode> root_;
    std::uint32_t nextNodeId_ = 1;
    std::uint32_t maximizedLeafId_ = 0;
};

} // namespace kb::editor
