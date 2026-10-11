#include "app/editor_settings/EditorSettingsPointerController.hpp"

#if defined(_WIN32)
#include "docking/DefaultDockWorkspace.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "localization/EditorLocalization.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"
#include "scene/EditorSceneContext.hpp"

#include <array>
#include <string>
#include <vector>

namespace kb::editor {

namespace {

// The Language list: every editor language by its own name, the current one checked. The choice
// applies at once and is kept for every project.
// Panels showing their own title take the new language's; a panel titled after an open file keeps it.
void RetitlePanels(const std::vector<DockPanelKind>& ownTitles, EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows) {
    for (const DockPanelKind kind : ownTitles) {
        static_cast<void>(dockModel.Commands().SetPanelTitle(kind, DefaultDockWorkspace::PanelTitle(kind)));
    }
    for (const DockPanel& panel : dockModel.Queries().Panels()) {
        if (HWND window = floatingWindows.Queries().WindowForPanel(panel.id); window != nullptr) {
            SetWindowTextW(window, ScriptEditorTextEncoding::Widen(panel.title).c_str());
        }
    }
}

[[nodiscard]] bool ChooseLanguage(HWND owner, const RECT& field, EditorSceneContext& sceneContext,
    EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return false;
    }
    const auto languages = EditorLocalization::Languages();
    for (std::size_t index = 0; index < languages.size(); ++index) {
        const bool current = languages[index].code == EditorLocalization::Current();
        const std::wstring name = ScriptEditorTextEncoding::Widen(std::string{ languages[index].nativeName });
        AppendMenuW(menu, MF_STRING | (current ? MF_CHECKED : 0U), static_cast<UINT_PTR>(index + 1U), name.c_str());
    }
    POINT anchor{ field.left, field.bottom };
    ClientToScreen(owner, &anchor);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY,
        anchor.x, anchor.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (chosen <= 0 || static_cast<std::size_t>(chosen) > languages.size()) {
        return false;
    }
    const EditorLanguage& language = languages[static_cast<std::size_t>(chosen - 1)];
    std::vector<DockPanelKind> ownTitles;
    for (const DockPanel& panel : dockModel.Queries().Panels()) {
        if (!panel.title.empty() && panel.title == DefaultDockWorkspace::PanelTitle(panel.kind)) {
            ownTitles.push_back(panel.kind);
        }
    }
    std::string error;
    if (!EditorLocalization::SetLanguage(language.code, EditorLocalization::DefaultCatalogFolder(), error)) {
        sceneContext.Console().Error("Editor", error);
        return false;
    }
    if (!EditorLocalization::StorePreferredLanguage(language.code, error)) {
        sceneContext.Console().Warning("Editor", "The editor language was changed but could not be kept: " + error);
    }
    sceneContext.Console().Info("Editor", "Editor language: " + std::string{ language.englishName } + ".");
    RetitlePanels(ownTitles, dockModel, floatingWindows);
    // Every window draws its text again, now in the new language.
    RedrawWindow(GetAncestor(owner, GA_ROOT), nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    return true;
}

} // namespace

bool EditorSettingsPointerController::HandlePointerDown(
    HWND owner,
    const RECT& content,
    int x,
    int y) {
    const EditorSettingsHit hit = EditorSettingsPanelRenderer::HitTest(content, x, y);
    if (hit.kind == EditorSettingsHitKind::Category) {
        EditorSettingsPanelRenderer::SelectCategory(static_cast<EditorSettingsCategory>(hit.option));
        return true;
    }
    if (hit.kind == EditorSettingsHitKind::Language) {
        return ChooseLanguage(owner, EditorSettingsPanelRenderer::LanguageField(content), sceneContext_, dockModel_,
            floatingWindows_);
    }
    if (hit.kind != EditorSettingsHitKind::Toggle &&
        hit.kind != EditorSettingsHitKind::Choice) {
        return false;
    }

    const EditorSavingPreferences current = sceneContext_.CaptureEditorSavingPreferences();
    EditorSavingPreferences candidate = current;
    if (hit.kind == EditorSettingsHitKind::Toggle && hit.row == 0) {
        candidate.autosaveEnabled = !candidate.autosaveEnabled;
    } else if (hit.kind == EditorSettingsHitKind::Choice && hit.row == 1) {
        constexpr std::array<std::uint32_t, 5> intervals{{5U, 10U, 15U, 30U, 60U}};
        if (!candidate.autosaveEnabled ||
            hit.option < 0 ||
            hit.option >= static_cast<int>(intervals.size())) {
            return false;
        }
        candidate.autosaveIntervalMinutes =
            intervals[static_cast<std::size_t>(hit.option)];
    }

    if (candidate == current) return false;
    return sceneContext_.CommitEditorSettings(candidate);
}

} // namespace kb::editor
#endif
