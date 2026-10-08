#include "commands/EditorSceneHistoryCommand.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneHistory.hpp"

#include <fstream>
#include <iterator>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] std::optional<std::string> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        return std::nullopt;
    }
    return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

} // namespace

std::unique_ptr<EditorSceneHistoryCommand> EditorSceneHistoryCommand::Create(kb::scene::Scene& scene, std::string label, Mutation mutation, AssetFile assetFile) {
    std::unique_ptr<EditorSceneHistoryCommand> command{ new EditorSceneHistoryCommand(scene, std::move(label), std::move(mutation), false) };
    command->assetFile_ = std::move(assetFile);
    return command;
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
    if (!assetFile_.path.empty()) {
        assetBefore_ = ReadFileBytes(assetFile_.path);
    }
    if (mutation_()) {
        if (!assetFile_.path.empty()) {
            assetAfter_ = ReadFileBytes(assetFile_.path);
        }
        return true;
    }

    static_cast<void>(scene_.History().Undo());
    return false;
}

bool EditorSceneHistoryCommand::Undo() {
    if (!scene_.History().Undo()) {
        return false;
    }
    RestoreAssetFile(assetBefore_);
    return true;
}

bool EditorSceneHistoryCommand::Redo() {
    if (!scene_.History().Redo()) {
        return false;
    }
    RestoreAssetFile(assetAfter_);
    return true;
}

void EditorSceneHistoryCommand::RestoreAssetFile(const std::optional<std::string>& bytes) {
    if (assetFile_.path.empty() || !bytes.has_value()) {
        return;
    }
    {
        std::ofstream output{ assetFile_.path, std::ios::binary | std::ios::trunc };
        output.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
    }
    if (assetFile_.reload) {
        assetFile_.reload();
    }
}

} // namespace kb::editor
