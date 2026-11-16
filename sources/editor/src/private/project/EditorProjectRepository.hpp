#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kb::editor {

#if defined(_WIN32)

struct EditorRepositoryState {
    // The project folder has its own .git.
    bool hasRepository = false;
    std::string branch;
    // Where origin points; empty when nothing is connected.
    std::string remoteUrl;
    // A repository above the project, when the project has none of its own.
    std::filesystem::path enclosingRoot;
};

struct EditorRepositoryResult {
    bool succeeded = false;
    std::string message;
};

// How a file differs from the last saved version (the last commit).
enum class EditorChangeState : std::uint8_t {
    Modified,
    // Added to the repository's index but not saved yet.
    Added,
    Deleted,
    // Not known to the repository at all.
    Untracked,
};

struct EditorFileChange {
    // Relative to the repository folder, with '/' separators.
    std::string path;
    EditorChangeState state = EditorChangeState::Modified;
};

struct EditorChangeList {
    // False when git is not installed or the project is not inside a repository.
    bool available = false;
    // The repository folder; the paths of `changes` are relative to it.
    std::filesystem::path root;
    std::string branch;
    // Sorted by path.
    std::vector<EditorFileChange> changes;
};

// The project's version control, through the git and gh programs on this computer. The
// commands that change something take as long as the network does, so they run off the UI
// thread; none of them touches editor state.
class EditorProjectRepository final {
public:
    EditorProjectRepository() = delete;

    [[nodiscard]] static EditorRepositoryState Query(const std::filesystem::path& projectRoot);

    // The files under the project that differ from the last saved version. Only reads.
    [[nodiscard]] static EditorChangeList Changes(const std::filesystem::path& projectRoot);
    // A file as the last saved version holds it; empty when that version does not have it.
    [[nodiscard]] static std::optional<std::string> SavedContent(const std::filesystem::path& projectRoot, const std::string& repositoryPath);
    // Whether a file in this state exists in the last saved version, so that it can be brought back.
    [[nodiscard]] static bool CanRestore(EditorChangeState state) noexcept;
    // Puts one file back as the last saved version has it.
    [[nodiscard]] static EditorRepositoryResult Restore(const std::filesystem::path& projectRoot, const std::string& repositoryPath);
    [[nodiscard]] static bool GitHubCliAvailable();
    // Letters, digits, '.', '-' and '_': what GitHub accepts as a repository name.
    [[nodiscard]] static bool IsValidRepositoryName(const std::string& name) noexcept;
    [[nodiscard]] static std::string SuggestedRepositoryName(const std::filesystem::path& projectRoot);
    // The remote's page in a browser; empty for a remote that has none we can name.
    [[nodiscard]] static std::string BrowserUrl(const std::string& remoteUrl);

    // A repository with the project's first commit in it.
    [[nodiscard]] static EditorRepositoryResult CreateLocal(const std::filesystem::path& projectRoot);
    // Points origin at `url` once it answers, and sends the project there when it is empty.
    [[nodiscard]] static EditorRepositoryResult ConnectUrl(const std::filesystem::path& projectRoot, const std::string& url);
    // A new private GitHub repository named `name`, holding the project. Asks gh to sign in first
    // when it is not; the sign-in happens in gh's own window and browser.
    [[nodiscard]] static EditorRepositoryResult ConnectGitHub(const std::filesystem::path& projectRoot, const std::string& name);
};

#endif

} // namespace kb::editor
