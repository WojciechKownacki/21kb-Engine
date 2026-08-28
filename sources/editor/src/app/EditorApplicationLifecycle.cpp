#include "app/EditorApplicationLifecycle.hpp"

#if defined(_WIN32)
#include "app/EditorApplicationWindowProc.hpp"
#include "app/EditorWorkspaceSession.hpp"
#include "docking/EditorFloatingWindowSync.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "platform/win32/EditorMainWindow.hpp"
#include "platform/win32/EditorWindowClassRegistry.hpp"
#include "project/EditorProjectPaths.hpp"

#include <string>
#include <string_view>

namespace kb::editor {
namespace {

constexpr wchar_t kWindowTitle[] = L"21kb Engine";
constexpr int kInitialWindowWidth = 1600;
constexpr int kInitialWindowHeight = 960;

[[nodiscard]] const DockPanelLayout* FindPanelLayout(const DockLayout& layout, std::uint32_t panelId) noexcept {
    for (const DockPanelLayout& candidate : layout.panels) {
        if (candidate.panelId == panelId) return &candidate;
    }
    return nullptr;
}

void RestoreParticleEditorHostSession(EditorApplicationState& state) {
    constexpr std::uint32_t panelId = 14U;
    const EditorPanelSession* stored = state.sceneContext.EditorConfig().FindPanel(panelId);
    if (stored == nullptr) {
        return;
    }
    const EditorPanelSession& session = *stored;
    if (!session.visible) {
        static_cast<void>(state.dockModel.Commands().ClosePanel(panelId));
        return;
    }
    if (session.area == DockArea::Floating) {
        state.dockModel.Commands().UndockPanel(panelId, session.floatingRect);
        static_cast<void>(state.floatingWindows.Commands().Create(
            panelId, "21kb Particle System", session.floatingRect));
    } else {
        RECT client{};
        GetClientRect(state.window, &client);
        const DockLayout layout = state.dockModel.Queries().BuildLayout(
            client.right, client.bottom, state.metrics.menuHeight, state.metrics.toolbarHeight,
            state.metrics.tabStripHeight, state.metrics.tabMinWidth, state.metrics.tabWidth,
            state.metrics.splitterSize, state.metrics.panelPadding);
        const DockPanel* targetPanel = nullptr;
        for (const DockPanel& candidate : state.dockModel.Queries().Panels()) {
            if (candidate.id != panelId && candidate.visible && candidate.area == session.area) {
                targetPanel = &candidate;
                break;
            }
        }
        const DockPanelLayout* target = targetPanel == nullptr ? nullptr : FindPanelLayout(layout, targetPanel->id);
        if (target != nullptr) {
            state.dockModel.Commands().DockPanelTo(panelId, {
                .zone = DockDropZone::Center,
                .kind = DockDropPreviewKind::Glow,
                .leafId = target->leafId,
            });
        }
    }
    if (!session.documentPath.empty()) {
        kb::assets::AssetId assetId{};
        auto& manager = state.sceneContext.Scene().Assets().Manager();
        for (const auto& candidate : manager.Registry().All()) {
            const auto mounted = manager.Mounts().Resolve(candidate.virtualPath);
            const std::filesystem::path candidatePath = mounted.has_value()
                ? *mounted
                : candidate.physicalPath;
            std::error_code pathError;
            const std::filesystem::path absoluteCandidate =
                std::filesystem::absolute(candidatePath, pathError).lexically_normal();
            if (!pathError && absoluteCandidate == session.documentPath.lexically_normal()) {
                assetId = candidate.id;
                break;
            }
        }
        if (assetId.IsValid() && state.sceneContext.OpenParticleEditorAsset(assetId)) {
            const auto* opened = state.sceneContext.Scene().Assets().Manager().Registry().Find(assetId);
            if (opened != nullptr) {
                static_cast<void>(state.dockModel.Commands().SetPanelTitle(
                    DockPanelKind::ParticleEditor, opened->virtualPath.filename().string()));
            }
        }
    }
}

} // namespace

bool EditorApplicationLifecycle::Initialize(EditorApplicationState& state) {
    state.instance = GetModuleHandleW(nullptr);

    if (!state.windowClasses.Register(state.instance, &EditorApplicationWindowProc::Handle)) {
        return false;
    }

    state.window = EditorMainWindow::Create(state.instance, EditorWindowClassRegistry::MainWindowClassName, kWindowTitle, kInitialWindowWidth, kInitialWindowHeight, &state);
    if (state.window == nullptr) {
        state.windowClasses.Unregister();
        return false;
    }
    if (RegisterTouchWindow(state.window, 0U) == 0) {
        DestroyWindow(state.window);
        state.window = nullptr;
        state.windowClasses.Unregister();
        return false;
    }

    EditorMainWindow::EnableDarkMode(state.window);
    static_cast<void>(state.sceneContext.LoadEditorSettings());
    state.sceneViewport.Configure(state.instance, state.window, &state.renderBackendSettings);
    state.sceneViewport.SetErrorReporter([&state](std::string_view message) {
        state.sceneContext.Console().Error("Renderer", std::string{ message });
    });
    state.sceneViewport.SetAaTraceReporter([&state](std::string_view message) {
        state.sceneContext.Console().Info("AA", std::string{ message });
    });
    state.sceneContext.SetRenderSceneReleaseHandler(
        [&state](const kb::scene::Scene& scene) {
            state.sceneViewport.ReleaseScene(scene);
        });
    state.sceneContext.SetParticlePreviewReleaseHandler(
        [&state](const kb::scene::Scene& scene) {
            state.sceneViewport.ReleaseScene(scene);
        });
    state.floatingWindows.Lifecycle().Configure(state.instance, state.window, state.metrics);
    state.dockController.Configure(state.window, state.dockModel, state.floatingWindows, state.metrics);
    EditorWorkspaceSession::Restore(state.dockModel, state.sceneContext);
    RestoreParticleEditorHostSession(state);
    // A panel the session left torn off needs its window back, or it would be neither
    // in the dock tree nor on screen. The particle editor has already made its own by
    // now, and creating a window a panel already has is a no-op.
    EditorFloatingWindowSync::Reconcile(state.dockModel, state.floatingWindows);

    ShowWindow(state.window, SW_SHOWMAXIMIZED);
    UpdateWindow(state.window);

    state.running = true;
    return true;
}

void EditorApplicationLifecycle::Shutdown(EditorApplicationState& state) {
    static_cast<void>(state.sceneContext.RestorePlayModeSceneSession());
    static_cast<void>(state.sceneContext.SaveDirtySceneDocument("application shutdown"));
    EditorWorkspaceSession::Save(state.dockModel, state.sceneContext);
    if (state.sceneContext.HasParticleEditorAsset()) state.sceneContext.CloseParticleEditorAsset();
    state.sceneContext.SetParticlePreviewReleaseHandler({});
    state.sceneContext.SetRenderSceneReleaseHandler({});
    state.sceneViewport.Shutdown();
    state.floatingWindows.Lifecycle().Shutdown();

    if (state.window != nullptr) {
        static_cast<void>(UnregisterTouchWindow(state.window));
        DestroyWindow(state.window);
        state.window = nullptr;
    }

    state.windowClasses.Unregister();
    state.instance = nullptr;
    state.running = false;
}

} // namespace kb::editor

#endif
