#pragma once

#include "commands/IEditorCommand.hpp"

#include <functional>
#include <memory>
#include <string>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorSceneHistoryCommand final : public IEditorCommand {
public:
    using Mutation = std::function<bool()>;

    [[nodiscard]] static std::unique_ptr<EditorSceneHistoryCommand> Create(kb::scene::Scene& scene, std::string label, Mutation mutation);
    [[nodiscard]] static std::unique_ptr<EditorSceneHistoryCommand> CreateRecorded(kb::scene::Scene& scene, std::string label);

    [[nodiscard]] std::string_view Label() const noexcept override;
    [[nodiscard]] bool Execute() override;
    [[nodiscard]] bool Undo() override;
    [[nodiscard]] bool Redo() override;

private:
    EditorSceneHistoryCommand(kb::scene::Scene& scene, std::string label, Mutation mutation, bool alreadyRecorded);

    kb::scene::Scene& scene_;
    std::string label_;
    Mutation mutation_;
    bool alreadyRecorded_ = false;
};

} // namespace kb::editor
