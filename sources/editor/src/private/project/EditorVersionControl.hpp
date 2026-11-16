#pragma once

#include "project/EditorProjectRepository.hpp"
#include "scene/EditorSceneComparison.hpp"

#if defined(_WIN32)

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace kb::editor {

// What the Version Control panel and the asset markers show: the files of the project that differ from the last
// saved version, kept current by reading git in the background, and the comparison of the open scene with its last
// saved version. One for the editor, since there is one open project.
class EditorVersionControl final {
public:
    [[nodiscard]] static EditorVersionControl& Instance();

    // Asks for a fresh reading as soon as none is running.
    void RequestRefresh() noexcept { refreshRequested_ = true; }
    // Takes a finished reading, and starts the next one when asked for or when it has been a while. True when what
    // the panel shows changed.
    [[nodiscard]] bool Pump(const std::filesystem::path& projectRoot, std::chrono::steady_clock::time_point now);

    // A reading has arrived for the project.
    [[nodiscard]] bool Known() const noexcept { return known_; }
    [[nodiscard]] const EditorChangeList& Changes() const noexcept { return changes_; }
    // How a file or folder under the project differs, found by its full path; null when it does not.
    [[nodiscard]] const EditorChangeState* StateOf(const std::filesystem::path& path) const;
    [[nodiscard]] bool FolderHasChanges(const std::filesystem::path& path) const;

    // The change the user chose in the list, by its place there; kNone for none.
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    [[nodiscard]] std::size_t Selected() const noexcept { return selected_; }
    void Select(std::size_t index) noexcept { selected_ = index; }

    int changeScroll = 0;
    int sceneScroll = 0;

    // The open scene, as it is now, against its last saved version; runs in the background.
    void CompareScene(const std::filesystem::path& projectRoot, std::string repositoryPath, kb::scene::SceneDocument current);
    [[nodiscard]] bool Comparing() const noexcept { return comparison_.valid(); }
    [[nodiscard]] bool HasComparison() const noexcept { return hasComparison_; }
    [[nodiscard]] const EditorSceneComparison& Comparison() const noexcept { return result_; }
    // The changed object the user chose in the comparison.
    [[nodiscard]] std::size_t SelectedObject() const noexcept { return selectedObject_; }
    void SelectObject(std::size_t index) noexcept { selectedObject_ = index; }
    void ClearComparison() noexcept;

private:
    void Adopt(EditorChangeList list);

    EditorChangeList changes_;
    bool known_ = false;
    bool refreshRequested_ = true;
    std::filesystem::path root_;
    std::chrono::steady_clock::time_point lastRefresh_{};
    std::future<EditorChangeList> reading_;
    std::size_t selected_ = kNone;
    // Paths of the changed files and of the folders above them, lower-cased: Windows names ignore case.
    std::unordered_map<std::string, EditorChangeState> byPath_;
    std::unordered_set<std::string> folders_;

    // The saved version of the scene, read in the background, with the scene as it was when the user asked.
    struct SavedScene {
        kb::scene::SceneDocumentLoadResult saved;
        kb::scene::SceneDocument current;
    };
    std::future<SavedScene> comparison_;
    bool hasComparison_ = false;
    EditorSceneComparison result_;
    std::size_t selectedObject_ = kNone;
};

} // namespace kb::editor

#endif
