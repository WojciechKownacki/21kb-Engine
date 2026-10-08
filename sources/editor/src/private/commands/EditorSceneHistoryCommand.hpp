#pragma once

#include "commands/IEditorCommand.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorSceneHistoryCommand final : public IEditorCommand {
public:
    using Mutation = std::function<bool()>;
    // A file the mutation rewrites outside the scene (a prefab Apply writes its asset). Undo and redo
    // put its bytes back as they were before and after, then call reload.
    struct AssetFile {
        std::filesystem::path path;
        std::function<void()> reload;
    };

    [[nodiscard]] static std::unique_ptr<EditorSceneHistoryCommand> Create(kb::scene::Scene& scene, std::string label, Mutation mutation, AssetFile assetFile = {});
    [[nodiscard]] static std::unique_ptr<EditorSceneHistoryCommand> CreateRecorded(kb::scene::Scene& scene, std::string label);

    [[nodiscard]] std::string_view Label() const noexcept override;
    [[nodiscard]] bool Execute() override;
    [[nodiscard]] bool Undo() override;
    [[nodiscard]] bool Redo() override;

private:
    EditorSceneHistoryCommand(kb::scene::Scene& scene, std::string label, Mutation mutation, bool alreadyRecorded);
    void RestoreAssetFile(const std::optional<std::string>& bytes);

    kb::scene::Scene& scene_;
    std::string label_;
    Mutation mutation_;
    bool alreadyRecorded_ = false;
    AssetFile assetFile_;
    std::optional<std::string> assetBefore_;
    std::optional<std::string> assetAfter_;
};

} // namespace kb::editor
