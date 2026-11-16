#include "rendering/script_editor/ScriptProblemService.hpp"

#include "engine/script/ScriptLanguageSubset.hpp"

#include <utility>

namespace kb::editor {

ScriptProblemService::ScriptProblemService()
    : ScriptProblemService(&ScriptProblemService::AnalyzeLuaSubset) {}

ScriptProblemService::ScriptProblemService(Analyzer analyzer)
    : analyzer_(std::move(analyzer)) {}

void ScriptProblemService::TextChanged(Clock::time_point now) {
    ++version_;
    dueAt_ = now + kDelay;
}

void ScriptProblemService::Reset(Clock::time_point now) {
    ++version_;
    problems_.clear();
    dueAt_ = now;
}

bool ScriptProblemService::Update(Clock::time_point now, const std::function<std::string()>& currentText) {
    bool changed = false;
    if (job_ && job_->result.wait_for(std::chrono::seconds{ 0 }) == std::future_status::ready) {
        std::vector<ScriptProblem> found = job_->result.get();
        if (job_->version == version_) { // an older text's result must not replace the newer text's
            changed = found != problems_;
            problems_ = std::move(found);
            analysedVersion_ = version_;
        }
        job_.reset();
    }
    if (!job_ && analysedVersion_ != version_ && now >= dueAt_) {
        job_ = Job{ version_, std::async(std::launch::async, analyzer_, currentText()) };
    }
    return changed;
}

std::vector<ScriptProblem> ScriptProblemService::AnalyzeLuaSubset(const std::string& text) {
    std::vector<ScriptProblem> problems;
    for (const kb::script::ScriptSubsetFinding& finding : kb::script::FindOutOfSubsetConstructs(text)) {
        problems.push_back(ScriptProblem{
            .line = static_cast<int>(finding.line) - 1,
            .column = static_cast<int>(finding.column) - 1,
            .length = static_cast<int>(finding.length),
            .message = finding.message,
        });
    }
    return problems;
}

} // namespace kb::editor
