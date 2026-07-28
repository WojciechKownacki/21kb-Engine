#pragma once

#include "engine/input/InputKey.hpp"
#include "inspection/InspectorPhysicsModel.hpp"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace kb::editor {

class EditorSceneContext;

// Deterministic, windowless driver for the production editor interaction
// paths. It resolves controls from the live Inspector layout and sends the
// same pointer/text handlers used by the Win32 message routers. Gameplay input
// enters at InputDeviceState, immediately before the normal InputSubsystem.
class EditorHeadlessAutomation final {
public:
    EditorHeadlessAutomation(
        EditorSceneContext& context,
        std::filesystem::path artifactRoot);

    [[nodiscard]] bool AddComponent(std::string_view componentId);
    [[nodiscard]] bool SetPhysicsFloat(
        PhysicsComponentKind component, int fieldIndex, float value);
    [[nodiscard]] bool SetGameplayKey(
        kb::input::InputKey key, bool down);
    [[nodiscard]] bool StepRuntime(
        std::size_t frames, float deltaSeconds);

    [[nodiscard]] bool CaptureInspector(
        std::string_view checkpoint);
    [[nodiscard]] bool SnapshotInspectorTree(
        std::string_view checkpoint);
    void SnapshotConsole(std::string_view checkpoint);
    void Trace(
        std::string_view operation, bool succeeded,
        std::string_view detail = {});

    [[nodiscard]] const std::filesystem::path& ArtifactRoot()
        const noexcept;

private:
    EditorSceneContext& context_;
    std::filesystem::path artifactRoot_;
    std::filesystem::path tracePath_;
};

} // namespace kb::editor
