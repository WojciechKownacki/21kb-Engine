#include "app/EditorSelfTest.hpp"

#if defined(_WIN32)
#include "app/project_settings/EditorProjectSettingsPointerController.hpp"
#include "rendering/ProjectSettingsPanelLayout.hpp"
#include "rendering/ProjectSettingsPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include "engine/project/ProjectManager.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace kb::editor {
namespace {

// Accumulates pass/fail lines so the whole suite runs even after a failure and
// the agent gets a complete picture in one report.
class Report {
public:
    void Check(bool condition, const std::string& label) {
        lines_.push_back((condition ? "[PASS] " : "[FAIL] ") + label);
        ok_ = ok_ && condition;
    }

    void Note(const std::string& line) { lines_.push_back("       " + line); }

    [[nodiscard]] bool Ok() const noexcept { return ok_; }
    [[nodiscard]] const std::vector<std::string>& Lines() const noexcept { return lines_; }

private:
    std::vector<std::string> lines_;
    bool ok_ = true;
};

// Isolated scratch project so the self-test never touches the user's project.
[[nodiscard]] std::filesystem::path PrepareScratchProjectDir() {
    std::error_code error;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(error) / "21kb_selftest" / "project";
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

// A wide content rect so the right content pane has realistic room (matches the
// docked panel). Clicks are computed from the real layout (below), not hardcoded,
// so the test follows any future geometry change automatically.
constexpr RECT kContent{ 0, 0, 900, 560 };

[[nodiscard]] POINT Center(const RECT& rect) noexcept {
    return POINT{ (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 };
}

// Centers of each interactive control, derived from the shared panel layout.
struct ProjectSettingsClickPoints {
    POINT category{};   // First sidebar category ("Inputs").
    POINT field{};      // Mapping Context selector box.
    POINT optionRow1{}; // First named option inside the open dropdown.
    POINT checkbox{};   // Enabled checkbox.
    POINT elsewhere{};  // Empty space in the right content pane.
};

[[nodiscard]] ProjectSettingsClickPoints ResolveClickPoints() noexcept {
    const ProjectSettingsPanelLayoutRects rects = ProjectSettingsPanelLayout::Resolve(kContent);
    const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
    return ProjectSettingsClickPoints{
        .category = Center(ProjectSettingsPanelLayout::CategoryRow(rects.sidebar, 0)),
        .field = Center(fieldBox),
        .optionRow1 = Center(ProjectSettingsPanelLayout::OptionRow(fieldBox, 1)),
        .checkbox = Center(rects.enabledCheckbox),
        .elsewhere = POINT{ Center(rects.content).x, rects.content.bottom - 40 },
    };
}

[[nodiscard]] ProjectSettingsHitKind HitKindAt(const EditorSceneContext& context, POINT point) {
    return ProjectSettingsPanelRenderer::HitTest(kContent, context, point.x, point.y).kind;
}

void RunProjectSettingsSuite(Report& report) {
    EditorSceneContext context;

    // Real authoring: create two Input Mapping Context assets the same way the
    // user does in the editor (write asset -> discover -> registry).
    report.Check(context.CreateInputMappingContextAsset("/Game"), "Create first Input Mapping Context asset");
    report.Check(context.CreateInputMappingContextAsset("/Game"), "Create second Input Mapping Context asset");

    std::vector<std::string> options = context.ProjectInputMappingContextOptions();
    report.Check(options.size() == 3U, "Mapping Context options = (None) + 2 created (got " + std::to_string(options.size()) + ")");
    report.Check(!options.empty() && options.front().empty(), "First option is the empty (None) entry");
    report.Check(std::is_sorted(options.begin() + (options.empty() ? 0 : 1), options.end()), "Named options are sorted");
    if (options.size() < 2U) {
        report.Note("Aborting interaction checks: not enough options were created.");
        return;
    }

    const ProjectSettingsClickPoints click = ResolveClickPoints();
    EditorProjectSettingsPointerController controller{ context };

    // Left sidebar (master) lists categories; Inputs is selected by default.
    report.Check(HitKindAt(context, click.category) == ProjectSettingsHitKind::CategoryItem, "Sidebar point hit-tests as CategoryItem");
    report.Check(context.ProjectSettings().SelectedCategory() == static_cast<int>(ProjectSettingsCategory::Inputs), "Inputs category selected by default");
    report.Check(controller.HandlePointerDown(kContent, click.category.x, click.category.y), "Clicking the Inputs category is handled");
    report.Check(context.ProjectSettings().SelectedCategory() == static_cast<int>(ProjectSettingsCategory::Inputs), "Inputs category active after click");

    // Dropdown starts closed.
    report.Check(!context.ProjectSettings().IsMappingContextDropdownOpen(), "Dropdown starts closed");

    // Click the selector field -> dropdown opens.
    report.Check(HitKindAt(context, click.field) == ProjectSettingsHitKind::MappingContextField, "Field point hit-tests as MappingContextField");
    report.Check(controller.HandlePointerDown(kContent, click.field.x, click.field.y), "Clicking field is handled");
    report.Check(context.ProjectSettings().IsMappingContextDropdownOpen(), "Dropdown opened after field click");

    // Click option row 1 (first named context) -> selects + closes + persists.
    report.Check(HitKindAt(context, click.optionRow1) == ProjectSettingsHitKind::MappingContextOption, "Open-list row hit-tests as MappingContextOption");
    const std::string expected = options[1];
    report.Check(controller.HandlePointerDown(kContent, click.optionRow1.x, click.optionRow1.y), "Clicking option is handled");
    report.Check(context.Project().inputMappingContext == expected, "Selected mapping context applied to project (" + expected + ")");
    report.Check(!context.ProjectSettings().IsMappingContextDropdownOpen(), "Dropdown closed after selection");

    // Persistence: reload the descriptor from disk.
    {
        const kb::project::ProjectDescriptorReadResult reloaded = kb::project::ProjectManager::LoadProject(context.ProjectFile());
        report.Check(reloaded.succeeded, "Project descriptor reloads from disk");
        report.Check(reloaded.succeeded && reloaded.descriptor.inputMappingContext == expected, "Mapping context persisted to descriptor");
        report.Check(reloaded.succeeded && reloaded.descriptor.fileVersion >= 2U, "Descriptor written at file version >= 2");
    }

    // Enabled checkbox toggles + persists.
    const bool enabledBefore = context.Project().inputEnabled;
    report.Check(HitKindAt(context, click.checkbox) == ProjectSettingsHitKind::EnabledCheckbox, "Checkbox point hit-tests as EnabledCheckbox");
    report.Check(controller.HandlePointerDown(kContent, click.checkbox.x, click.checkbox.y), "Clicking checkbox is handled");
    report.Check(context.Project().inputEnabled == !enabledBefore, "Enabled flag toggled");
    {
        const kb::project::ProjectDescriptorReadResult reloaded = kb::project::ProjectManager::LoadProject(context.ProjectFile());
        report.Check(reloaded.succeeded && reloaded.descriptor.inputEnabled == !enabledBefore, "Enabled flag persisted to descriptor");
    }

    // Reopen the dropdown, then click empty panel space -> dismisses.
    report.Check(controller.HandlePointerDown(kContent, click.field.x, click.field.y), "Reopen dropdown via field click");
    report.Check(context.ProjectSettings().IsMappingContextDropdownOpen(), "Dropdown reopened");
    report.Check(HitKindAt(context, click.elsewhere) == ProjectSettingsHitKind::None, "Empty panel point hit-tests as None");
    report.Check(controller.HandlePointerDown(kContent, click.elsewhere.x, click.elsewhere.y), "Clicking empty space dismisses dropdown");
    report.Check(!context.ProjectSettings().IsMappingContextDropdownOpen(), "Dropdown closed after clicking empty space");
}

void WriteReport(const std::filesystem::path& reportPath, const Report& report) {
    std::error_code error;
    std::filesystem::create_directories(reportPath.parent_path(), error);
    std::ofstream out(reportPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << "21kb editor headless self-test\n";
    out << "Project Settings: Inputs (mapping context dropdown + enabled)\n";
    out << "================================================\n";
    for (const std::string& line : report.Lines()) {
        out << line << '\n';
    }
    out << "================================================\n";
    out << "RESULT: " << (report.Ok() ? "PASS" : "FAIL") << '\n';
}

} // namespace

int EditorSelfTest::Run(const std::filesystem::path& reportPath) {
    const std::filesystem::path scratch = PrepareScratchProjectDir();
    const std::filesystem::path previous = std::filesystem::current_path();
    std::error_code error;
    std::filesystem::current_path(scratch, error);

    Report report;
    if (error) {
        report.Check(false, "Enter isolated scratch project directory");
    } else {
        RunProjectSettingsSuite(report);
    }

    std::filesystem::current_path(previous, error);
    WriteReport(reportPath, report);
    return report.Ok() ? 0 : 1;
}

} // namespace kb::editor

#endif
