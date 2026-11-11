#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "app/EditorPlayModeState.hpp"
#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorSceneViewportGeometry.hpp"
#include "scene/EditorGameView.hpp"
#include "settings/EditorConfigurationStore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

using kb::editor::EditorGameView;
using kb::editor::EditorGameViewResolution;
using kb::editor::EditorGameViewSize;
using kb::editor::EditorViewportFitMode;
using kb::editor::tests::Require;

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_game_view_tests";
}

[[nodiscard]] long Width(const RECT& rect) { return rect.right - rect.left; }
[[nodiscard]] long Height(const RECT& rect) { return rect.bottom - rect.top; }

void RunResolutionTableTest() {
    const EditorGameViewSize project{ 1280U, 720U };
    Require(EditorGameView::Size(EditorGameViewResolution::FreeAspect, project).FollowsPanel(),
        "Free Aspect should follow the panel");
    Require(EditorGameView::Size(EditorGameViewResolution::ProjectResolution, project) == project,
        "Project Resolution should be the project's window size");
    Require((EditorGameView::Size(EditorGameViewResolution::Landscape16x9, project) == EditorGameViewSize{ 1920U, 1080U }),
        "16:9 should render at 1920 x 1080");
    Require((EditorGameView::Size(EditorGameViewResolution::Portrait9x16, project) == EditorGameViewSize{ 1080U, 1920U }),
        "9:16 phone portrait should render at 1080 x 1920");
    const EditorGameViewSize phone = EditorGameView::Size(EditorGameViewResolution::PhoneLandscape, project);
    Require(std::fabs(static_cast<double>(phone.width) / phone.height - 19.5 / 9.0) < 0.01,
        "The phone landscape choice should be 19.5:9");
    const EditorGameViewSize tablet = EditorGameView::Size(EditorGameViewResolution::Tablet4x3, project);
    Require(tablet.width * 3U == tablet.height * 4U, "The 4:3 tablet should be four to three");
    const EditorGameViewSize wide = EditorGameView::Size(EditorGameViewResolution::Tablet16x10, project);
    Require(wide.width * 10U == wide.height * 16U, "The 16:10 tablet should be sixteen to ten");
    Require(EditorGameView::Size(EditorGameViewResolution::ProjectResolution, EditorGameViewSize{}).FollowsPanel(),
        "A project without a window size should fall back to following the panel");

    for (const EditorGameViewResolution resolution : EditorGameView::kResolutions) {
        const auto parsed = EditorGameView::FromName(EditorGameView::Name(resolution));
        Require(parsed.has_value() && *parsed == resolution, "Every resolution should keep its name through the settings text");
    }
    Require(!EditorGameView::FromName("Nonsense").has_value(), "An unknown name should not select a resolution");

    Require(EditorGameView::Label(EditorGameViewResolution::FreeAspect, project).find('(') == std::string::npos,
        "Free Aspect has no size to show");
    Require(EditorGameView::Label(EditorGameViewResolution::Landscape16x9, project).find("1920 x 1080") != std::string::npos,
        "A fixed choice shows its size");
    Require(EditorGameView::Label(EditorGameViewResolution::ProjectResolution, project).find("1280 x 720") != std::string::npos,
        "Project Resolution shows the project's size");
    Require(EditorGameView::FormatScale(960U, 1920U) == "0.50x", "A half size picture reads 0.50x");
    Require(EditorGameView::FormatScale(1920U, 1920U) == "1.00x", "A picture at its own size reads 1.00x");
    Require(EditorGameView::FormatScale(100U, 0U) == "1.00x", "No render width reads as 1.00x");
}

void RunLetterboxTest() {
    using kb::editor::EditorSceneViewportGeometry;
    // A portrait picture in a wide panel: it takes the full height, with equal bars at both sides.
    const RECT wide{ 0, 0, 1000, 500 };
    const RECT portrait = EditorSceneViewportGeometry::CenteredRectFor(wide, 1080U, 1920U, EditorViewportFitMode::Fit);
    Require(Height(portrait) == 500, "A portrait picture fills the height of a wide panel");
    Require(Width(portrait) < 1000 && std::abs((portrait.left - wide.left) - (wide.right - portrait.right)) <= 1,
        "The bars beside a portrait picture are the same width");
    Require(std::fabs(static_cast<double>(Width(portrait)) / Height(portrait) - 9.0 / 16.0) < 0.01,
        "The picture keeps its 9:16 shape");

    // A wide picture in a tall panel: full width, bars above and below.
    const RECT tall{ 100, 50, 500, 650 };
    const RECT landscape = EditorSceneViewportGeometry::CenteredRectFor(tall, 1920U, 1080U, EditorViewportFitMode::Fit);
    Require(Width(landscape) == 400 && landscape.left == tall.left && landscape.right == tall.right,
        "A landscape picture fills the width of a tall panel");
    Require(std::abs((landscape.top - tall.top) - (tall.bottom - landscape.bottom)) <= 1 && Height(landscape) < Height(tall),
        "The bars above and below a landscape picture are the same height");
    Require(std::fabs(static_cast<double>(Width(landscape)) / Height(landscape) - 16.0 / 9.0) < 0.02,
        "The picture keeps its 16:9 shape");

    // The same shape as the panel needs no bars; Free Aspect renders at the panel's own size.
    const RECT same{ 0, 0, 1600, 900 };
    const RECT exact = EditorSceneViewportGeometry::CenteredRectFor(same, 1920U, 1080U, EditorViewportFitMode::Fit);
    Require(exact.left == same.left && exact.top == same.top && exact.right == same.right && exact.bottom == same.bottom,
        "A picture of the panel's shape fills it");
    const RECT free = EditorSceneViewportGeometry::CenteredRectFor(same, 1600U, 900U, EditorViewportFitMode::Fit);
    Require(free.left == same.left && free.right == same.right && free.top == same.top && free.bottom == same.bottom,
        "Free Aspect fills the panel");
    // A panel too small for the picture shows it scaled down, and the scale reads below 1.
    Require(EditorGameView::FormatScale(static_cast<std::uint32_t>(Width(exact)), 1920U) == "0.83x",
        "The scale shows how far the picture is scaled down");
}

void RunPreferencePersistenceTest() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    const std::filesystem::path root = TempRoot();
    const std::filesystem::path path = kb::editor::EditorConfigurationStore::FilePath(root);
    std::filesystem::create_directories(root / "Config", error);

    // Nothing stored: Play opens in the Game View with a free aspect.
    {
        const auto result = kb::editor::EditorConfigurationStore::Load(path, root);
        Require(!result.found && result.configuration.play.target == kb::editor::EditorPlayTarget::GameView,
            "Play should default to the Game View");
        Require(result.configuration.play.gameViewResolution == EditorGameViewResolution::FreeAspect,
            "The Game View should default to a free aspect");
    }

    kb::editor::EditorConfiguration written;
    written.play.target = kb::editor::EditorPlayTarget::SceneView;
    written.play.gameViewResolution = EditorGameViewResolution::Tablet4x3;
    std::string saveError;
    Require(kb::editor::EditorConfigurationStore::Save(path, root, written, saveError), "Editor settings should save");
    {
        const auto loaded = kb::editor::EditorConfigurationStore::Load(path, root);
        Require(loaded.Succeeded() && loaded.found, "Saved editor settings should load");
        Require(loaded.configuration.play.target == kb::editor::EditorPlayTarget::SceneView,
            "Playing in the Scene View should be kept");
        Require(loaded.configuration.play.gameViewResolution == EditorGameViewResolution::Tablet4x3,
            "The Game View resolution should be kept");
    }
    for (const EditorGameViewResolution resolution : EditorGameView::kResolutions) {
        written.play.target = kb::editor::EditorPlayTarget::GameView;
        written.play.gameViewResolution = resolution;
        Require(kb::editor::EditorConfigurationStore::Save(path, root, written, saveError), "Editor settings should save again");
        const auto loaded = kb::editor::EditorConfigurationStore::Load(path, root);
        Require(loaded.configuration.play.target == kb::editor::EditorPlayTarget::GameView &&
                loaded.configuration.play.gameViewResolution == resolution,
            "Every Game View resolution should survive a save");
    }

    // A file written before the Game View existed, and one with a name this build does not know.
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "[Editor.Play]\nTarget=2\nGameViewResolution=Nonsense\n";
    }
    const auto older = kb::editor::EditorConfigurationStore::Load(path, root);
    Require(older.configuration.play.target == kb::editor::EditorPlayTarget::Standalone,
        "A stored play place from an older editor should still mean the same place");
    Require(older.configuration.play.gameViewResolution == EditorGameViewResolution::FreeAspect,
        "An unknown resolution name should fall back to a free aspect");
    std::filesystem::remove_all(TempRoot(), error);
}

void RunPlayInputFocusTest() {
    kb::editor::EditorPlayModeState play;
    play.SetGameHasInput(true);
    Require(!play.GameHasInput(), "The game has no input while Play is stopped");
    play.Play();
    play.SetGameHasInput(true);
    Require(play.GameHasInput(), "The game takes the input when the Game View is clicked");
    play.SetGameHasInput(false);
    Require(!play.GameHasInput(), "A press elsewhere returns the input to the editor");
    play.SetGameHasInput(true);
    play.Stop();
    Require(!play.GameHasInput(), "Stopping returns the input to the editor");
}

void RunGameViewPanelDockingTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::EditorMetrics metrics{};
    const auto layoutOf = [&]() {
        return model.Queries().BuildLayout(1280, 720, metrics.menuHeight, metrics.statusBarHeight, metrics.tabStripHeight,
            metrics.tabMinWidth, metrics.tabWidth, metrics.splitterSize);
    };
    const kb::editor::DockPanel* game = nullptr;
    for (const kb::editor::DockPanel& panel : model.Queries().Panels()) {
        if (panel.kind == kb::editor::DockPanelKind::Game) {
            game = &panel;
        }
    }
    Require(game != nullptr, "The default workspace has a Game View panel");
    Require(!game->visible && !game->detachable, "The Game View starts closed and stays in the main window");
    const std::uint32_t gameId = game->id;
    const auto find = [](const kb::editor::DockLayout& layout, std::uint32_t id) -> const kb::editor::DockPanelLayout* {
        for (const kb::editor::DockPanelLayout& panel : layout.panels) {
            if (panel.panelId == id) {
                return &panel;
            }
        }
        return nullptr;
    };
    Require(find(layoutOf(), gameId) == nullptr, "A closed Game View has no tab");

    Require(model.Commands().ActivatePanelKind(kb::editor::DockPanelKind::Game, kb::editor::DockArea::Center),
        "Play can open the Game View");
    const kb::editor::DockLayout opened = layoutOf();
    const kb::editor::DockPanelLayout* gameTab = find(opened, gameId);
    const kb::editor::DockPanelLayout* sceneTab = find(opened, 2U);
    Require(gameTab != nullptr && sceneTab != nullptr, "Both the Game View and the Scene View are docked");
    Require(gameTab->leafId == sceneTab->leafId, "The Game View opens as a tab beside the Scene View");
    Require(gameTab->active && !sceneTab->active, "The Game View comes to the front");

    // Stop brings the Scene View back to the front.
    model.Commands().ActivatePanel(2U);
    const kb::editor::DockLayout back = layoutOf();
    Require(find(back, 2U) != nullptr && find(back, 2U)->active, "The Scene View comes back to the front");
    Require(find(back, gameId) != nullptr, "The Game View stays docked as a tab");
}

} // namespace

namespace kb::editor::tests {

void RunEditorGameViewTests() {
    RunResolutionTableTest();
    RunLetterboxTest();
    RunPreferencePersistenceTest();
    RunPlayInputFocusTest();
    RunGameViewPanelDockingTest();
}

} // namespace kb::editor::tests
