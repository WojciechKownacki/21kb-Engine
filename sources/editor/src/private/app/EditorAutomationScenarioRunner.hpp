#pragma once

#include <filesystem>

namespace kb::editor {

// Executes a versioned, external editor-automation scenario against an
// isolated project. Every operation is routed through production editor or
// engine APIs; the runner only resolves aliases, validates JSON, records
// evidence, and owns the isolated project lifetime.
class EditorAutomationScenarioRunner final {
public:
    EditorAutomationScenarioRunner() = delete;

    [[nodiscard]] static int Run(
        const std::filesystem::path& scenarioPath,
        const std::filesystem::path& artifactRoot);
};

} // namespace kb::editor
