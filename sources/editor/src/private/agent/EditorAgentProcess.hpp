#pragma once

#include <atomic>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace kb::editor {

// One command-line agent (or its status command) running in the background: its standard input
// gets the request text and is closed - or, for an agent that takes one request after another,
// stays open for Write - and its output (standard output and error together) is read on a worker
// thread and handed over line by line. Stopping ends the whole process tree.
class EditorAgentProcess final {
public:
    EditorAgentProcess() = default;
    ~EditorAgentProcess();
    EditorAgentProcess(const EditorAgentProcess&) = delete;
    EditorAgentProcess& operator=(const EditorAgentProcess&) = delete;

    // Starts `executable` with `arguments` in `workingDirectory`, the current environment plus
    // `environment`. A .cmd or .bat file runs through the command processor. Fails, with a
    // reason, when a run is still going or the process cannot start.
    [[nodiscard]] bool Start(
        const std::filesystem::path& executable,
        const std::vector<std::wstring>& arguments,
        const std::filesystem::path& workingDirectory,
        const std::vector<std::pair<std::wstring, std::wstring>>& environment,
        std::string input,
        std::string& error,
        bool keepInputOpen = false);
    // Writes more input to a process started with keepInputOpen; false once it has ended.
    [[nodiscard]] bool Write(std::string_view text);
    // Whether the process is still running (it may be idle, waiting for input).
    [[nodiscard]] bool Alive() const noexcept { return process_ != nullptr && !finished_; }

    // Complete output lines that arrived since the last call, as UTF-8.
    [[nodiscard]] std::vector<std::string> TakeLines();
    // Whether the process is still running or has output left to take.
    [[nodiscard]] bool Busy() const noexcept;
    // The exit code once the process ended and every line was taken.
    [[nodiscard]] std::optional<int> ExitCode() const noexcept;
    void Stop() noexcept;

private:
    void Join() noexcept;

    void* process_ = nullptr;
    void* job_ = nullptr;
    void* input_ = nullptr;
    std::mutex inputMutex_;
    std::thread reader_;
    mutable std::mutex mutex_;
    std::deque<std::string> lines_;
    std::atomic<bool> finished_{ false };
    std::atomic<int> exitCode_{ 0 };
};

} // namespace kb::editor
