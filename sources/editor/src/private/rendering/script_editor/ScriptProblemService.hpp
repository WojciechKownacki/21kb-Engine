#pragma once

#include "rendering/script_editor/ScriptProblem.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace kb::editor {

// Keeps the warnings of one script text up to date while it is typed. An edit only
// starts a timer; once the text has been still for kDelay the text is analysed once
// on a worker thread, and a result for a text that was edited since is dropped. Pure
// C++ (no Win32): the caller passes the clock, so it is unit-tested without waiting.
class ScriptProblemService {
public:
    using Clock = std::chrono::steady_clock;
    using Analyzer = std::function<std::vector<ScriptProblem>(const std::string&)>;

    static constexpr std::chrono::milliseconds kDelay{ 300 };

    // The default analyzer is the validator that runs at Play and in project validation.
    ScriptProblemService();
    explicit ScriptProblemService(Analyzer analyzer);

    // The text was edited: analysis is due once it stays unchanged for kDelay.
    void TextChanged(Clock::time_point now);
    // A different text was loaded: drops the warnings and analyses it at the next Update.
    void Reset(Clock::time_point now);

    // Collects a finished analysis and starts one when it is due; `currentText` is read only then.
    // Returns true when Problems() changed.
    bool Update(Clock::time_point now, const std::function<std::string()>& currentText);

    [[nodiscard]] const std::vector<ScriptProblem>& Problems() const noexcept { return problems_; }
    // Nothing is waiting or running, so Update has nothing to do until the next edit.
    [[nodiscard]] bool Idle() const noexcept { return !job_.has_value() && analysedVersion_ == version_; }

    // Every construct outside the language both script VMs share, as ScriptProblems.
    [[nodiscard]] static std::vector<ScriptProblem> AnalyzeLuaSubset(const std::string& text);

private:
    struct Job {
        std::uint64_t version = 0;
        std::future<std::vector<ScriptProblem>> result;
    };

    Analyzer analyzer_;
    std::vector<ScriptProblem> problems_;
    std::optional<Job> job_;
    std::uint64_t version_ = 0;         // bumped by every edit
    std::uint64_t analysedVersion_ = 0; // the version problems_ belongs to
    Clock::time_point dueAt_{};
};

} // namespace kb::editor
