#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputTouchPoint.hpp"
#include "inspection/InspectorPhysicsModel.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace kb::editor {

class EditorSceneContext;

// Deterministic, windowless driver for the production editor interaction
// paths. It resolves controls from the live Inspector layout and sends the
// same pointer/text handlers used by the Win32 message routers. Gameplay input
// enters at InputDeviceState, immediately before the normal InputSubsystem.
class EditorHeadlessAutomation final {
public:
    struct ParticleThumbnailVerification {
        bool succeeded = false;
        std::size_t ticks = 0U;
        bool animated = false;
    };

    // What one step of an Inspector drag actually costs, split by stage, so the
    // stall a person feels can be attributed instead of guessed at.
    struct InspectorDragProfile {
        bool succeeded = false;
        std::size_t steps = 0U;
        double applyMs = 0.0;
        double inspectorPaintMs = 0.0;
        double inspectorHeightMs = 0.0;
        double inspectorRowPaintMs = 0.0;
        double inspectorHitTestMs = 0.0;
        double scenePresentMs = 0.0;
    };

    // What one idle scene-view frame actually costs, split by stage. "Idle" means
    // the editor decided to present but nothing about the scene changed: no camera
    // move, no selection, no dirty entity. Every number is one frame's worth,
    // averaged over the sampled frames.
    struct IdleFrameProfile {
        bool succeeded = false;
        std::size_t steps = 0U;
        // One dock layout build - the tree walk that produces every panel rect.
        double dockLayoutMs = 0.0;
        // Resolving the child-surface rects for the main window (builds its own layout).
        double hostSurfaceResolveMs = 0.0;
        // One EditorPanelContentResolver::Resolve for a docked panel (builds its own layout).
        double panelResolveMs = 0.0;
        // Deciding whether the Particle Editor panel is on screen (builds its own layout).
        double particleVisibleMs = 0.0;
        // The material-preview probe: two asset registry lookups that answer "nothing to draw".
        double materialPreviewProbeMs = 0.0;
        // The whole non-render half of an idle frame, as the message loop performs it.
        double geometryTotalMs = 0.0;
        // The same half through the shared single-build path.
        double sharedGeometryMs = 0.0;
        // BeginPaintLayout + Present + EndPaintLayout at idle revisions: the GPU submit.
        double sceneSubmitMs = 0.0;
        // geometryTotalMs + sceneSubmitMs.
        double idleFrameMs = 0.0;
        // How many dock layout builds the naive sequence performs per frame.
        std::size_t dockLayoutBuildsPerFrame = 0U;
    };

    struct FloatingWindowFrame {
        bool succeeded = false;
        // Pixels of window height Windows keeps for itself, with the editor's frame
        // handling and without it.
        int reservedWithHandler = -1;
        int reservedWithoutHandler = -1;
    };

    struct SavedLayoutRoundTrip {
        bool succeeded = false;
        bool listed = false;
        bool applied = false;
        bool named = false;
        bool deleted = false;
        std::string layout;
    };

    struct WorkspaceLayoutPersistence {
        bool succeeded = false;
        bool storedOnDisk = false;
        std::string savedLayout;
        std::string restoredLayout;
    };

    struct ParticleDependencyNavigation {
        bool succeeded = false;
        std::size_t dependencyCount = 0U;
        kb::assets::AssetId expectedAsset{};
        kb::assets::AssetId selectedAsset{};
    };

    EditorHeadlessAutomation(
        EditorSceneContext& context,
        std::filesystem::path artifactRoot);
    ~EditorHeadlessAutomation();

    [[nodiscard]] bool AddComponent(std::string_view componentId);
    [[nodiscard]] bool SetPhysicsFloat(
        PhysicsComponentKind component, int fieldIndex, float value);
    [[nodiscard]] bool SetGameplayKey(
        kb::input::InputKey key, bool down,
        std::uint8_t gamepadIndex = 0U);
    [[nodiscard]] bool SetGameplayAnalog(
        kb::input::InputKey key, float value,
        std::uint8_t gamepadIndex = 0U);
    [[nodiscard]] bool SetGameplayPointer(float x, float y);
    [[nodiscard]] bool SetGameplayTouches(
        std::span<const kb::input::InputTouchPoint> points);
    [[nodiscard]] bool SetGameplayFocus(bool focused);
    [[nodiscard]] bool SetGamepadConnected(
        std::uint8_t gamepadIndex, bool connected);
    [[nodiscard]] bool StepRuntime(
        std::size_t frames, float deltaSeconds);
    [[nodiscard]] bool StepEditorParticles(
        std::size_t frames, float deltaSeconds);
    [[nodiscard]] bool VerifyParticlePickerInteraction();
    [[nodiscard]] ParticleThumbnailVerification VerifyParticleThumbnail(
        kb::assets::AssetId assetId,
        std::size_t maximumTicks);
    [[nodiscard]] ParticleDependencyNavigation
        VerifyParticleDependencyNavigation();
    [[nodiscard]] WorkspaceLayoutPersistence
        VerifyWorkspaceLayoutPersistence();
    [[nodiscard]] SavedLayoutRoundTrip VerifySavedLayoutRoundTrip();
    [[nodiscard]] FloatingWindowFrame VerifyFloatingWindowFrame();
    [[nodiscard]] InspectorDragProfile ProfileInspectorTransformDrag(std::size_t steps);
    [[nodiscard]] IdleFrameProfile ProfileIdleSceneFrame(std::size_t steps);

    [[nodiscard]] bool InspectorPointerDown(int x, int y);
    [[nodiscard]] bool InspectorPointerDrag(int x, int y);
    [[nodiscard]] bool InspectorPointerUp();
    [[nodiscard]] bool InspectorChar(wchar_t character);
    [[nodiscard]] bool InspectorKey(std::uintptr_t key);

    [[nodiscard]] bool CaptureInspector(
        std::string_view checkpoint);
    [[nodiscard]] bool CapturePanel(
        std::string_view panel, std::string_view checkpoint);
    [[nodiscard]] bool CapturePanelScreenshotMatrix(
        std::string_view panel, std::string_view checkpoint);
    [[nodiscard]] bool VerifyViewportHostLifecycle();
    [[nodiscard]] bool VerifySceneRenderTargetAfterSecondary(
        std::string_view checkpoint);
    [[nodiscard]] bool CaptureRuntime(
        std::string_view checkpoint,
        bool requireNonUniform = false);
    [[nodiscard]] bool SnapshotInspectorTree(
        std::string_view checkpoint);
    void SnapshotConsole(std::string_view checkpoint);
    void Trace(
        std::string_view operation, bool succeeded,
        std::string_view detail = {});

    [[nodiscard]] const std::filesystem::path& ArtifactRoot()
        const noexcept;

private:
    struct Impl;

    EditorSceneContext& context_;
    std::filesystem::path artifactRoot_;
    std::filesystem::path tracePath_;
    std::unique_ptr<Impl> impl_;
};

} // namespace kb::editor
