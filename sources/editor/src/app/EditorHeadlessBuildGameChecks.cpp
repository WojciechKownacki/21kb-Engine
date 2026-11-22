#include "app/EditorHeadlessPopupChecks.hpp"
#include "app/EditorKeyState.hpp"
#include "app/EditorWindowUnderTest.hpp"
#include "packaging/EditorProjectPackageService.hpp"
#include "rendering/BuildGamePanelLayout.hpp"
#include "rendering/BuildGamePanelModel.hpp"
#include "rendering/BuildGamePanelRenderer.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
namespace kb::editor {
namespace {

using namespace headless;

[[nodiscard]] std::wstring Wide(std::string_view text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    if (length > 0) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

[[nodiscard]] POINT Middle(const RECT& rect) noexcept {
    return POINT{ (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 };
}

} // namespace

EditorHeadlessPopupCheckResult EditorHeadlessPopupChecks::VerifyBuildGamePanel(
    EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport, const EditorBuildGamePanelCheck& check) {
    EditorWindowUnderTest editor(sceneContext, viewport);
    if (!editor.Create()) return { false, "window creation" };
    const EditorScriptedInputScope scriptedInput;
    const HWND main = editor.Window();
    std::string failures;
    std::string log;
    const auto expect = [&](bool condition, std::string_view what) {
        if (!condition) failures += std::string{ what } + "; ";
        return condition;
    };

    // The panel is opened as the editor opens it and fills the window, so every row is reachable by scrolling.
    static_cast<void>(editor.Dock().Commands().ActivatePanelKind(DockPanelKind::BuildGame, DockArea::Center));
    editor.Pump();
    std::optional<RECT> found;
    for (const DockPanelLayout& panel : editor.Layout().panels) {
        const DockPanel* model = editor.Dock().Queries().FindPanel(panel.panelId);
        if (model != nullptr && model->kind == DockPanelKind::BuildGame && panel.active) {
            found = RECT{ panel.content.x, panel.content.y, panel.content.x + panel.content.width, panel.content.y + panel.content.height };
        }
    }
    if (!expect(found.has_value(), "the Build Game panel did not open")) return { false, failures };
    const RECT content = *found;
    const BuildGamePanelLayoutRects rects = BuildGamePanelLayout::Resolve(content);

    // The target and the profile are chosen with clicks on the sidebar rows.
    const auto targets = kb::packaging::PackagingTargets();
    int targetIndex = -1;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        if (targets[index].target == check.target) targetIndex = static_cast<int>(index);
    }
    if (!expect(targetIndex >= 0, "unknown target")) return { false, failures };
    bool targetClicked = false;
    for (int y = rects.platformsList.top; y < rects.platformsList.bottom && !targetClicked; y += 2) {
        const POINT point{ (rects.platformsList.left + rects.platformsList.right) / 2, y };
        if (BuildGamePanelRenderer::HitTestSidebar(content, point.x, point.y).target == targetIndex) {
            editor.Click(main, point);
            targetClicked = true;
        }
    }
    expect(targetClicked && sceneContext.BuildGameSelectedTarget() == targetIndex, "a click on the target row did not select it");
    const int profile = check.release ? 1 : 0;
    bool profileClicked = false;
    for (int y = rects.profilesList.top; y < rects.profilesList.bottom && !profileClicked; y += 2) {
        const POINT point{ (rects.profilesList.left + rects.profilesList.right) / 2, y };
        if (BuildGamePanelRenderer::HitTestSidebar(content, point.x, point.y).profile == profile) {
            editor.Click(main, point);
            profileClicked = true;
        }
    }
    expect(profileClicked && sceneContext.BuildGameSelectedProfile() == profile, "a click on the profile row did not select it");

    // Finds where a click lands on a field: scrolls the settings until the row is in view, opening a folded section
    // with a click on its header on the way. Returns the point the next click must use.
    const int x = (rects.body.left + rects.body.right) / 2;
    const auto locate = [&](BuildGameField field) -> std::optional<POINT> {
        const int maxScroll = BuildGamePanelLayout::MaxScrollOffset(rects.body, BuildGamePanelRenderer::SettingsContentHeight(sceneContext));
        for (int offset = 0; offset <= maxScroll + 40; offset += 40) {
            static_cast<void>(sceneContext.SetBuildGameScrollOffset(std::min(offset, maxScroll), maxScroll));
            for (int y = rects.body.top; y < rects.body.bottom; y += 3) {
                const BuildGamePanelRenderer::RowHit hit = BuildGamePanelRenderer::HitTest(content, sceneContext, x, y);
                if (hit.section >= 0 && hit.row < 0 && sceneContext.IsBuildGameSectionCollapsed(hit.section)) {
                    // Opened, the rows below move; the caller looks again.
                    editor.Click(main, POINT{ x, y });
                    return std::nullopt;
                }
                if (BuildGamePanelRenderer::FieldForHit(sceneContext, hit) == field) return POINT{ x, y + 4 };
            }
        }
        return std::nullopt;
    };
    const auto reach = [&](BuildGameField field) {
        // A folded section opens on the first pass; the second finds the row inside it.
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (const std::optional<POINT> point = locate(field)) return point;
        }
        return std::optional<POINT>{};
    };

    // Every row the target shows can be reached and a click on it lands on its own field.
    const std::vector<BuildGameSectionSpec> sections = BuildGamePanelModel::Sections(check.target, check.release);
    int rows = 0;
    for (const BuildGameSectionSpec& section : sections) {
        for (const BuildGameRowSpec& row : section.rows) {
            ++rows;
            expect(reach(row.field).has_value(), "a row cannot be clicked: field " + std::to_string(static_cast<int>(row.field)));
        }
    }
    log += std::to_string(sections.size()) + " sections, " + std::to_string(rows) + " rows clickable; ";

    // Text is typed as the keyboard types it: a click on the row, one WM_CHAR per UTF-16 unit, then Return.
    for (const auto& [field, value] : check.typed) {
        const std::optional<POINT> point = reach(field);
        if (!expect(point.has_value(), "a typed field cannot be reached")) continue;
        editor.Click(main, *point);
        if (!expect(sceneContext.IsBuildGameTextEditing() && sceneContext.BuildGameEditingField() == field,
                "a click on a text row did not start editing it: field " + std::to_string(static_cast<int>(field)))) continue;
        for (const wchar_t unit : Wide(value)) PostMessageW(main, WM_CHAR, static_cast<WPARAM>(unit), 0);
        PostMessageW(main, WM_KEYDOWN, VK_RETURN, 0);
        PostMessageW(main, WM_KEYUP, VK_RETURN, 0);
        editor.Pump();
        expect(!sceneContext.IsBuildGameTextEditing(), "Return did not commit a typed field");
        if (BuildGamePanelModel::IsSecret(field)) {
            expect(sceneContext.HasBuildGameSecret(field) || field == BuildGameField::AndroidStorePassword ||
                    field == BuildGameField::AndroidKeyPassword, "a typed password was not kept");
        } else {
            const std::string shown = BuildGamePanelModel::Value(field, check.target, sceneContext.ProjectConfiguration(),
                sceneContext.BuildGameSettings());
            expect(shown == value, "typed '" + value + "' but the field shows '" + shown + "'");
        }
    }
    if (check.release && (check.target == kb::packaging::PackagingTarget::AndroidAstcArm64 ||
            check.target == kb::packaging::PackagingTarget::AndroidEtc2Arm64)) {
        expect(sceneContext.HasBuildGameStorePassword(), "the typed keystore password was not kept for the build");
    }

    // The launch checkbox flips with a click and back.
    if (const std::optional<POINT> point = reach(BuildGameField::LaunchAfterBuild)) {
        const bool before = sceneContext.BuildGameSettings().For(check.target).launchAfterBuild;
        editor.Click(main, *point);
        expect(sceneContext.BuildGameSettings().For(check.target).launchAfterBuild != before, "a click did not flip Launch after build");
        if (sceneContext.BuildGameSettings().For(check.target).launchAfterBuild) editor.Click(main, *point);
        expect(!sceneContext.BuildGameSettings().For(check.target).launchAfterBuild, "Launch after build could not be turned off");
    }

    // Rows that open a file dialog or a list: the click is checked to land on them above; what the dialog returns
    // is applied the way the panel applies it.
    if (!check.icon.empty()) expect(sceneContext.ImportBuildGameApplicationIcon(check.icon), "the application icon was not imported");
    if (!check.loadingScreen.empty()) {
        expect(sceneContext.ImportBuildGameLoadingScreenImage(check.loadingScreen) &&
                !sceneContext.ProjectConfiguration().display.splash.image.empty(), "the loading screen picture was not imported");
    }
    if (!check.androidFormat.empty()) {
        static_cast<void>(sceneContext.SetBuildGameAndroidFormat(check.androidFormat));
        expect(sceneContext.BuildGameSettings().For(check.target).androidFormat == check.androidFormat, "the Android format was not chosen");
    }
    if (!check.uploadKey.empty()) {
        expect(sceneContext.CreateBuildGameUploadKey(check.uploadKey) &&
                sceneContext.BuildGameSettings().For(check.target).androidKeystore == check.uploadKey,
            "the upload key was not created and selected");
    }
    if (!check.buildRoot.empty()) static_cast<void>(sceneContext.SetBuildGameBuildRoot(check.buildRoot));
    if (!check.emsdk.empty()) static_cast<void>(sceneContext.SetBuildGameToolchainDirectory(BuildGameField::EmsdkRoot, check.emsdk));
    if (!check.outputDirectory.empty()) static_cast<void>(sceneContext.SetBuildGameOutputDirectory(check.outputDirectory));

    const BuildGameValidation validation = BuildGamePanelModel::Validate(check.target, sceneContext.ProjectConfiguration(),
        sceneContext.BuildGameSettings(), check.release, false, sceneContext.HasBuildGameStorePassword(),
        sceneContext.HasBuildGameKeyPassword(), sceneContext.HasBuildGameSecret(BuildGameField::WindowsCertificatePassword));
    expect(validation.canBuild, "the panel does not allow the build: " + validation.reason);
    expect(BuildGamePanelRenderer::HitTestBuildButton(content, Middle(rects.buildButton).x, Middle(rects.buildButton).y),
        "the build button cannot be clicked");

    if (check.build && validation.canBuild && failures.empty()) {
        if (expect(sceneContext.StartBuildGamePackage(EditorDirtySceneResolution::Save), "the build did not start")) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(check.timeoutMinutes);
            EditorPackageSnapshot snapshot = sceneContext.BuildGamePackageSnapshot();
            while (snapshot.state == EditorPackageJobState::Running && std::chrono::steady_clock::now() < deadline) {
                editor.Pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                snapshot = sceneContext.BuildGamePackageSnapshot();
            }
            if (snapshot.state == EditorPackageJobState::Running) {
                sceneContext.CancelBuildGamePackage();
                expect(false, "the build timed out");
            } else {
                std::string detail = snapshot.status;
                // The last lines the packager printed say what it made or why it stopped.
                const std::size_t shown = std::min<std::size_t>(snapshot.diagnostics.size(), 12U);
                for (std::size_t index = snapshot.diagnostics.size() - shown; index < snapshot.diagnostics.size(); ++index) {
                    detail += " | " + snapshot.diagnostics[index].message;
                }
                log += "build: " + detail + "; ";
                expect(snapshot.state == EditorPackageJobState::Succeeded, "the build failed: " + detail);
            }
        }
    }
    editor.Destroy();
    return { failures.empty(), failures.empty() ? log : failures + log };
}

} // namespace kb::editor
#endif
