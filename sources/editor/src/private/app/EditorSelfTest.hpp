#pragma once

#include <filesystem>

namespace kb::editor {

// Headless self-tests that drive the real editor objects (EditorSceneContext,
// panel renderers/controllers, on-disk persistence) end-to-end without creating
// a window or a graphics device. Invoked from main via the --selftest flag so
// the editor can be verified in CI / by an agent without a GUI session.
class EditorSelfTest {
public:
    EditorSelfTest() = delete;

    // Runs every self-test suite, writes a human-readable report to reportPath,
    // and returns 0 when all checks pass, 1 otherwise.
    [[nodiscard]] static int Run(const std::filesystem::path& reportPath);
};

} // namespace kb::editor
