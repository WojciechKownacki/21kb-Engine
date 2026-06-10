#include "app/EditorSelfTest.hpp"

#if defined(_WIN32)
#include "app/project_settings/EditorProjectSettingsPointerController.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/ProjectSettingsPanelLayout.hpp"
#include "rendering/ProjectSettingsPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
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
// Each suite gets its own leaf directory so they cannot cross-contaminate each
// other's assets.
[[nodiscard]] std::filesystem::path PrepareScratchProjectDir(const std::string& leaf) {
    std::error_code error;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(error) / "21kb_selftest" / leaf;
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

    // Hover tracking over the open list (drives the highlight).
    report.Check(controller.UpdateHover(kContent, click.optionRow1.x, click.optionRow1.y), "Hovering an option updates hover state");
    report.Check(context.ProjectSettings().HoveredOption() == 1, "Hovered option index tracked");
    report.Check(controller.UpdateHoverOrClear(std::optional<RECT>{ kContent }, click.elsewhere.x, click.elsewhere.y), "Moving off the list clears hover");
    report.Check(context.ProjectSettings().HoveredOption() == -1, "Hover cleared off the list");

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

// Finds the first registered asset matching a predicate, or an invalid id.
template <typename Predicate>
[[nodiscard]] kb::assets::AssetId FindAssetId(const EditorSceneContext& context, Predicate predicate) {
    for (const kb::assets::AssetMetadata& metadata : context.Scene().Assets().Manager().Registry().All()) {
        if (predicate(metadata)) {
            return metadata.id;
        }
    }
    return kb::assets::AssetId{};
}

[[nodiscard]] float ActorX(const EditorSceneContext& context, kb::scene::SceneEntity actor, float fallback) {
    const kb::scene::TransformComponent* transform = context.Scene().Transforms().TryGet(actor);
    return transform != nullptr ? transform->localPosition.x : fallback;
}

// Proves the full gameplay loop with real engine subsystems and the editor's
// play-mode wiring: device key -> Input action -> Lua behaviour -> entity moves.
void RunGameplayLoopSuite(Report& report) {
    EditorSceneContext context;

    // Author an Input Action "Move" (Axis1D so the value is continuous while held).
    report.Check(context.CreateInputActionAsset("/Game"), "Create Input Action asset");
    const kb::assets::AssetId moveAction = FindAssetId(context, [](const kb::assets::AssetMetadata& m) { return m.type == "InputAction"; });
    report.Check(moveAction.IsValid(), "Input Action asset registered");
    report.Check(context.SetInputActionName(moveAction, "Move"), "Name the action 'Move'");
    report.Check(context.CycleInputActionValueType(moveAction), "Set action value type to Axis1D (Bool -> Axis1D)");

    // Author a Mapping Context binding W -> Move (scale +1).
    report.Check(context.CreateInputMappingContextAsset("/Game"), "Create Mapping Context asset");
    const kb::assets::AssetId mappingContext = FindAssetId(context, [](const kb::assets::AssetMetadata& m) { return m.type == "InputMappingContext"; });
    report.Check(mappingContext.IsValid(), "Mapping Context asset registered");
    report.Check(context.AddInputMapping(mappingContext), "Add a mapping row");
    report.Check(context.SetInputMappingKey(mappingContext, 0, kb::input::InputKey::W), "Bind mapping key to W");
    report.Check(context.CycleInputMappingAction(mappingContext, 0), "Point mapping at the Move action");
    report.Check(context.SetInputMappingScale(mappingContext, 0, 1.0F), "Set mapping scale +1");

    // Author a Lua behaviour that translates the entity along +X by the Move value.
    const std::filesystem::path luaPath = EditorProjectPaths::AssetsRoot() / "MoveBehaviour.lua";
    {
        std::error_code error;
        std::filesystem::create_directories(luaPath.parent_path(), error);
        std::ofstream lua(luaPath, std::ios::binary | std::ios::trunc);
        lua << "function Tick(self, dt)\n"
               "    local v = CallFunction(\"GetActionValue\", { action = \"Move\" })\n"
               "    if v ~= 0 then\n"
               "        local x = self:GetProperty(\"Transform\", \"localPosition.x\")\n"
               "        self:SetProperty(\"Transform\", \"localPosition.x\", x + v * 5.0 * dt)\n"
               "    end\n"
               "end\n";
    }
    report.Check(std::filesystem::exists(luaPath), "Write Move behaviour .lua to project assets");
    static_cast<void>(context.Scene().Assets().Discover());
    const kb::assets::AssetId behaviourAsset = FindAssetId(context, [](const kb::assets::AssetMetadata& m) { return m.virtualPath.filename() == "MoveBehaviour.lua"; });
    report.Check(behaviourAsset.IsValid(), "Lua behaviour asset discovered");

    // Spawn an actor and attach the behaviour.
    const kb::scene::SceneEntity actor = context.CreateHierarchyObject();
    report.Check(actor.IsValid(), "Create actor entity");
    report.Check(context.AddBehaviourAssetToEntity(behaviourAsset, actor), "Attach behaviour to actor");

    // Make the mapping context the project's active input.
    const std::vector<std::string> options = context.ProjectInputMappingContextOptions();
    report.Check(options.size() == 2U, "Project input options = (None) + 1 context");
    if (options.size() == 2U) {
        report.Check(context.SetProjectInputMappingContext(options[1]), "Set project mapping context");
    }

    // Enter play mode: installs the script runtime and activates project input.
    report.Check(context.BeginPlayModeSceneSession(), "Begin play mode session");
    report.Check(context.Scene().Transforms().TryGet(actor) != nullptr, "Actor has a Transform");
    const float startX = ActorX(context, actor, 0.0F);

    // Hold W and tick: device -> "Move" action -> Lua -> +X.
    context.Scene().Input().MutableDeviceState().SetKeyDown(kb::input::InputKey::W, true);
    for (int frame = 0; frame < 12; ++frame) {
        static_cast<void>(context.Scene().Runtime().Update(0.016F));
    }
    const float movedX = ActorX(context, actor, startX);
    report.Check(movedX > startX + 0.05F, "Holding W moved the actor along +X (" + std::to_string(startX) + " -> " + std::to_string(movedX) + ")");

    // Release W: the actor should stop (no drift).
    context.Scene().Input().MutableDeviceState().SetKeyDown(kb::input::InputKey::W, false);
    for (int frame = 0; frame < 5; ++frame) {
        static_cast<void>(context.Scene().Runtime().Update(0.016F));
    }
    const float settledX = ActorX(context, actor, movedX);
    report.Check(std::abs(settledX - movedX) < 0.01F, "Releasing W stops the actor (no drift)");
}

// Runs one suite in its own freshly-created scratch project (cwd-based bootstrap),
// then restores the previous working directory.
void RunSuiteInScratch(Report& report, const std::string& leaf, void (*suite)(Report&)) {
    const std::filesystem::path scratch = PrepareScratchProjectDir(leaf);
    const std::filesystem::path previous = std::filesystem::current_path();
    std::error_code error;
    std::filesystem::current_path(scratch, error);
    if (error) {
        report.Check(false, "Enter isolated scratch project directory for " + leaf);
        return;
    }
    suite(report);
    std::filesystem::current_path(previous, error);
}

void WriteReport(const std::filesystem::path& reportPath, const Report& report) {
    std::error_code error;
    std::filesystem::create_directories(reportPath.parent_path(), error);
    std::ofstream out(reportPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << "21kb editor headless self-test\n";
    out << "Suites: Project Settings (inputs panel) + Gameplay loop (input -> script -> movement)\n";
    out << "================================================\n";
    for (const std::string& line : report.Lines()) {
        out << line << '\n';
    }
    out << "================================================\n";
    out << "RESULT: " << (report.Ok() ? "PASS" : "FAIL") << '\n';
}

} // namespace

int EditorSelfTest::Run(const std::filesystem::path& reportPath) {
    Report report;
    RunSuiteInScratch(report, "project_settings", &RunProjectSettingsSuite);
    RunSuiteInScratch(report, "gameplay", &RunGameplayLoopSuite);
    WriteReport(reportPath, report);
    return report.Ok() ? 0 : 1;
}

} // namespace kb::editor

#endif
