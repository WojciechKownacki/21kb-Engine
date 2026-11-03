#pragma once

#include "engine/core/ProfilerDiagnosis.hpp"
#include "engine/core/ProfilerRegistry.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

// The profiler's names in the editor's language. A counter that is not built in (a system of the
// scene) has no translation and shows its own name.
struct ProfilerText final {
    ProfilerText() = delete;

    [[nodiscard]] static std::string_view Counter(const kb::core::ProfilerCounterInfo& info);
    [[nodiscard]] static std::string_view Group(std::string_view englishGroup);
    [[nodiscard]] static std::string_view Area(kb::core::ProfilerArea area);

    // A region of the frame; the fixed ones of the editor are translated, a system keeps its name.
    [[nodiscard]] static std::string_view Region(std::string_view name);
    [[nodiscard]] static std::string_view Severity(kb::core::DiagnosisSeverity severity);
    [[nodiscard]] static std::string_view Category(kb::core::DiagnosisCategory category);
    // What a finding says: its headline, why it hurts, the steps to take, and where it is.
    [[nodiscard]] static std::string FindingTitle(const kb::core::DiagnosisFinding& finding, const kb::core::ProfilerRegistry& registry);
    [[nodiscard]] static std::string FindingDetail(const kb::core::DiagnosisFinding& finding);
    [[nodiscard]] static std::vector<std::string> FindingSteps(const kb::core::DiagnosisFinding& finding);
    [[nodiscard]] static std::string FindingWhere(const kb::core::DiagnosisFinding& finding, const kb::core::ProfilerRegistry& registry);
};

} // namespace kb::editor
