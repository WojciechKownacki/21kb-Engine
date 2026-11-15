#pragma once

#include "app/EditorShellInteractionState.hpp"
#include "rendering/HeroIconKind.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace kb::editor {

// A text the editor shows in the user's language: the catalog key and the English text.
// The localization scan reads both from the call, as it does for EditorText.
struct EditorMenuText {
    std::string_view key;
    std::string_view english;

    constexpr EditorMenuText() noexcept = default;
    constexpr EditorMenuText(std::string_view textKey, std::string_view englishText) noexcept
        : key(textKey), english(englishText) {}

    [[nodiscard]] constexpr bool Empty() const noexcept { return key.empty(); }
    // The text in the editor language; empty for a text that was never set.
    [[nodiscard]] std::string_view Resolve() const;
};

// What choosing a menu row does. The pointer handler carries each one out; a row names its action here
// so that painting, clicking and any other way to run a menu command read the same table.
enum class EditorMenuAction : std::uint8_t {
    None,
    // File, Scene and Project.
    NewScene,
    OpenScene,
    SaveScene,
    SaveSceneAs,
    OpenBuildGame,
    OpenProject,
    NewProject,
    ShowRecentProjects,
    ShowProjectMenu,
    SwitchProject,
    ShowInExplorer,
    OpenSceneFile,
    DuplicateScene,
    RenameScene,
    DeleteScene,
    // Edit.
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    Duplicate,
    OpenCommandPalette,
    // Layout.
    ApplyDefaultLayout,
    ApplyLayout,
    SaveLayout,
    ShowDeleteLayoutList,
    DeleteLayout,
    TogglePanel,
    // Options.
    CycleRenderBackend,
    OpenProjectSettings,
    OpenEditorSettings,
    OpenPlugins,
    OpenProfiler,
    OpenAudioMixer,
    OpenAgent,
    // World.
    LoadCellsNearCamera,
    LoadAllCells,
    UnloadAllCells,
    BuildWorld,
    ToggleCellGrid,
    ConvertSceneToWorld,
    AddDataLayer,
    CycleSelectionDataLayer,
    ToggleSelectionAlwaysLoaded,
    BakeNavigation,
    ToggleNavigationMesh,
    ToggleNavigationAutoUpdate,
    BuildLighting,
    // Help.
    OpenDocumentation,
    ReportIssue,
    OpenReleaseNotes,
    ShowAbout,
    // Play options and Publish.
    SetPlayTarget,
    ToggleStartPaused,
    PublishPlatform,
    OpenPublishSettings,
};

// What a row looks like and allows right now; read from the shell's state when the menu is resolved.
struct EditorMenuRowState {
    bool enabled = true;
    // Drawn as the chosen one of a set.
    bool selected = false;
    // Drawn with a check mark in place of its icon.
    bool checked = false;
};

struct EditorMenuRowSpec;
using EditorMenuStateFn = EditorMenuRowState (*)(const EditorShellInteractionState& shell, const EditorMenuRowSpec& row);

// One fixed row of a menu: everything the menu paints for it and everything a click on it needs.
struct EditorMenuRowSpec {
    // Stable name of the row ("edit.copy"), the same in every language.
    std::string_view id;
    EditorMenuText label;
    // The line under the label; unset when the label says it all.
    EditorMenuText detail;
    HeroIconKind icon = HeroIconKind::RectangleGroup;
    // The key bound to the same command, shown on the right; empty when none is bound.
    std::string_view shortcut;
    EditorMenuAction action = EditorMenuAction::None;
    // What the action needs besides itself, such as the place Play runs in.
    int argument = 0;
    // A rule sets the row apart from the one above.
    bool startsGroup = false;
    bool submenu = false;
    bool danger = false;
    // The menu goes away before the action runs, not after.
    bool closesFirst = false;
    // The caption above the row; unset for none.
    EditorMenuText heading;
    // Null for a row that is always enabled and never marked.
    EditorMenuStateFn state = nullptr;
};

// A menu row as it is shown and clicked now: a fixed row of the table or one the project supplies (a scene,
// a platform, a recent project, a saved layout). Strings and the path point into the shell's state and stay
// valid until that state changes.
struct EditorMenuRowView {
    EditorMenuCommand menu = EditorMenuCommand::None;
    std::string_view id;
    std::string_view label;
    std::string_view detail;
    std::string_view shortcut;
    std::string_view heading;
    HeroIconKind icon = HeroIconKind::RectangleGroup;
    EditorMenuAction action = EditorMenuAction::None;
    int argument = 0;
    // The scene or project the row opens.
    const std::filesystem::path* path = nullptr;
    // The saved layout the row applies or removes.
    std::string_view name;
    bool enabled = true;
    bool selected = false;
    // Marked on: shown with a check mark in place of its icon.
    bool checked = false;
    bool danger = false;
    bool submenu = false;
    bool startsGroup = false;
    bool closesFirst = false;
};

class EditorMenuTable {
public:
    EditorMenuTable() = delete;

    // The rows the build fixes, in the order the menu lists them. Empty for Layout, whose rows are the
    // layouts the project holds.
    [[nodiscard]] static std::span<const EditorMenuRowSpec> FixedRows(EditorMenuCommand menu) noexcept;
    [[nodiscard]] static int FixedRowCount(EditorMenuCommand menu) noexcept;

    // Where a menu puts a rule or a caption, given how many rows it shows now.
    [[nodiscard]] static bool StartsGroup(EditorMenuCommand menu, int row, int rowCount) noexcept;
    [[nodiscard]] static std::string_view Heading(EditorMenuCommand menu, int row, int rowCount);

    // Every row the open menu shows, in order, with the state they have now.
    [[nodiscard]] static std::vector<EditorMenuRowView> Rows(EditorMenuCommand menu, const EditorShellInteractionState& shell);
    // The row under a hit-test result; null for a result outside the menu.
    [[nodiscard]] static const EditorMenuRowView* RowAt(const std::vector<EditorMenuRowView>& rows, int index) noexcept;
    // Every row of every menu in the order the menus are listed, for anything that lists the editor's
    // commands. The rows a project supplies (layouts, scenes, platforms, recent projects) are the ones
    // `shell` holds; its strings must outlive the result.
    [[nodiscard]] static std::vector<EditorMenuRowView> AllRows(const EditorShellInteractionState& shell);
    // Puts the rows of `rows` (an AllRows result) a user can run on their own into the palette's commands, each
    // valued with its place in `rows`. Rows that only open another list, or the palette itself, are left out.
    static void ListCommands(EditorCommandPaletteModel& palette, const std::vector<EditorMenuRowView>& rows);
    // The row of `rows` a chosen palette command stands for; null when the menus changed since it was listed.
    [[nodiscard]] static const EditorMenuRowView* CommandRow(const std::vector<EditorMenuRowView>& rows, const EditorCommandPaletteModel::Row& chosen) noexcept;
};

} // namespace kb::editor
