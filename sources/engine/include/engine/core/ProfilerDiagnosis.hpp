#pragma once

#include "engine/core/ProfilerFrameRegions.hpp"
#include "engine/core/ProfilerRegistry.hpp"

#include <memory>
#include <string>
#include <vector>

namespace kb::core {

enum class DiagnosisSeverity : std::uint8_t { Problem, Warning, Tip };
enum class DiagnosisCategory : std::uint8_t { Cpu, Gpu, Memory, Frame };

// What a rule found, as numbers and names; the words are the tool's, in its own language.
// `value` is what was measured, `limit` what it is held against, `subject` the region or counter
// concerned, `gainMilliseconds` an upper estimate of the frame time that fixing it gives back.
struct DiagnosisFinding {
    std::string rule;
    DiagnosisSeverity severity = DiagnosisSeverity::Warning;
    DiagnosisCategory category = DiagnosisCategory::Frame;
    std::string subject;
    double value = 0.0;
    double limit = 0.0;
    double gainMilliseconds = 0.0;

    // What names the finding from one run of the diagnosis to the next, while its position among
    // the findings changes: the rule and where it found the problem.
    [[nodiscard]] std::string Key() const { return rule + "/" + subject; }
};

// What a rule looks at: the counters with their history and thresholds, and the regions of the
// latest captured frame.
struct DiagnosisInput {
    const ProfilerRegistry& registry;
    const ProfilerFrameRegions& frames;
};

// One check on measured data. Rules are independent objects so a tool can add or drop one.
class DiagnosisRule {
public:
    virtual ~DiagnosisRule() = default;
    [[nodiscard]] virtual std::string_view Id() const noexcept = 0;
    virtual void Evaluate(const DiagnosisInput& input, std::vector<DiagnosisFinding>& findings) const = 0;
};

// The rules of the engine: the frame over its budget (and whether the CPU or the GPU holds it up),
// draw calls and triangles over their thresholds, memory that keeps growing, a region that eats a
// large part of the frame, and frames that spike. Findings come out most severe first, then by gain.
class ProfilerDiagnosis final {
public:
    ProfilerDiagnosis();
    ~ProfilerDiagnosis();

    [[nodiscard]] std::vector<DiagnosisFinding> Run(const DiagnosisInput& input) const;

private:
    std::vector<std::unique_ptr<DiagnosisRule>> rules_;
};

} // namespace kb::core
