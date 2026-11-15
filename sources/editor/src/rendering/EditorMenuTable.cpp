#include "rendering/EditorMenuTable.hpp"

#include "localization/EditorLocalization.hpp"
#include "rendering/EditorToolbarLayout.hpp"

#include <array>
#include <utility>

namespace kb::editor {
namespace {

using Action = EditorMenuAction;
using Icon = HeroIconKind;

[[nodiscard]] EditorMenuRowState SceneDocumentState(const EditorShellInteractionState& shell, const EditorMenuRowSpec&) {
    return { .enabled = shell.SceneDocumentActionsEnabled() };
}

[[nodiscard]] EditorMenuRowState PlayTargetState(const EditorShellInteractionState& shell, const EditorMenuRowSpec& row) {
    return {
        .enabled = shell.PlayMenuCanChange(),
        .selected = shell.PlayMenuPreferences().target == static_cast<EditorPlayTarget>(row.argument),
    };
}

[[nodiscard]] EditorMenuRowState StartPausedState(const EditorShellInteractionState& shell, const EditorMenuRowSpec&) {
    return { .enabled = shell.PlayMenuCanChange(), .checked = shell.PlayMenuPreferences().startPaused };
}

// Rows that only turn a menu into another list, or open the palette itself, are not commands to run.
[[nodiscard]] bool ListedAsCommand(const EditorMenuRowView& row) noexcept {
    return row.enabled && row.action != Action::None && row.action != Action::ShowRecentProjects && row.action != Action::ShowProjectMenu &&
           row.action != Action::ShowDeleteLayoutList && row.action != Action::OpenCommandPalette;
}

// Where a row is found: the menu it belongs to, in the editor language.
[[nodiscard]] std::string_view MenuName(EditorMenuCommand menu) {
    switch (menu) {
    case EditorMenuCommand::Project:
        return EditorText("menu.heading.project", "Project");
    case EditorMenuCommand::RecentProjects:
        return EditorText("menu.project.recent", "Recent Projects");
    case EditorMenuCommand::Scene:
        return EditorText("palette.menu.scene", "Scene");
    case EditorMenuCommand::PlayOptions:
        return EditorText("palette.menu.play", "Play");
    case EditorMenuCommand::Publish:
        return EditorText("palette.menu.publish", "Publish");
    default:
        return EditorToolbarLayout::MenuLabel(menu);
    }
}

constexpr std::array<EditorMenuRowSpec, 5> kFileRows{{
    { .id = "file.newScene", .label = EditorMenuText("menu.file.newScene", "New Scene..."),
      .detail = EditorMenuText("menu.detail.newScene", "An empty scene in the project"), .icon = Icon::Plus, .action = Action::NewScene },
    { .id = "file.openScene", .label = EditorMenuText("menu.file.openScene", "Open Scene..."),
      .detail = EditorMenuText("menu.detail.openScene", "Choose one from disk"), .icon = Icon::Folder, .action = Action::OpenScene },
    { .id = "file.save", .label = EditorMenuText("common.save", "Save"),
      .detail = EditorMenuText("menu.detail.save", "Saves the current scene"), .icon = Icon::Save, .shortcut = "Ctrl+S",
      .action = Action::SaveScene, .startsGroup = true },
    { .id = "file.saveAs", .label = EditorMenuText("menu.file.saveAs", "Save As..."),
      .detail = EditorMenuText("menu.detail.saveAs", "Under a new name"), .icon = Icon::DocumentText, .action = Action::SaveSceneAs },
    { .id = "file.buildGame", .label = EditorMenuText("menu.file.buildGame", "Build Game"),
      .detail = EditorMenuText("menu.detail.buildGame", "For the chosen platform"), .icon = Icon::RocketLaunch,
      .action = Action::OpenBuildGame, .startsGroup = true },
}};

// Only the shortcuts the editor actually binds (EditorEditCommandInputHandler); a row without one shows
// nothing rather than a key that does not work.
constexpr std::array<EditorMenuRowSpec, 7> kEditRows{{
    { .id = "edit.undo", .label = EditorMenuText("menu.edit.undo", "Undo"), .icon = Icon::Undo, .shortcut = "Ctrl+Z", .action = Action::Undo },
    { .id = "edit.redo", .label = EditorMenuText("menu.edit.redo", "Redo"), .icon = Icon::Redo, .shortcut = "Ctrl+Y", .action = Action::Redo },
    { .id = "edit.cut", .label = EditorMenuText("menu.edit.cut", "Cut"), .icon = Icon::Scissors, .shortcut = "Ctrl+X",
      .action = Action::Cut, .startsGroup = true },
    { .id = "edit.copy", .label = EditorMenuText("menu.edit.copy", "Copy"), .icon = Icon::ClipboardCopy, .shortcut = "Ctrl+C", .action = Action::Copy },
    { .id = "edit.paste", .label = EditorMenuText("menu.edit.paste", "Paste"), .icon = Icon::ClipboardPaste, .shortcut = "Ctrl+V", .action = Action::Paste },
    { .id = "edit.duplicate", .label = EditorMenuText("menu.edit.duplicate", "Duplicate"), .icon = Icon::Duplicate, .shortcut = "Ctrl+D",
      .action = Action::Duplicate },
    { .id = "edit.commandPalette", .label = EditorMenuText("menu.edit.commandPalette", "Find Command or Asset..."),
      .detail = EditorMenuText("menu.detail.commandPalette", "Type a name to run a command or open an asset"), .icon = Icon::MagnifyingGlass,
      .shortcut = "Ctrl+Shift+P", .action = Action::OpenCommandPalette, .startsGroup = true, .closesFirst = true },
}};

constexpr std::array<EditorMenuRowSpec, 7> kOptionsRows{{
    { .id = "options.renderer", .label = EditorMenuText("menu.options.renderer", "Renderer"),
      .detail = EditorMenuText("menu.detail.renderer", "The next graphics backend"), .icon = Icon::Monitor, .action = Action::CycleRenderBackend },
    { .id = "options.projectSettings", .label = EditorMenuText("menu.options.projectSettings", "Project Settings"),
      .detail = EditorMenuText("menu.detail.projectSettings", "Inputs, graphics, physics"), .icon = Icon::AdjustmentsHorizontal,
      .action = Action::OpenProjectSettings, .startsGroup = true },
    { .id = "options.editorSettings", .label = EditorMenuText("menu.options.editorSettings", "Editor Settings"),
      .detail = EditorMenuText("menu.detail.editorSettings", "Language and how the editor works"), .icon = Icon::WrenchScrewdriver,
      .action = Action::OpenEditorSettings },
    { .id = "options.plugins", .label = EditorMenuText("menu.edit.plugins", "Plugins"),
      .detail = EditorMenuText("menu.detail.plugins", "Enabled modules"), .icon = Icon::Plug, .action = Action::OpenPlugins },
    { .id = "options.profiler", .label = EditorMenuText("panel.profiler", "Profiler"),
      .detail = EditorMenuText("menu.detail.profiler", "Frame timings"), .icon = Icon::Activity, .action = Action::OpenProfiler,
      .startsGroup = true },
    { .id = "options.audioMixer", .label = EditorMenuText("inspector.audioMixer.subtitle", "Audio Mixer"),
      .detail = EditorMenuText("menu.detail.audioMixer", "Buses and volumes"), .icon = Icon::SpeakerWave, .action = Action::OpenAudioMixer },
    { .id = "options.agent", .label = EditorMenuText("menu.options.agent", "AI Agent"),
      .detail = EditorMenuText("menu.detail.agent", "Tell the agent what to do"), .icon = Icon::Bolt, .shortcut = "Ctrl+K",
      .action = Action::OpenAgent, .startsGroup = true },
}};

constexpr std::array<EditorMenuRowSpec, 13> kWorldRows{{
    { .id = "world.loadCellsNearCamera", .label = EditorMenuText("menu.world.loadCellsNearCamera", "Load Cells Near Camera"),
      .detail = EditorMenuText("menu.detail.loadNear", "Around the view"), .icon = Icon::Camera, .action = Action::LoadCellsNearCamera },
    { .id = "world.loadAllCells", .label = EditorMenuText("menu.world.loadAllCells", "Load All Cells"),
      .detail = EditorMenuText("menu.detail.loadAll", "The whole world into memory"), .icon = Icon::Download, .action = Action::LoadAllCells },
    { .id = "world.unloadAllCells", .label = EditorMenuText("menu.world.unloadAllCells", "Unload All Cells"),
      .detail = EditorMenuText("menu.detail.unloadAll", "Frees the memory"), .icon = Icon::Upload, .action = Action::UnloadAllCells },
    { .id = "world.buildWorld", .label = EditorMenuText("menu.world.buildWorldAndHlods", "Build World and HLODs"),
      .detail = EditorMenuText("menu.detail.buildWorld", "Streaming data and far objects"), .icon = Icon::WrenchScrewdriver,
      .action = Action::BuildWorld, .startsGroup = true },
    { .id = "world.toggleCellGrid", .label = EditorMenuText("menu.world.toggleCellGrid", "Show or Hide Cell Grid"),
      .detail = EditorMenuText("menu.detail.cellGrid", "Cell boundaries in the view"), .icon = Icon::Grid, .action = Action::ToggleCellGrid },
    { .id = "world.convertSceneToWorld", .label = EditorMenuText("menu.world.convertSceneToWorld", "Convert Scene to World"),
      .detail = EditorMenuText("menu.detail.convertWorld", "Splits it into streamed cells"), .icon = Icon::Map,
      .action = Action::ConvertSceneToWorld },
    { .id = "world.addDataLayer", .label = EditorMenuText("menu.world.addDataLayer", "Add Data Layer..."),
      .detail = EditorMenuText("menu.detail.addLayer", "A new group of objects"), .icon = Icon::Plus, .action = Action::AddDataLayer,
      .startsGroup = true },
    { .id = "world.cycleSelectionDataLayer", .label = EditorMenuText("menu.world.cycleSelectionDataLayer", "Cycle Selection Data Layer"),
      .detail = EditorMenuText("menu.detail.cycleLayer", "The next layer for the selection"), .icon = Icon::Layers,
      .action = Action::CycleSelectionDataLayer },
    { .id = "world.toggleSelectionAlwaysLoaded", .label = EditorMenuText("menu.world.toggleSelectionAlwaysLoaded", "Toggle Selection Always Loaded"),
      .detail = EditorMenuText("menu.detail.alwaysLoaded", "Stays in memory with every cell"), .icon = Icon::LockClosed,
      .action = Action::ToggleSelectionAlwaysLoaded },
    { .id = "world.bakeNavigationMesh", .label = EditorMenuText("menu.world.bakeNavigationMesh", "Bake Navigation Mesh"),
      .detail = EditorMenuText("menu.detail.bakeNavigation", "Where characters can walk"), .icon = Icon::Navigation,
      .action = Action::BakeNavigation, .startsGroup = true },
    { .id = "world.toggleNavigationMesh", .label = EditorMenuText("menu.world.toggleNavigationMesh", "Show or Hide Navigation Mesh"),
      .detail = EditorMenuText("menu.detail.showNavigation", "Walkable areas in the view"), .icon = Icon::Eye,
      .action = Action::ToggleNavigationMesh },
    { .id = "world.toggleNavigationAutoUpdate",
      .label = EditorMenuText("menu.world.toggleNavigationAutoUpdate", "Turn Automatic Navigation Update On or Off"),
      .detail = EditorMenuText("menu.detail.navigationAutoUpdate", "Rebuilds the tiles an edit touches"), .icon = Icon::Clock,
      .action = Action::ToggleNavigationAutoUpdate },
    { .id = "world.buildLighting", .label = EditorMenuText("menu.world.buildLighting", "Build Lighting"),
      .detail = EditorMenuText("menu.detail.buildLighting", "Bakes lights and bounces into lightmaps"), .icon = Icon::Bolt,
      .action = Action::BuildLighting },
}};

constexpr std::array<EditorMenuRowSpec, 4> kHelpRows{{
    { .id = "help.documentation", .label = EditorMenuText("menu.help.documentation", "Documentation"),
      .detail = EditorMenuText("menu.detail.documentation", "Opens in the browser"), .icon = Icon::BookOpen, .action = Action::OpenDocumentation },
    { .id = "help.reportIssue", .label = EditorMenuText("menu.help.reportIssue", "Report Issue"),
      .detail = EditorMenuText("menu.detail.reportIssue", "A new issue on GitHub"), .icon = Icon::Flag, .action = Action::ReportIssue },
    { .id = "help.releaseNotes", .label = EditorMenuText("menu.help.releaseNotes", "Release Notes"),
      .detail = EditorMenuText("menu.detail.releaseNotes", "What changed"), .icon = Icon::DocumentText, .action = Action::OpenReleaseNotes },
    { .id = "help.about", .label = EditorMenuText("menu.help.about", "About"),
      .detail = EditorMenuText("menu.detail.about", "Version and license"), .icon = Icon::Info, .action = Action::ShowAbout,
      .startsGroup = true },
}};

// The Project menu hangs from the project button, so each of its rows puts the menu away before it acts.
constexpr std::array<EditorMenuRowSpec, 7> kProjectRows{{
    { .id = "project.open", .label = EditorMenuText("menu.project.open", "Open Project..."),
      .detail = EditorMenuText("menu.detail.openProject", "Another project from disk"), .icon = Icon::Folder, .action = Action::OpenProject,
      .closesFirst = true, .heading = EditorMenuText("menu.heading.project", "Project") },
    { .id = "project.new", .label = EditorMenuText("menu.project.new", "New Project..."),
      .detail = EditorMenuText("menu.detail.newProject", "A new folder with a first scene"), .icon = Icon::Plus, .action = Action::NewProject,
      .closesFirst = true },
    // Opens the recent list in place of this menu instead of putting it away.
    { .id = "project.recent", .label = EditorMenuText("menu.project.recent", "Recent Projects"),
      .detail = EditorMenuText("menu.detail.recentProjects", "Quick way back"), .icon = Icon::Clock, .action = Action::ShowRecentProjects,
      .submenu = true },
    { .id = "project.settings", .label = EditorMenuText("menu.options.projectSettings", "Project Settings"),
      .detail = EditorMenuText("menu.detail.projectSettings", "Inputs, graphics, physics"), .icon = Icon::AdjustmentsHorizontal,
      .action = Action::OpenProjectSettings, .startsGroup = true, .closesFirst = true,
      .heading = EditorMenuText("menu.heading.settings", "Settings") },
    { .id = "project.plugins", .label = EditorMenuText("menu.edit.plugins", "Plugins"),
      .detail = EditorMenuText("menu.detail.plugins", "Enabled modules"), .icon = Icon::Plug, .action = Action::OpenPlugins,
      .closesFirst = true },
    { .id = "project.buildGame", .label = EditorMenuText("menu.file.buildGame", "Build Game"),
      .detail = EditorMenuText("menu.detail.buildGame", "For the chosen platform"), .icon = Icon::WrenchScrewdriver,
      .action = Action::OpenBuildGame, .startsGroup = true, .closesFirst = true },
    { .id = "project.showInExplorer", .label = EditorMenuText("menu.project.showInExplorer", "Show in Explorer"),
      .detail = EditorMenuText("menu.detail.showInExplorer", "The project folder"), .icon = Icon::ExternalLink,
      .action = Action::ShowInExplorer, .closesFirst = true },
}};

// The recent list's first row takes the user back to the Project menu; the projects follow it.
constexpr std::array<EditorMenuRowSpec, 1> kRecentProjectsRows{{
    { .id = "recent.back", .label = EditorMenuText("common.back", "Back"), .icon = Icon::ChevronLeft, .action = Action::ShowProjectMenu },
}};

// The scenes of the project come first; these actions follow them. The ones that change the open scene's
// file wait for a scene they can change.
constexpr std::array<EditorMenuRowSpec, 6> kSceneRows{{
    { .id = "scene.new", .label = EditorMenuText("menu.file.newScene", "New Scene..."),
      .detail = EditorMenuText("menu.detail.newScene", "An empty scene in the project"), .icon = Icon::Plus, .action = Action::NewScene,
      .startsGroup = true, .closesFirst = true, .heading = EditorMenuText("menu.heading.actions", "Actions") },
    { .id = "scene.open", .label = EditorMenuText("menu.file.openScene", "Open Scene..."),
      .detail = EditorMenuText("menu.detail.openScene", "Choose one from disk"), .icon = Icon::Folder, .action = Action::OpenScene,
      .closesFirst = true },
    { .id = "scene.duplicate", .label = EditorMenuText("menu.scene.duplicate", "Duplicate Scene"),
      .detail = EditorMenuText("menu.detail.duplicateScene", "A copy beside this scene"), .icon = Icon::Duplicate,
      .action = Action::DuplicateScene, .closesFirst = true, .state = &SceneDocumentState },
    { .id = "scene.rename", .label = EditorMenuText("menu.scene.rename", "Rename..."),
      .detail = EditorMenuText("menu.detail.renameScene", "The scene file's name"), .icon = Icon::Pencil, .action = Action::RenameScene,
      .closesFirst = true, .state = &SceneDocumentState },
    { .id = "scene.save", .label = EditorMenuText("common.save", "Save"),
      .detail = EditorMenuText("menu.detail.save", "Saves the current scene"), .icon = Icon::Save, .shortcut = "Ctrl+S",
      .action = Action::SaveScene, .closesFirst = true },
    { .id = "scene.delete", .label = EditorMenuText("menu.scene.delete", "Delete Scene"),
      .detail = EditorMenuText("menu.detail.deleteScene", "Goes to the Recycle Bin"), .icon = Icon::Trash, .action = Action::DeleteScene,
      .startsGroup = true, .danger = true, .closesFirst = true, .state = &SceneDocumentState },
}};

// The places Play runs the game in the order the arrow beside Play lists them; Start Paused follows.
constexpr std::array<EditorMenuRowSpec, 5> kPlayOptionsRows{{
    { .id = "play.gameView", .label = EditorMenuText("menu.play.gameView", "In the Game View"),
      .detail = EditorMenuText("menu.detail.playGameView", "What the player sees, at a resolution you choose"), .icon = Icon::Play,
      .action = Action::SetPlayTarget, .argument = static_cast<int>(EditorPlayTarget::GameView), .closesFirst = true,
      .heading = EditorMenuText("menu.heading.playWhere", "Run"), .state = &PlayTargetState },
    { .id = "play.sceneView", .label = EditorMenuText("menu.play.sceneView", "In the Scene View"),
      .detail = EditorMenuText("menu.detail.playSceneView", "The open scene, inside the editor"), .icon = Icon::Monitor,
      .action = Action::SetPlayTarget, .argument = static_cast<int>(EditorPlayTarget::SceneView), .closesFirst = true,
      .state = &PlayTargetState },
    { .id = "play.maximized", .label = EditorMenuText("menu.play.maximized", "Maximized Scene View"),
      .detail = EditorMenuText("menu.detail.playMaximized", "Scene View fills the editor while playing"), .icon = Icon::Scale,
      .action = Action::SetPlayTarget, .argument = static_cast<int>(EditorPlayTarget::SceneViewMaximized), .closesFirst = true,
      .state = &PlayTargetState },
    { .id = "play.standalone", .label = EditorMenuText("menu.play.standalone", "In Its Own Window"),
      .detail = EditorMenuText("menu.detail.playStandalone", "From the startup scene, like the finished game"), .icon = Icon::Gamepad2,
      .action = Action::SetPlayTarget, .argument = static_cast<int>(EditorPlayTarget::Standalone), .closesFirst = true,
      .state = &PlayTargetState },
    { .id = "play.startPaused", .label = EditorMenuText("menu.play.startPaused", "Start Paused"),
      .detail = EditorMenuText("menu.detail.playStartPaused", "Waits on the first frame"), .icon = Icon::Pause,
      .action = Action::ToggleStartPaused, .startsGroup = true, .closesFirst = true,
      .heading = EditorMenuText("menu.heading.options", "Options"), .state = &StartPausedState },
}};

// The platforms come first; Release Settings follows them.
constexpr std::array<EditorMenuRowSpec, 1> kPublishRows{{
    { .id = "publish.settings", .label = EditorMenuText("menu.publish.settings", "Release Settings..."),
      .detail = EditorMenuText("menu.detail.publishSettings", "Version, signing and output folder"), .icon = Icon::AdjustmentsHorizontal,
      .action = Action::OpenPublishSettings, .startsGroup = true, .closesFirst = true },
}};

// Where the rows a project supplies sit among the fixed ones.
enum class DynamicRows : std::uint8_t { None, BeforeFixed, AfterFixed };

struct MenuDefinition {
    EditorMenuCommand menu = EditorMenuCommand::None;
    std::span<const EditorMenuRowSpec> fixed;
    DynamicRows dynamic = DynamicRows::None;
    // The caption above the first of the supplied rows.
    EditorMenuText dynamicHeading;
    // A rule sets the first supplied row apart from the one above.
    bool dynamicStartsGroup = false;
};

const std::array<MenuDefinition, 11> kMenus{{
    { EditorMenuCommand::File, kFileRows },
    { EditorMenuCommand::Edit, kEditRows },
    // Layout lists the layouts the project holds, so it has no fixed rows.
    { EditorMenuCommand::Layout, {}, DynamicRows::AfterFixed },
    { EditorMenuCommand::Options, kOptionsRows },
    { EditorMenuCommand::World, kWorldRows },
    { EditorMenuCommand::Help, kHelpRows },
    { EditorMenuCommand::Project, kProjectRows },
    { EditorMenuCommand::RecentProjects, kRecentProjectsRows, DynamicRows::AfterFixed, {}, true },
    { EditorMenuCommand::Scene, kSceneRows, DynamicRows::BeforeFixed, EditorMenuText("menu.heading.scenes", "Scenes in the Project") },
    { EditorMenuCommand::PlayOptions, kPlayOptionsRows },
    { EditorMenuCommand::Publish, kPublishRows, DynamicRows::BeforeFixed, EditorMenuText("menu.heading.publishPlatform", "Release Build For") },
}};

[[nodiscard]] const MenuDefinition* FindMenu(EditorMenuCommand menu) noexcept {
    for (const MenuDefinition& definition : kMenus) {
        if (definition.menu == menu) {
            return &definition;
        }
    }
    return nullptr;
}

// What stands at `row` of a menu showing `rowCount` rows: one of its fixed rows, or the first or a later one
// of the rows the project supplies.
struct Slot {
    const EditorMenuRowSpec* fixed = nullptr;
    bool firstSupplied = false;
};

[[nodiscard]] Slot SlotAt(const MenuDefinition& definition, int row, int rowCount) noexcept {
    const auto fixedCount = static_cast<int>(definition.fixed.size());
    const int supplied = rowCount > fixedCount ? rowCount - fixedCount : 0;
    const int firstFixed = definition.dynamic == DynamicRows::BeforeFixed ? supplied : 0;
    if (row >= firstFixed && row < firstFixed + fixedCount) {
        return { .fixed = &definition.fixed[static_cast<std::size_t>(row - firstFixed)] };
    }
    const int firstSupplied = definition.dynamic == DynamicRows::BeforeFixed ? 0 : fixedCount;
    return { .firstSupplied = row == firstSupplied };
}

[[nodiscard]] HeroIconKind LayoutRowIcon(const EditorLayoutMenuRow& row) noexcept {
    if (row.active) {
        return HeroIconKind::Check;
    }
    switch (row.action) {
    case EditorLayoutMenuAction::Save:
        return HeroIconKind::Plus;
    case EditorLayoutMenuAction::Delete:
    case EditorLayoutMenuAction::Remove:
        return HeroIconKind::XMark;
    case EditorLayoutMenuAction::Default:
    case EditorLayoutMenuAction::Apply:
    case EditorLayoutMenuAction::TogglePanel:
    default:
        return HeroIconKind::RectangleGroup;
    }
}

[[nodiscard]] EditorMenuRowView LayoutRowView(const EditorLayoutMenuRow& row) {
    EditorMenuRowView view{
        .menu = EditorMenuCommand::Layout,
        .label = row.label,
        .icon = LayoutRowIcon(row),
        .argument = static_cast<int>(row.panelKind),
        .name = row.layoutName,
        .enabled = row.enabled,
        .checked = row.active,
    };
    switch (row.action) {
    case EditorLayoutMenuAction::Default:
        view.id = "layout.default";
        view.action = EditorMenuAction::ApplyDefaultLayout;
        break;
    case EditorLayoutMenuAction::Apply:
        view.id = "layout.apply";
        view.action = EditorMenuAction::ApplyLayout;
        break;
    case EditorLayoutMenuAction::Save:
        view.id = "layout.save";
        view.action = EditorMenuAction::SaveLayout;
        break;
    case EditorLayoutMenuAction::Delete:
        view.id = "layout.delete";
        view.action = EditorMenuAction::ShowDeleteLayoutList;
        break;
    case EditorLayoutMenuAction::Remove:
        view.id = "layout.remove";
        view.action = EditorMenuAction::DeleteLayout;
        break;
    case EditorLayoutMenuAction::TogglePanel:
        view.id = "layout.panel";
        view.action = EditorMenuAction::TogglePanel;
        break;
    }
    return view;
}

[[nodiscard]] EditorMenuRowView FixedRowView(EditorMenuCommand menu, const EditorMenuRowSpec& spec, const EditorShellInteractionState& shell) {
    EditorMenuRowView view{
        .menu = menu,
        .id = spec.id,
        .label = spec.label.Resolve(),
        .detail = spec.detail.Resolve(),
        .shortcut = spec.shortcut,
        .icon = spec.icon,
        .action = spec.action,
        .argument = spec.argument,
        .danger = spec.danger,
        .submenu = spec.submenu,
        .closesFirst = spec.closesFirst,
    };
    if (spec.state != nullptr) {
        const EditorMenuRowState state = spec.state(shell, spec);
        view.enabled = state.enabled;
        view.selected = state.selected;
        view.checked = state.checked;
        if (state.checked) {
            view.icon = HeroIconKind::Check;
        }
    }
    return view;
}

// The rows the project supplies for the menus that list some.
void AppendSuppliedRows(EditorMenuCommand menu, const EditorShellInteractionState& shell, std::vector<EditorMenuRowView>& rows) {
    switch (menu) {
    case EditorMenuCommand::Layout:
        for (const EditorLayoutMenuRow& row : shell.LayoutMenu().Rows()) {
            rows.push_back(LayoutRowView(row));
        }
        break;
    case EditorMenuCommand::Scene:
        for (const EditorPathMenuRow& scene : shell.SceneMenuRows()) {
            rows.push_back({
                .menu = menu,
                .id = "scene.file",
                .label = scene.label,
                .icon = HeroIconKind::Cube,
                .action = EditorMenuAction::OpenSceneFile,
                .path = &scene.path,
                .selected = scene.active,
                .closesFirst = true,
            });
        }
        break;
    case EditorMenuCommand::RecentProjects:
        for (const EditorPathMenuRow& project : shell.RecentProjectRows()) {
            rows.push_back({
                .menu = menu,
                .id = "recent.project",
                .label = project.label,
                .detail = project.detail,
                .icon = HeroIconKind::Cube,
                .action = EditorMenuAction::SwitchProject,
                .path = &project.path,
                .enabled = !project.path.empty(),
                .closesFirst = true,
            });
        }
        break;
    case EditorMenuCommand::Publish: {
        int platform = 0;
        for (const EditorPathMenuRow& row : shell.PublishRows()) {
            rows.push_back({
                .menu = menu,
                .id = "publish.platform",
                .label = row.label,
                .detail = row.detail,
                .icon = row.active ? HeroIconKind::RocketLaunch : HeroIconKind::Warning,
                .action = EditorMenuAction::PublishPlatform,
                .argument = platform++,
                .closesFirst = true,
            });
        }
        break;
    }
    default:
        break;
    }
}

} // namespace

std::string_view EditorMenuText::Resolve() const {
    return key.empty() ? std::string_view{} : EditorText(key, english);
}

std::span<const EditorMenuRowSpec> EditorMenuTable::FixedRows(EditorMenuCommand menu) noexcept {
    const MenuDefinition* definition = FindMenu(menu);
    return definition != nullptr ? definition->fixed : std::span<const EditorMenuRowSpec>{};
}

int EditorMenuTable::FixedRowCount(EditorMenuCommand menu) noexcept {
    return static_cast<int>(FixedRows(menu).size());
}

bool EditorMenuTable::StartsGroup(EditorMenuCommand menu, int row, int rowCount) noexcept {
    const MenuDefinition* definition = FindMenu(menu);
    // Nothing is above the first row to set it apart from.
    if (definition == nullptr || row <= 0 || row >= rowCount) {
        return false;
    }
    const Slot slot = SlotAt(*definition, row, rowCount);
    return slot.fixed != nullptr ? slot.fixed->startsGroup : (slot.firstSupplied && definition->dynamicStartsGroup);
}

std::string_view EditorMenuTable::Heading(EditorMenuCommand menu, int row, int rowCount) {
    const MenuDefinition* definition = FindMenu(menu);
    if (definition == nullptr || row < 0 || row >= rowCount) {
        return {};
    }
    const Slot slot = SlotAt(*definition, row, rowCount);
    return slot.fixed != nullptr ? slot.fixed->heading.Resolve() : (slot.firstSupplied ? definition->dynamicHeading.Resolve() : std::string_view{});
}

std::vector<EditorMenuRowView> EditorMenuTable::Rows(EditorMenuCommand menu, const EditorShellInteractionState& shell) {
    std::vector<EditorMenuRowView> rows;
    const MenuDefinition* definition = FindMenu(menu);
    if (definition == nullptr) {
        return rows;
    }
    const auto appendFixed = [&] {
        for (const EditorMenuRowSpec& spec : definition->fixed) {
            rows.push_back(FixedRowView(menu, spec, shell));
        }
    };
    if (definition->dynamic == DynamicRows::BeforeFixed) {
        AppendSuppliedRows(menu, shell, rows);
        appendFixed();
    } else {
        appendFixed();
        AppendSuppliedRows(menu, shell, rows);
    }
    const auto count = static_cast<int>(rows.size());
    for (int row = 0; row < count; ++row) {
        EditorMenuRowView& view = rows[static_cast<std::size_t>(row)];
        view.startsGroup = StartsGroup(menu, row, count);
        view.heading = Heading(menu, row, count);
    }
    return rows;
}

const EditorMenuRowView* EditorMenuTable::RowAt(const std::vector<EditorMenuRowView>& rows, int index) noexcept {
    return index >= 0 && static_cast<std::size_t>(index) < rows.size() ? &rows[static_cast<std::size_t>(index)] : nullptr;
}

std::vector<EditorMenuRowView> EditorMenuTable::AllRows(const EditorShellInteractionState& shell) {
    std::vector<EditorMenuRowView> all;
    for (const MenuDefinition& definition : kMenus) {
        for (EditorMenuRowView& row : Rows(definition.menu, shell)) {
            all.push_back(std::move(row));
        }
    }
    return all;
}

void EditorMenuTable::ListCommands(EditorCommandPaletteModel& palette, const std::vector<EditorMenuRowView>& rows) {
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (ListedAsCommand(rows[index])) {
            palette.AddCommand(rows[index].label, MenuName(rows[index].menu), rows[index].shortcut, index);
        }
    }
}

const EditorMenuRowView* EditorMenuTable::CommandRow(const std::vector<EditorMenuRowView>& rows, const EditorCommandPaletteModel::Row& chosen) noexcept {
    return chosen.value < rows.size() && rows[static_cast<std::size_t>(chosen.value)].label == chosen.label ? &rows[static_cast<std::size_t>(chosen.value)] : nullptr;
}

} // namespace kb::editor
