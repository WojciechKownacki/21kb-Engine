#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::script {

struct NativeScriptBuildDesc {
    bool enabled = false;
    std::string command;
    std::filesystem::path workingDirectory;
};

struct NativeScriptBuildResult {
    int exitCode = 0;
    // The build was stopped by Cancel(); it is not a compiler error and nothing is reported as one.
    bool cancelled = false;
    std::vector<std::string> errors;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && exitCode == 0 && !cancelled;
    }
};

// Where a build message says its problem is: `path(12): error ...`, `path(12,5): ...` or `path:12:5: ...`.
struct NativeScriptSourceLocation {
    std::string path;
    int line = 0;
    int column = 0;
};

// The location a compiler message starts with, if it has one.
[[nodiscard]] std::optional<NativeScriptSourceLocation> ParseBuildMessageLocation(std::string_view message);

class NativeScriptBuildPipeline final {
public:
    NativeScriptBuildPipeline() = delete;

    // Runs the build on the calling thread and waits for it.
    [[nodiscard]] static NativeScriptBuildResult Build(const NativeScriptBuildDesc& desc);
};

struct NativeScriptBuildProgress {
    bool finished = false;
    bool cancelRequested = false;
    double elapsedSeconds = 0.0;
    // Lines the build printed so far and the last of them: enough to show that it moves and where it is.
    std::size_t outputLines = 0;
    std::string lastLine;
};

// A build running on its own thread and in its own process (and process tree), so the caller keeps going:
// look at Progress() every frame, Cancel() to stop it, and take Result() once Finished().
class NativeScriptBuildJob final {
public:
    explicit NativeScriptBuildJob(NativeScriptBuildDesc desc);
    // Stops a build still running and waits for its end.
    ~NativeScriptBuildJob();

    NativeScriptBuildJob(const NativeScriptBuildJob&) = delete;
    NativeScriptBuildJob& operator=(const NativeScriptBuildJob&) = delete;

    [[nodiscard]] bool Finished() const noexcept;
    [[nodiscard]] NativeScriptBuildProgress Progress() const;
    void Cancel() noexcept;
    // Waits for the end of the build and returns what it reported.
    [[nodiscard]] NativeScriptBuildResult Wait();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kb::script
