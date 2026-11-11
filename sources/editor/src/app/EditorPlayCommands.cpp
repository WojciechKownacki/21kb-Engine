#include "app/EditorPlayCommands.hpp"

#if defined(_WIN32)
#include "app/EditorSceneLifecycleGuard.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "project/EditorProjectPaths.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace kb::editor {
namespace {

bool g_maximizedForPlay = false;
// The tab that was in front of the Game View's group before Play, so Stop brings it back.
std::optional<std::uint32_t> g_panelBeforePlay;

[[nodiscard]] std::optional<std::uint32_t> FrontPanelOfGameViewGroup(EditorDockModel& dockModel, HWND owner, const EditorMetrics& metrics) {
    RECT client{};
    GetClientRect(owner, &client);
    const DockLayout layout = dockModel.Queries().BuildLayout(client.right, client.bottom, metrics.menuHeight, metrics.statusBarHeight,
        metrics.tabStripHeight, metrics.tabMinWidth, metrics.tabWidth, metrics.splitterSize);
    // The Game View's own group when it is open, otherwise the Scene View's: that is where it opens.
    for (const DockPanelKind kind : { DockPanelKind::Game, DockPanelKind::Scene }) {
        for (const DockPanel& panel : dockModel.Queries().Panels()) {
            if (panel.kind != kind) {
                continue;
            }
            for (const DockPanelLayout& candidate : layout.panels) {
                if (candidate.panelId != panel.id) {
                    continue;
                }
                for (const DockLeafLayout& leaf : layout.leaves) {
                    if (leaf.leafId == candidate.leafId) {
                        return leaf.activePanelId;
                    }
                }
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> SceneViewLeaf(EditorDockModel& dockModel, HWND owner, const EditorMetrics& metrics) {
    RECT client{};
    GetClientRect(owner, &client);
    const DockLayout layout = dockModel.Queries().BuildLayout(client.right, client.bottom, metrics.menuHeight, metrics.statusBarHeight,
        metrics.tabStripHeight, metrics.tabMinWidth, metrics.tabWidth, metrics.splitterSize);
    for (const DockPanel& panel : dockModel.Queries().Panels()) {
        if (panel.kind != DockPanelKind::Scene) {
            continue;
        }
        std::optional<std::uint32_t> leaf;
        bool otherLeaves = false;
        for (const DockPanelLayout& candidate : layout.panels) {
            if (candidate.panelId == panel.id) {
                leaf = candidate.leafId;
            }
        }
        for (const DockPanelLayout& candidate : layout.panels) {
            otherLeaves = otherLeaves || (leaf.has_value() && candidate.leafId != *leaf);
        }
        // With nothing beside it the Scene View already fills the editor.
        return otherLeaves ? leaf : std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool LaunchStandalone(EditorSceneContext& sceneContext) {
    std::array<wchar_t, 32768> module{};
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0U || length >= module.size()) {
        return false;
    }
    const std::filesystem::path directory = std::filesystem::path{ module.data() }.parent_path();
    const std::filesystem::path game = directory / "kb_game.exe";
    std::error_code error;
    if (!std::filesystem::is_regular_file(game, error)) {
        sceneContext.Console().Error("Play", "The game program kb_game.exe was not found beside the editor.");
        return false;
    }
    std::wstring line = L"\"" + game.wstring() + L"\" \"--project=" + EditorProjectPaths::ProjectFile().wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(game.c_str(), line.data(), nullptr, nullptr, FALSE, 0U, nullptr, directory.c_str(), &startup, &process) == FALSE) {
        sceneContext.Console().Error("Play", "The game could not be started (error " + std::to_string(GetLastError()) + ").");
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    sceneContext.Console().Info("Play", "The game was started in its own window, from the project's startup scene.");
    return true;
}

} // namespace

bool EditorPlayCommands::Start(HWND owner, EditorDockModel& dockModel, EditorSceneContext& sceneContext, EditorPlayModeState& playMode) {
    if (playMode.Mode() != EditorPlayMode::Stopped) {
        return false;
    }
    const EditorPlayPreferences preferences = sceneContext.EditorConfig().play;
    if (preferences.target == EditorPlayTarget::Standalone) {
        // The game reads the project from disk, so what is unsaved is settled first.
        const std::optional<EditorDirtySceneResolution> resolution =
            EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(owner, sceneContext, EditorSceneTransition::BuildGame);
        if (!resolution.has_value()) {
            return false;
        }
        if (*resolution == EditorDirtySceneResolution::Save && !sceneContext.SaveOpenDocuments()) {
            return false;
        }
        static_cast<void>(LaunchStandalone(sceneContext));
        return false;
    }
    if (!sceneContext.BeginPlayModeSceneSession()) {
        return false;
    }
    playMode.Play();
    if (preferences.startPaused) {
        playMode.Pause();
    }
    if (preferences.target == EditorPlayTarget::GameView) {
        // The Game View opens as a tab beside the Scene View and takes the keyboard and mouse.
        g_panelBeforePlay = FrontPanelOfGameViewGroup(dockModel, owner, EditorMetrics{});
        static_cast<void>(dockModel.Commands().ActivatePanelKind(DockPanelKind::Game, DockArea::Center));
        playMode.SetGameHasInput(true);
    }
    if (preferences.target == EditorPlayTarget::SceneViewMaximized) {
        if (const std::optional<std::uint32_t> leaf = SceneViewLeaf(dockModel, owner, EditorMetrics{})) {
            g_maximizedForPlay = dockModel.Commands().ToggleMaximizedLeaf(*leaf);
        }
    }
    return true;
}

void EditorPlayCommands::RestoreWorkspace(EditorDockModel& dockModel) noexcept {
    if (g_panelBeforePlay.has_value()) {
        const DockPanel* panel = dockModel.Queries().FindPanel(*g_panelBeforePlay);
        if (panel != nullptr && panel->visible) {
            dockModel.Commands().ActivatePanel(*g_panelBeforePlay);
        }
        g_panelBeforePlay.reset();
    }
    if (g_maximizedForPlay) {
        g_maximizedForPlay = false;
        static_cast<void>(dockModel.Commands().RestoreMaximizedLeaf());
    }
}

} // namespace kb::editor

#endif
