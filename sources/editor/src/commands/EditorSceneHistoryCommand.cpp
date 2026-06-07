#include "commands/EditorSceneHistoryCommand.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneHistory.hpp"

#include <utility>

namespace kb::editor {

std::unique_ptr<EditorSceneHistoryCommand> EditorSceneHistoryCommand::Create(kb::scene::Scene& scene, std::string label, Mutation mutation) {
    return std::unique_ptr<EditorSceneHistoryCommand>{ new EditorSceneHistoryCommand(scene, std::move(label), std::move(mutation), false) };
}

std::unique_ptr<EditorSceneHistoryCommand> EditorSceneHistoryCommand::CreateRecorded(kb::scene::Scene& scene, std::string label) {
    return std::unique_ptr<EditorSceneHistoryCommand>{ new EditorSceneHistoryCommand(scene, std::move(label), {}, true) };
}

EditorSceneHistoryCommand::EditorSceneHistoryCommand(kb::scene::Scene& scene, std::string label, Mutation mutation, bool alreadyRecorded)
    : scene_(scene)
    , label_(std::move(label))
    , mutation_(std::move(mutation))
    , alreadyRecorded_(alreadyRecorded) {}

std::string_view EditorSceneHistoryCommand::Label() const noexcept {
    return label_;
}

bool EditorSceneHistoryCommand::Execute() {
    if (alreadyRecorded_) {
        return true;
    }
    if (!mutation_) {
        return false;
    }
    if (!scene_.History().Record(label_)) {
        return false;
    }
    if (mutation_()) {
        return true;
    }

    static_cast<void>(scene_.History().Undo());
    return false;
}

bool EditorSceneHistoryCommand::Undo() {
    return scene_.History().Undo();
}

bool EditorSceneHistoryCommand::Redo() {
    return scene_.History().Redo();
}

} // namespace kb::editor
