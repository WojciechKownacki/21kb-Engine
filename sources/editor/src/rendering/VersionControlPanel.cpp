#include "rendering/VersionControlPanel.hpp"

#if defined(_WIN32)
#include "engine/scene/SceneDocumentService.hpp"
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "project/EditorVersionControl.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace kb::editor {
namespace {

constexpr int kPad = 12;
constexpr int kRow = 22;
constexpr int kButtonHeight = 26;
constexpr int kPointSize = 12;
constexpr COLORREF kAmber = RGB(224, 165, 54);
constexpr COLORREF kGreen = RGB(63, 194, 123);
constexpr COLORREF kRed = RGB(232, 108, 108);

struct Layout {
    RECT status{};
    RECT undo{};
    RECT compare{};
    RECT changes{};
    RECT sceneTitle{};
    RECT scene{};
};

[[nodiscard]] COLORREF Color(const EditorColor& color) noexcept {
    return RGB(color.r, color.g, color.b);
}

[[nodiscard]] bool Inside(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void Text(HDC dc, RECT rect, std::string_view text, COLORREF color, int weight = FW_NORMAL, UINT format = DT_LEFT) {
    const ScopedFont font(kPointSize, weight);
    const ScopedGdiObject selected(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

[[nodiscard]] Layout MakeLayout(const RECT& content) {
    Layout layout;
    const RECT inner{ content.left + kPad, content.top + kPad, content.right - kPad, content.bottom - kPad };
    layout.status = RECT{ inner.left, inner.top, inner.right, inner.top + 20 };
    const int buttonWidth = std::clamp((static_cast<int>(inner.right - inner.left) - 8) / 2, 140, 300);
    const int buttonTop = static_cast<int>(layout.status.bottom) + 8;
    layout.undo = RECT{ inner.left, buttonTop, inner.left + buttonWidth, buttonTop + kButtonHeight };
    layout.compare = RECT{ layout.undo.right + 8, buttonTop, layout.undo.right + 8 + buttonWidth, buttonTop + kButtonHeight };
    const int bodyTop = static_cast<int>(layout.undo.bottom) + 10;
    const int bodyHeight = std::max(0, static_cast<int>(inner.bottom) - bodyTop);
    const int changesHeight = std::max(kRow * 3, bodyHeight * 2 / 5);
    layout.changes = RECT{ inner.left, bodyTop, inner.right, std::min(bodyTop + changesHeight, static_cast<int>(inner.bottom)) };
    layout.sceneTitle = RECT{ inner.left, layout.changes.bottom + 10, inner.right, layout.changes.bottom + 10 + 20 };
    layout.scene = RECT{ inner.left, layout.sceneTitle.bottom + 4, inner.right, std::max(static_cast<int>(layout.sceneTitle.bottom) + 4, static_cast<int>(inner.bottom)) };
    return layout;
}

[[nodiscard]] int VisibleRows(const RECT& area) noexcept {
    return std::max(0, static_cast<int>(area.bottom - area.top) / kRow);
}

[[nodiscard]] std::string StateLabel(EditorChangeState state) {
    switch (state) {
    case EditorChangeState::Modified: return std::string{ EditorText("versionControl.state.changed", "Changed") };
    case EditorChangeState::Added: return std::string{ EditorText("versionControl.state.added", "Added") };
    case EditorChangeState::Deleted: return std::string{ EditorText("versionControl.state.deleted", "Deleted") };
    case EditorChangeState::Untracked: return std::string{ EditorText("versionControl.state.new", "New") };
    }
    return {};
}

[[nodiscard]] COLORREF StateColor(EditorChangeState state) noexcept {
    switch (state) {
    case EditorChangeState::Modified: return kAmber;
    case EditorChangeState::Deleted: return kRed;
    case EditorChangeState::Added:
    case EditorChangeState::Untracked: return kGreen;
    }
    return kAmber;
}

// The open scene's file as the repository names it, when the scene can be compared with its last saved version.
[[nodiscard]] std::optional<std::string> SceneRepositoryPath(const EditorSceneContext& sceneContext, const EditorChangeList& changes) {
    if (!changes.available || sceneContext.InPrefabEditMode() || sceneContext.CurrentScenePath().empty()) {
        return std::nullopt;
    }
    std::error_code error;
    const std::filesystem::path scene = std::filesystem::weakly_canonical(sceneContext.CurrentScenePath(), error);
    const std::filesystem::path root = std::filesystem::weakly_canonical(changes.root, error);
    const std::filesystem::path relative = scene.lexically_relative(root);
    if (error || relative.empty() || *relative.begin() == "..") {
        return std::nullopt;
    }
    const std::u8string text = relative.generic_u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] const EditorFileChange* SelectedChange(const EditorVersionControl& control) {
    const std::size_t selected = control.Selected();
    return selected < control.Changes().changes.size() ? &control.Changes().changes[selected] : nullptr;
}

[[nodiscard]] bool CanUndo(const EditorVersionControl& control) {
    const EditorFileChange* change = SelectedChange(control);
    return change != nullptr && EditorProjectRepository::CanRestore(change->state);
}

[[nodiscard]] bool CanCompare(const EditorVersionControl& control, const EditorSceneContext& sceneContext) {
    return SceneRepositoryPath(sceneContext, control.Changes()).has_value();
}

// One line of the scene comparison: an object, or one value of the chosen object.
struct SceneLine {
    std::string text;
    COLORREF color = 0;
    // The object a click on the line chooses; kNone for a line that is only information.
    std::size_t object = EditorVersionControl::kNone;
    bool detail = false;
};

[[nodiscard]] std::vector<SceneLine> SceneLines(const EditorVersionControl& control, const EditorTheme& theme) {
    std::vector<SceneLine> lines;
    if (control.Comparing()) {
        lines.push_back({ std::string{ EditorText("versionControl.comparing", "Comparing...") }, Color(theme.textDisabled) });
        return lines;
    }
    if (!control.HasComparison()) {
        lines.push_back({ std::string{ EditorText("versionControl.notCompared", "Choose Compare to see what changed in the open scene.") }, Color(theme.textDisabled) });
        return lines;
    }
    const EditorSceneComparison& comparison = control.Comparison();
    if (!comparison.succeeded) {
        lines.push_back({ EditorLocalization::Format("versionControl.compareFailed", "The scene could not be compared: {reason}", { { "reason", comparison.error } }),
            kRed });
        return lines;
    }
    if (comparison.objects.empty()) {
        lines.push_back({ std::string{ EditorText("versionControl.sceneSame", "The scene is the same as the last saved version.") }, Color(theme.textDisabled) });
        return lines;
    }
    for (std::size_t index = 0U; index < comparison.objects.size(); ++index) {
        const EditorSceneObjectChange& object = comparison.objects[index];
        switch (object.kind) {
        case EditorSceneChangeKind::Added:
            lines.push_back({ EditorLocalization::Format("versionControl.objectAdded", "Added: {path}", { { "path", object.path } }), kGreen, index });
            break;
        case EditorSceneChangeKind::Removed:
            lines.push_back({ EditorLocalization::Format("versionControl.objectRemoved", "Removed: {path}", { { "path", object.path } }), kRed, index });
            break;
        case EditorSceneChangeKind::Changed:
            lines.push_back({ EditorLocalization::Format("versionControl.objectChanged", "Changed: {path}", { { "path", object.path } }), kAmber, index });
            break;
        }
        if (index != control.SelectedObject()) {
            continue;
        }
        for (const EditorSceneFieldChange& field : object.fields) {
            std::string text;
            if (field.property.empty()) {
                text = field.before.empty()
                    ? EditorLocalization::Format("versionControl.componentAdded", "{component} added", { { "component", field.component } })
                    : EditorLocalization::Format("versionControl.componentRemoved", "{component} removed", { { "component", field.component } });
            } else {
                text = EditorLocalization::Format("versionControl.fieldChanged", "{component} {property}: was {before}, now {after}",
                    { { "component", field.component }, { "property", field.property }, { "before", field.before }, { "after", field.after } });
            }
            lines.push_back({ std::move(text), Color(theme.textSecondary), index, true });
        }
    }
    return lines;
}

void DrawButton(HDC dc, const RECT& rect, const EditorTheme& theme, std::string_view label, bool enabled) {
    GdiDrawing::DrawSharpFrame(dc, rect, Color(theme.toolbarButton), Color(theme.borderPanel));
    Text(dc, RECT{ rect.left + 8, rect.top, rect.right - 8, rect.bottom }, label, Color(enabled ? theme.textPrimary : theme.textDisabled), FW_NORMAL, DT_CENTER);
}

// The panel can only scroll as far as its list is long.
[[nodiscard]] int MaximumScroll(std::size_t lines, const RECT& area) noexcept {
    return std::max(0, static_cast<int>(lines) - VisibleRows(area));
}

} // namespace

void VersionControlPanel::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const EditorVersionControl& control = EditorVersionControl::Instance();
    const EditorChangeList& changes = control.Changes();
    const Layout layout = MakeLayout(content);
    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));

    if (control.Known() && !changes.available) {
        Text(dc, layout.status, EditorText("versionControl.unavailable", "Version control not available"), Color(theme.textPrimary), FW_SEMIBOLD);
        Text(dc, RECT{ layout.status.left, layout.status.bottom + 4, layout.status.right, layout.status.bottom + 28 },
            EditorText("versionControl.unavailableHint", "Open a project that is in a git repository, and make sure git is installed."), Color(theme.textDisabled));
        return;
    }
    if (!control.Known()) {
        Text(dc, layout.status, EditorText("versionControl.checking", "Checking for changes..."), Color(theme.textDisabled));
        return;
    }

    const std::string count = std::to_string(changes.changes.size());
    const std::string summary = changes.branch.empty()
        ? EditorLocalization::Format("versionControl.summary", "Changes: {count}", { { "count", count } })
        : EditorLocalization::Format("versionControl.summaryBranch", "Changes: {count} on branch {branch}", { { "count", count }, { "branch", changes.branch } });
    Text(dc, layout.status, summary, Color(theme.textPrimary), FW_SEMIBOLD);
    DrawButton(dc, layout.undo, theme, EditorText("versionControl.undo", "Undo changes to this file"), CanUndo(control));
    DrawButton(dc, layout.compare, theme, EditorText("versionControl.compare", "Compare with last saved version"), CanCompare(control, sceneContext));

    // The changed files.
    if (changes.changes.empty()) {
        Text(dc, RECT{ layout.changes.left, layout.changes.top, layout.changes.right, layout.changes.top + kRow },
            EditorText("versionControl.noChanges", "No changes since the last saved version."), Color(theme.textDisabled));
    }
    const int visibleChanges = VisibleRows(layout.changes);
    for (int row = 0; row < visibleChanges; ++row) {
        const std::size_t index = static_cast<std::size_t>(control.changeScroll + row);
        if (index >= changes.changes.size()) {
            break;
        }
        const EditorFileChange& change = changes.changes[index];
        const RECT line{ layout.changes.left, layout.changes.top + row * kRow, layout.changes.right, layout.changes.top + (row + 1) * kRow };
        if (index == control.Selected()) {
            GdiDrawing::FillRectColor(dc, line, Color(theme.header));
        }
        Text(dc, RECT{ line.left + 6, line.top, line.left + 96, line.bottom }, StateLabel(change.state), StateColor(change.state));
        Text(dc, RECT{ line.left + 100, line.top, line.right - 6, line.bottom }, change.path, Color(theme.textPrimary));
    }

    // What changed in the open scene.
    Text(dc, layout.sceneTitle, EditorText("versionControl.sceneChanges", "Scene changes"), Color(theme.textPrimary), FW_SEMIBOLD);
    const std::vector<SceneLine> lines = SceneLines(control, theme);
    const int visibleLines = VisibleRows(layout.scene);
    for (int row = 0; row < visibleLines; ++row) {
        const std::size_t index = static_cast<std::size_t>(control.sceneScroll + row);
        if (index >= lines.size()) {
            break;
        }
        const SceneLine& line = lines[index];
        const RECT area{ layout.scene.left, layout.scene.top + row * kRow, layout.scene.right, layout.scene.top + (row + 1) * kRow };
        if (!line.detail && line.object != EditorVersionControl::kNone && line.object == control.SelectedObject()) {
            GdiDrawing::FillRectColor(dc, area, Color(theme.header));
        }
        Text(dc, RECT{ area.left + (line.detail ? 24 : 6), area.top, area.right - 6, area.bottom }, line.text, line.color);
    }
}

bool VersionControlPanel::Pointer(HWND owner, const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    EditorVersionControl& control = EditorVersionControl::Instance();
    const EditorChangeList& changes = control.Changes();
    const Layout layout = MakeLayout(content);
    if (!control.Known() || !changes.available) {
        return true;
    }

    if (Inside(layout.changes, x, y)) {
        const std::size_t index = static_cast<std::size_t>(control.changeScroll + (y - layout.changes.top) / kRow);
        control.Select(index < changes.changes.size() ? index : EditorVersionControl::kNone);
        return true;
    }

    if (Inside(layout.undo, x, y) && CanUndo(control)) {
        const EditorFileChange change = *SelectedChange(control);
        const EditorChoiceDialogResult answer = EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
            .title = std::string{ EditorText("versionControl.undoTitle", "Undo changes") },
            .message = EditorLocalization::Format("versionControl.undoMessage", "Undo all changes to {file}?", { { "file", change.path } }),
            .supportingText = std::string{ EditorText("versionControl.undoSupport", "The file goes back to the last saved version. Your changes to it are lost.") },
            .primaryLabel = std::string{ EditorText("versionControl.undoConfirm", "Undo changes") },
            .cancelLabel = std::string{ EditorText("common.cancel", "Cancel") },
            .primaryTone = EditorDialogButtonTone::Destructive,
        });
        if (answer != EditorChoiceDialogResult::Primary) {
            return true;
        }
        const EditorRepositoryResult restored = EditorProjectRepository::Restore(changes.root, change.path);
        if (!restored.succeeded) {
            sceneContext.Console().Info("Version Control", restored.message);
            return true;
        }
        control.RequestRefresh();
        control.ClearComparison();
        // A scene brought back replaces what the editor shows, unless there are edits that would be lost.
        const std::optional<std::string> sceneFile = SceneRepositoryPath(sceneContext, changes);
        if (sceneFile.has_value() && *sceneFile == change.path) {
            if (sceneContext.SceneDocumentDirty()) {
                sceneContext.Console().Info("Version Control",
                    std::string{ EditorText("versionControl.sceneNotReloaded", "The open scene has edits that are not saved, so it was not reloaded.") });
            } else {
                static_cast<void>(sceneContext.ReloadSceneAfterExternalChange());
            }
        }
        return true;
    }

    if (Inside(layout.compare, x, y) && CanCompare(control, sceneContext)) {
        const std::filesystem::path scenePath = sceneContext.CurrentScenePath();
        control.CompareScene(changes.root, *SceneRepositoryPath(sceneContext, changes),
            kb::scene::SceneDocumentService::Capture(sceneContext.Scene(), scenePath.stem().string()));
        return true;
    }

    if (Inside(layout.scene, x, y)) {
        const std::vector<SceneLine> lines = SceneLines(control, EditorTheme{});
        const std::size_t index = static_cast<std::size_t>(control.sceneScroll + (y - layout.scene.top) / kRow);
        if (index >= lines.size() || lines[index].object == EditorVersionControl::kNone) {
            return true;
        }
        const std::size_t object = lines[index].object;
        control.SelectObject(object == control.SelectedObject() && !lines[index].detail ? EditorVersionControl::kNone : object);
        // An added or changed object is selected in the Hierarchy; a removed one is no longer there.
        const EditorSceneObjectChange& change = control.Comparison().objects[object];
        if (change.kind != EditorSceneChangeKind::Removed) {
            const kb::scene::SceneEntity entity = EditorSceneComparer::Find(sceneContext.Scene(), change.address);
            if (entity.IsValid()) {
                sceneContext.SelectEntity(entity);
            }
        }
        return true;
    }
    return true;
}

bool VersionControlPanel::Wheel(const RECT& content, int x, int y, int delta) {
    EditorVersionControl& control = EditorVersionControl::Instance();
    const Layout layout = MakeLayout(content);
    const int step = delta > 0 ? -3 : 3;
    if (Inside(layout.changes, x, y)) {
        const int next = std::clamp(control.changeScroll + step, 0, MaximumScroll(control.Changes().changes.size(), layout.changes));
        const bool moved = next != control.changeScroll;
        control.changeScroll = next;
        return moved;
    }
    if (Inside(layout.scene, x, y)) {
        const std::vector<SceneLine> lines = SceneLines(control, EditorTheme{});
        const int next = std::clamp(control.sceneScroll + step, 0, MaximumScroll(lines.size(), layout.scene));
        const bool moved = next != control.sceneScroll;
        control.sceneScroll = next;
        return moved;
    }
    return false;
}

} // namespace kb::editor

#endif
