#pragma once

#include <filesystem>
#include <string>

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

// The project's version control, through the git and gh programs on this computer. The
// commands that change something take as long as the network does, so they run off the UI
// thread; none of them touches editor state.
class EditorProjectRepository final {
public:
    EditorProjectRepository() = delete;

    [[nodiscard]] static EditorRepositoryState Query(const std::filesystem::path& projectRoot);
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
