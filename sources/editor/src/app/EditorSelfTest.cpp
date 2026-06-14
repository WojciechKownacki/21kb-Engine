#include "app/EditorSelfTest.hpp"

#if defined(_WIN32)
#include "app/plugins/EditorPluginsPointerController.hpp"
#include "app/project_settings/EditorProjectSettingsPointerController.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/PluginsPanelRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/ProjectSettingsPanelLayout.hpp"
#include "rendering/ProjectSettingsPanelRenderer.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "scene/EditorPluginCatalog.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorScriptAssetGateway.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
    POINT inputCategory{};    // First sidebar category ("Inputs").
    POINT graphicsCategory{}; // Second sidebar category ("Graphics").
    POINT field{};      // Mapping Context selector box.
    POINT optionRow1{}; // First named option inside the open dropdown.
    POINT checkbox{};   // Enabled checkbox.
    POINT vulkanBackend{};
    POINT postProcessToggle{};
    POINT fxaaMode{};
    POINT msaaMode{};
    POINT bloomToggle{};
    POINT msaa4x{};
    POINT elsewhere{};  // Empty space in the right content pane.
};

[[nodiscard]] ProjectSettingsClickPoints ResolveClickPoints() noexcept {
    const ProjectSettingsPanelLayoutRects rects = ProjectSettingsPanelLayout::Resolve(kContent);
    const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
    return ProjectSettingsClickPoints{
        .inputCategory = Center(ProjectSettingsPanelLayout::CategoryRow(rects.sidebar, 0)),
        .graphicsCategory = Center(ProjectSettingsPanelLayout::CategoryRow(rects.sidebar, 1)),
        .field = Center(fieldBox),
        .optionRow1 = Center(ProjectSettingsPanelLayout::OptionRow(fieldBox, 1)),
        .checkbox = Center(rects.enabledCheckbox),
        .vulkanBackend = Center(rects.backendVulkanButton),
        .postProcessToggle = Center(rects.postProcessCheckbox),
        .fxaaMode = Center(rects.antiAliasingFxaaButton),
        .msaaMode = Center(rects.antiAliasingMsaaButton),
        .bloomToggle = Center(rects.bloomCheckbox),
        .msaa4x = Center(rects.msaa4xButton),
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
    report.Check(HitKindAt(context, click.inputCategory) == ProjectSettingsHitKind::CategoryItem, "Sidebar point hit-tests as CategoryItem");
    report.Check(context.ProjectSettings().SelectedCategory() == static_cast<int>(ProjectSettingsCategory::Inputs), "Inputs category selected by default");
    report.Check(controller.HandlePointerDown(kContent, click.inputCategory.x, click.inputCategory.y), "Clicking the Inputs category is handled");
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

    EditorRenderBackendSettings renderBackendSettings;
    report.Check(controller.HandlePointerDown(kContent, click.graphicsCategory.x, click.graphicsCategory.y), "Clicking the Graphics category is handled");
    report.Check(context.ProjectSettings().SelectedCategory() == static_cast<int>(ProjectSettingsCategory::Graphics), "Graphics category active after click");
    report.Check(HitKindAt(context, click.vulkanBackend) == ProjectSettingsHitKind::RenderBackendOption, "Vulkan backend point hit-tests as RenderBackendOption");
    const std::uint64_t generationBefore = renderBackendSettings.Generation();
    report.Check(controller.HandlePointerDown(kContent, click.vulkanBackend.x, click.vulkanBackend.y, renderBackendSettings), "Clicking Vulkan backend is handled");
    report.Check(renderBackendSettings.Backend() == EditorRenderBackend::Vulkan, "Graphics backend setting changed to Vulkan");
    report.Check(renderBackendSettings.Generation() == generationBefore + 1U, "Graphics backend generation increments");
    report.Check(HitKindAt(context, click.postProcessToggle) == ProjectSettingsHitKind::GraphicsToggle, "Post FX point hit-tests as GraphicsToggle");
    const std::uint64_t toggleGenerationBefore = renderBackendSettings.Generation();
    const std::uint64_t backendGenerationBeforeToggle = renderBackendSettings.BackendGeneration();
    report.Check(controller.HandlePointerDown(kContent, click.postProcessToggle.x, click.postProcessToggle.y, renderBackendSettings), "Clicking Post FX toggle is handled");
    report.Check(!renderBackendSettings.PostProcessEnabled(), "Post FX setting toggled off");
    report.Check(renderBackendSettings.Generation() == toggleGenerationBefore + 1U, "Graphics toggle generation increments");
    report.Check(renderBackendSettings.BackendGeneration() == backendGenerationBeforeToggle, "Graphics quality toggle does not restart the backend");

    report.Check(HitKindAt(context, click.fxaaMode) == ProjectSettingsHitKind::AntiAliasingMode, "FXAA point hit-tests as AntiAliasingMode");
    const std::uint64_t backendGenerationBeforeFxaa = renderBackendSettings.BackendGeneration();
    report.Check(controller.HandlePointerDown(kContent, click.fxaaMode.x, click.fxaaMode.y, renderBackendSettings), "Clicking FXAA mode is handled");
    report.Check(renderBackendSettings.AntiAliasingMode() == EditorAntiAliasingMode::Fxaa, "Anti-aliasing mode changed to FXAA");
    report.Check(renderBackendSettings.FxaaEnabled(), "FXAA setting is active");
    report.Check(!renderBackendSettings.TemporalAntiAliasingEnabled(), "TAA setting is inactive when FXAA is selected");
    report.Check(renderBackendSettings.MsaaSamples() == 0U, "MSAA samples are disabled when FXAA is selected");
    report.Check(renderBackendSettings.BackendGeneration() == backendGenerationBeforeFxaa, "FXAA does not restart the backend");

    report.Check(HitKindAt(context, click.bloomToggle) == ProjectSettingsHitKind::GraphicsToggle, "Bloom point hit-tests as GraphicsToggle");
    report.Check(controller.HandlePointerDown(kContent, click.bloomToggle.x, click.bloomToggle.y, renderBackendSettings), "Clicking Bloom toggle is handled");
    report.Check(!renderBackendSettings.BloomEnabled(), "Bloom setting toggled off");

    report.Check(HitKindAt(context, click.msaaMode) == ProjectSettingsHitKind::AntiAliasingMode, "MSAA mode point hit-tests as AntiAliasingMode");
    report.Check(controller.HandlePointerDown(kContent, click.msaaMode.x, click.msaaMode.y, renderBackendSettings), "Clicking MSAA mode is handled");
    report.Check(renderBackendSettings.AntiAliasingMode() == EditorAntiAliasingMode::Msaa, "Anti-aliasing mode changed to MSAA");
    report.Check(HitKindAt(context, click.msaa4x) == ProjectSettingsHitKind::MsaaOption, "MSAA 4x point hit-tests as MsaaOption");
    const std::uint64_t backendGenerationBeforeMsaa = renderBackendSettings.BackendGeneration();
    report.Check(controller.HandlePointerDown(kContent, click.msaa4x.x, click.msaa4x.y, renderBackendSettings), "Clicking MSAA 4x is handled");
    report.Check(renderBackendSettings.MsaaSamples() == 4U, "MSAA setting changed to 4x");
    report.Check(renderBackendSettings.BackendGeneration() == backendGenerationBeforeMsaa + 1U, "MSAA change restarts the backend");
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

// Proves the Lua script editor model + file gateway: create a .lua asset, open
// it, and round-trip an edit on disk (what the editable control persists).
void RunScriptEditorSuite(Report& report) {
    EditorSceneContext context;

    report.Check(context.CreateLuaScriptAsset("/Game"), "Create Lua script asset");
    const kb::assets::AssetId script = FindAssetId(context, [](const kb::assets::AssetMetadata& m) { return m.type == "LuaScript"; });
    report.Check(script.IsValid(), "New asset registered with type LuaScript");

    report.Check(context.OpenLuaScript(script), "Open the script in the editor");
    report.Check(context.ScriptEditor().IsOpen(), "Script editor reports an open file");
    const std::filesystem::path path = context.ScriptEditor().FilePath();
    report.Check(std::filesystem::exists(path), "Open script file exists on disk");
    report.Check(context.ScriptEditor().Title().ends_with(".lua"), "Editor title is the .lua file name");
    report.Check(EditorScriptAssetGateway::ReadSource(path).find("function Tick") != std::string::npos, "New script ships a Tick template");

    const std::uint64_t generationBefore = context.ScriptEditor().Generation();
    report.Check(context.OpenLuaScript(script), "Re-open the script");
    report.Check(context.ScriptEditor().Generation() != generationBefore, "Re-opening bumps the reload generation");

    const std::string edited = "function Tick(self, dt) end\n";
    report.Check(EditorScriptAssetGateway::WriteSource(path, edited), "Save edited source to disk");
    report.Check(EditorScriptAssetGateway::ReadSource(path) == edited, "Edited source round-trips on disk");
}

// Proves the Unity-style Inspector script attach flow at the model level:
// scripts attach as components, toggle, and remove through the component path.
void RunScriptAttachSuite(Report& report) {
    EditorSceneContext context;

    report.Check(context.CreateLuaScriptAsset("/Game"), "Create Lua script asset");
    const kb::assets::AssetId script = FindAssetId(context, [](const kb::assets::AssetMetadata& m) { return m.type == "LuaScript"; });
    report.Check(script.IsValid(), "Lua script registered");

    const kb::scene::SceneEntity actor = context.CreateHierarchyObject();
    report.Check(actor.IsValid(), "Create actor entity");
    report.Check(!context.HasEntityScript(actor), "Actor has no script initially");

    report.Check(context.AttachScriptToEntity(actor, script), "Attach script as component");
    report.Check(context.HasEntityScript(actor), "Actor now has a script");
    report.Check(!context.EntityScriptName(actor).empty(), "Attached script name resolves");
    report.Check(context.EntityScriptEnabled(actor), "Script is enabled by default");

    report.Check(context.ToggleEntityScriptEnabled(actor), "Toggle script enabled");
    report.Check(!context.EntityScriptEnabled(actor), "Script is now disabled");

    report.Check(context.RemoveScriptFromEntity(actor), "Remove script component");
    report.Check(!context.HasEntityScript(actor), "Actor has no script after removal");
}

void RunSelectionTransformSuite(Report& report) {
    EditorSceneContext context;

    const kb::scene::SceneEntity first = context.CreateHierarchyObject();
    const kb::scene::SceneEntity second = context.CreateHierarchyObject();
    report.Check(first.IsValid() && second.IsValid(), "Create two hierarchy entities for multi-selection transform");

    kb::scene::TransformComponent firstTransform = context.Scene().Transforms().Get(first);
    firstTransform.localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F };
    context.Scene().Transforms().Set(first, firstTransform);

    kb::scene::TransformComponent secondTransform = context.Scene().Transforms().Get(second);
    secondTransform.localPosition = kb::scene::Vec3{ 6.0F, 0.0F, 0.0F };
    context.Scene().Transforms().Set(second, secondTransform);

    const std::array<kb::scene::SceneEntity, 2> selected{ first, second };
    context.SelectHierarchyEntities(selected);

    const InspectorPanelRenderer::Hit pivotXHit = InspectorPanelRenderer::HitTest(kContent, context, 360, 216);
    report.Check(
        pivotXHit.kind == InspectorHitKind::FloatField &&
            pivotXHit.section == InspectorSectionId::Transform &&
            pivotXHit.property == InspectorPropertyId::PositionX,
        "Multi-selection inspector pivot X field hit-tests as transform PositionX");

    report.Check(context.BeginSelectedTransformEdit("Edit Transform"), "Begin multi-selection transform edit");
    report.Check(std::abs(context.ActiveTransformEditPropertyStart(InspectorPropertyId::PositionX) - 4.0F) < 0.001F, "Multi-selection position edit starts from pivot X");
    report.Check(context.ApplyActiveTransformEditProperty(InspectorPropertyId::PositionX, 10.0F), "Apply multi-selection pivot X edit");
    report.Check(context.CommitActiveTransformEdit(), "Commit multi-selection transform edit");

    kb::scene::TransformComponent movedFirst = context.Scene().Transforms().Get(first);
    kb::scene::TransformComponent movedSecond = context.Scene().Transforms().Get(second);
    report.Check(std::abs(movedFirst.localPosition.x - 8.0F) < 0.001F, "Pivot edit moves first entity by shared delta");
    report.Check(std::abs(movedSecond.localPosition.x - 12.0F) < 0.001F, "Pivot edit moves second entity by shared delta");

    report.Check(context.UndoSceneCommand(), "Undo multi-selection transform edit");
    movedFirst = context.Scene().Transforms().Get(first);
    movedSecond = context.Scene().Transforms().Get(second);
    report.Check(std::abs(movedFirst.localPosition.x - 2.0F) < 0.001F, "Undo restores first entity transform");
    report.Check(std::abs(movedSecond.localPosition.x - 6.0F) < 0.001F, "Undo restores second entity transform");

    report.Check(context.RedoSceneCommand(), "Redo multi-selection transform edit");
    movedFirst = context.Scene().Transforms().Get(first);
    movedSecond = context.Scene().Transforms().Get(second);
    report.Check(std::abs(movedFirst.localPosition.x - 8.0F) < 0.001F, "Redo reapplies first entity transform");
    report.Check(std::abs(movedSecond.localPosition.x - 12.0F) < 0.001F, "Redo reapplies second entity transform");
}

void RunPrefabPlacementSuite(Report& report) {
    EditorSceneContext context;

    const kb::scene::SceneEntity source = context.CreateHierarchyObject();
    report.Check(source.IsValid(), "Create source entity for prefab placement");
    context.Scene().Entities().SetName(source, "PlacedPrefabSource");
    kb::scene::TransformComponent sourceTransform = context.Scene().Transforms().Get(source);
    sourceTransform.localPosition = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F };
    context.Scene().Transforms().Set(source, sourceTransform);

    const std::filesystem::path prefabPath = EditorProjectPaths::PrefabsRoot() / "PlacedPrefab.kbprefab";
    report.Check(context.CreatePrefabAsset(source, prefabPath), "Create prefab asset from source entity");
    const auto sourcePrefabRow = std::ranges::find_if(context.HierarchyRows(), [source](const EditorHierarchyRow& row) {
        return row.entity == source;
    });
    report.Check(sourcePrefabRow != context.HierarchyRows().end() && sourcePrefabRow->prefabRoot, "Source entity becomes a visible prefab root after prefab asset creation");
    report.Check(context.InstantiatePrefabAssetAt(prefabPath, "/Game/Prefabs/PlacedPrefab.kbprefab", kb::scene::Vec3{ 7.0F, 0.5F, -3.0F }), "Instantiate prefab asset at scene position");

    const kb::scene::SceneEntity placed = context.SelectedEntity();
    report.Check(placed.IsValid() && context.Scene().Entities().IsAlive(placed), "Placed prefab root becomes the selected entity");
    const kb::scene::TransformComponent placedTransform = context.Scene().Transforms().Get(placed);
    report.Check(std::abs(placedTransform.localPosition.x - 7.0F) < 0.001F, "Placed prefab x position matches drop position");
    report.Check(std::abs(placedTransform.localPosition.y - 0.5F) < 0.001F, "Placed prefab y position matches drop position");
    report.Check(std::abs(placedTransform.localPosition.z + 3.0F) < 0.001F, "Placed prefab z position matches drop position");

    report.Check(context.UndoSceneCommand(), "Undo prefab placement");
    report.Check(!context.Scene().Entities().IsAlive(placed), "Undo removes placed prefab root");
    report.Check(context.RedoSceneCommand(), "Redo prefab placement");
    const kb::scene::SceneEntity replaced = context.SelectedEntity();
    report.Check(replaced.IsValid() && context.Scene().Entities().IsAlive(replaced), "Redo selects recreated prefab root");
    const kb::scene::TransformComponent replacedTransform = context.Scene().Transforms().Get(replaced);
    report.Check(std::abs(replacedTransform.localPosition.x - 7.0F) < 0.001F, "Redo restores prefab x position");
    report.Check(std::abs(replacedTransform.localPosition.y - 0.5F) < 0.001F, "Redo restores prefab y position");
    report.Check(std::abs(replacedTransform.localPosition.z + 3.0F) < 0.001F, "Redo restores prefab z position");

    const kb::scene::SceneEntity parent = context.CreateHierarchyObject();
    report.Check(parent.IsValid() && context.Scene().Entities().IsAlive(parent), "Create parent for prefab instantiation");
    context.Scene().Entities().SetName(parent, "PrefabParent");
    report.Check(context.InstantiatePrefabAsset(prefabPath, "/Game/Prefabs/PlacedPrefab.kbprefab", parent), "Instantiate prefab asset under parent");

    const kb::scene::SceneEntity child = context.SelectedEntity();
    report.Check(child.IsValid() && context.Scene().Entities().IsAlive(child), "Parented prefab root becomes the selected entity");
    report.Check(context.Scene().Hierarchy().Parent(child) == parent, "Parented prefab root is attached to requested parent");

    report.Check(context.UndoSceneCommand(), "Undo parented prefab instantiation");
    report.Check(!context.Scene().Entities().IsAlive(child), "Undo removes parented prefab root");
    report.Check(context.Scene().Entities().IsAlive(parent), "Undo keeps prefab parent alive");
    report.Check(context.RedoSceneCommand(), "Redo parented prefab instantiation");
    const kb::scene::SceneEntity reparented = context.SelectedEntity();
    report.Check(reparented.IsValid() && context.Scene().Entities().IsAlive(reparented), "Redo selects recreated parented prefab root");
    report.Check(context.Scene().Hierarchy().Parent(reparented) == parent, "Redo restores parented prefab root parent");
}

void RunHierarchyCommandSuite(Report& report) {
    EditorSceneContext context;
    const auto findHierarchyEntityNamed = [&context](std::string_view name) {
        for (const EditorHierarchyRow& row : context.HierarchyRows()) {
            if (row.name == name) {
                return row.entity;
            }
        }
        return kb::scene::SceneEntity{};
    };
    const auto countHierarchyEntitiesNamed = [&context](std::string_view name) {
        std::size_t count = 0U;
        for (const EditorHierarchyRow& row : context.HierarchyRows()) {
            if (row.name == name) {
                ++count;
            }
        }
        return count;
    };

    const kb::scene::SceneEntity entity = context.CreateHierarchyObject();
    report.Check(entity.IsValid() && context.Scene().Entities().IsAlive(entity), "Create entity for hierarchy rename");
    context.SelectEntity(entity);
    const std::string originalName = context.Scene().Entities().Name(entity);

    report.Check(context.BeginHierarchyRename(), "Begin hierarchy rename");
    report.Check(context.IsHierarchyRenaming(entity), "Hierarchy rename targets selected entity");
    report.Check(context.IsHierarchyRenameSelectingAll(), "Hierarchy rename starts with text selected");
    context.SetHierarchyRenameText("RenamedEntity");
    report.Check(context.CommitHierarchyRename(), "Commit hierarchy rename");
    report.Check(context.Scene().Entities().Name(entity) == "RenamedEntity", "Hierarchy rename updates entity name");

    report.Check(context.UndoSceneCommand(), "Undo hierarchy rename");
    report.Check(findHierarchyEntityNamed(originalName).IsValid(), "Undo restores previous hierarchy name");
    report.Check(context.RedoSceneCommand(), "Redo hierarchy rename");
    const kb::scene::SceneEntity redone = findHierarchyEntityNamed("RenamedEntity");
    report.Check(redone.IsValid() && context.Scene().Entities().IsAlive(redone), "Redo reapplies hierarchy name");

    context.SelectEntity(redone);
    report.Check(context.BeginHierarchyRename(), "Begin hierarchy rename with empty value");
    context.SetHierarchyRenameText({});
    report.Check(context.CommitHierarchyRename(), "Commit empty hierarchy rename");
    report.Check(context.Scene().Entities().Name(context.SelectedEntity()) == "Entity", "Empty hierarchy rename falls back to default name");

    const kb::scene::SceneEntity renamed = findHierarchyEntityNamed("Entity");
    context.SelectEntity(renamed);
    const std::size_t beforeDuplicateCount = countHierarchyEntitiesNamed("Entity");
    report.Check(context.DuplicateSelectedHierarchyEntities(), "Duplicate selected hierarchy entity");
    report.Check(countHierarchyEntitiesNamed("Entity") == beforeDuplicateCount + 1U, "Duplicate adds a hierarchy entity copy");
    report.Check(context.UndoSceneCommand(), "Undo hierarchy duplicate");
    report.Check(countHierarchyEntitiesNamed("Entity") == beforeDuplicateCount, "Undo removes hierarchy duplicate");
    report.Check(context.RedoSceneCommand(), "Redo hierarchy duplicate");
    report.Check(countHierarchyEntitiesNamed("Entity") == beforeDuplicateCount + 1U, "Redo restores hierarchy duplicate");

    report.Check(context.DeleteSelectedHierarchyEntity(), "Delete selected hierarchy entity");
    report.Check(countHierarchyEntitiesNamed("Entity") == beforeDuplicateCount, "Delete removes selected hierarchy entity");
    report.Check(context.UndoSceneCommand(), "Undo hierarchy delete");
    report.Check(countHierarchyEntitiesNamed("Entity") == beforeDuplicateCount + 1U, "Undo restores deleted hierarchy entity");
    report.Check(context.RedoSceneCommand(), "Redo hierarchy delete");
    report.Check(countHierarchyEntitiesNamed("Entity") == beforeDuplicateCount, "Redo removes deleted hierarchy entity again");
}

// Proves Log("...") from a Lua script reaches the editor Console during play.
void RunScriptLogSuite(Report& report) {
    EditorSceneContext context;

    const std::filesystem::path luaPath = EditorProjectPaths::AssetsRoot() / "LogBehaviour.lua";
    {
        std::error_code error;
        std::filesystem::create_directories(luaPath.parent_path(), error);
        std::ofstream lua(luaPath, std::ios::binary | std::ios::trunc);
        lua << "local logged = false\n"
               "function Tick(self, dt)\n"
               "    if not logged then\n"
               "        logged = true\n"
               "        Log(\"PING_FROM_LUA\")\n"
               "    end\n"
               "end\n";
    }
    static_cast<void>(context.Scene().Assets().Discover());
    const kb::assets::AssetId behaviour = FindAssetId(context, [](const kb::assets::AssetMetadata& m) { return m.virtualPath.filename() == "LogBehaviour.lua"; });
    report.Check(behaviour.IsValid(), "Log behaviour script discovered");

    const kb::scene::SceneEntity actor = context.CreateHierarchyObject();
    report.Check(context.AttachScriptToEntity(actor, behaviour), "Attach logging script to actor");
    report.Check(context.BeginPlayModeSceneSession(), "Begin play mode session");
    report.Check(!context.SaveCurrentScene(), "Scene save is blocked during play mode");
    for (int frame = 0; frame < 4; ++frame) {
        static_cast<void>(context.Scene().Runtime().Update(0.016F));
    }
    report.Check(context.RestorePlayModeSceneSession(), "Restore play mode session after first script log");
    report.Check(context.BeginPlayModeSceneSession(), "Begin second play mode session");
    for (int frame = 0; frame < 4; ++frame) {
        static_cast<void>(context.Scene().Runtime().Update(0.016F));
    }

    int loggedCount = 0;
    for (const EditorConsoleEntry& entry : context.Console().Entries()) {
        if (entry.message.find("PING_FROM_LUA") != std::string::npos) {
            ++loggedCount;
        }
    }
    report.Check(loggedCount >= 2, "Lua script local state resets across play mode sessions");
}

void RunPluginsPanelSuite(Report& report) {
    EditorSceneContext context;

    report.Check(EditorPluginCatalog::Count() > 0U, "Plugin catalog has at least one plugin");
    const EditorPluginDescriptor* descriptor = EditorPluginCatalog::At(0);
    if (descriptor == nullptr) {
        report.Note("Aborting plugin panel checks: catalog entry 0 is missing.");
        return;
    }

    constexpr POINT togglePoint{ kContent.left + 30, kContent.top + 87 };
    const PluginsPanelRenderer::Hit toggleHit = PluginsPanelRenderer::HitTest(kContent, context, togglePoint.x, togglePoint.y);
    report.Check(toggleHit.kind == PluginsPanelHitKind::Toggle, "First plugin checkbox hit-tests as Toggle");
    report.Check(toggleHit.index == 0U, "First plugin checkbox resolves catalog index 0");

    EditorPluginsPointerController controller{ context };
    report.Check(controller.UpdateHoverOrClear(std::optional<RECT>{ kContent }, togglePoint.x, togglePoint.y), "Hovering first plugin updates hover state");
    report.Check(context.Plugins().HoveredPluginIndex() == 0U, "Hovered plugin index tracked");
    report.Check(!context.Plugins().HasPendingReload(), "Plugin panel starts without pending reload");

    if (context.IsProjectPluginEnabled(descriptor->id)) {
        report.Check(controller.HandlePointerDown(kContent, togglePoint.x, togglePoint.y), "Clicking initially enabled plugin checkbox is handled");
        report.Check(!context.IsProjectPluginEnabled(descriptor->id), "Clicking initially enabled plugin disables it");
        report.Check(context.Plugins().HasPendingReload(), "Disabling initially enabled plugin marks pending reload");
        report.Check(context.ReloadSceneFromProject(), "Reload scene with disabled plugin settings");
        report.Check(!context.Plugins().HasPendingReload(), "Reloading disabled plugin settings clears pending reload");
    }

    report.Check(!context.IsProjectPluginEnabled(descriptor->id), "Plugin can be disabled before enable checks");
    report.Check(controller.HandlePointerDown(kContent, togglePoint.x, togglePoint.y), "Clicking plugin checkbox is handled");
    report.Check(context.IsProjectPluginEnabled(descriptor->id), "Clicking checkbox enables the plugin");
    report.Check(context.ProjectPluginBinaryPath(descriptor->id) == descriptor->binaryPath, "Enabled plugin stores the catalog binary path");
    report.Check(context.Plugins().HasPendingReload(), "Enabling plugin marks pending reload");

    {
        const kb::project::ProjectDescriptorReadResult reloaded = kb::project::ProjectManager::LoadProject(context.ProjectFile());
        report.Check(reloaded.succeeded, "Project descriptor reloads after enabling plugin");
        const auto iter = std::find_if(reloaded.descriptor.plugins.begin(), reloaded.descriptor.plugins.end(), [descriptor](const kb::project::ProjectPluginReference& plugin) {
            return plugin.name == descriptor->id;
        });
        report.Check(iter != reloaded.descriptor.plugins.end(), "Enabled plugin reference persisted to descriptor");
        report.Check(iter != reloaded.descriptor.plugins.end() && iter->enabled, "Persisted plugin reference is enabled");
        report.Check(iter != reloaded.descriptor.plugins.end() && iter->binaryPath == descriptor->binaryPath, "Persisted plugin reference keeps binary path");
    }

    report.Check(context.ReloadSceneFromProject(), "Reload scene with enabled plugin settings");
    report.Check(!context.Plugins().HasPendingReload(), "Reloading scene clears pending reload");
    report.Check(context.IsProjectPluginEnabled(descriptor->id), "Plugin remains enabled after scene reload");

    const kb::scene::SceneEntity floor = context.CreateHierarchyObject();
    context.Scene().Transforms().Set(floor, kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ 0.0F, -0.5F, 0.0F },
    });
    context.Scene().Components().Rigidbodies().Set(floor, kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Static,
    });
    context.Scene().Components().Colliders().Set(floor, kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 10.0F, 1.0F, 10.0F },
    });

    const kb::scene::SceneEntity box = context.CreateHierarchyObject();
    context.Scene().Transforms().Set(box, kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ 0.0F, 4.0F, 0.0F },
    });
    context.Scene().Components().Rigidbodies().Set(box, kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
        .mass = 1.0F,
    });
    context.Scene().Components().Colliders().Set(box, kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
    });
    for (int frame = 0; frame < 120; ++frame) {
        static_cast<void>(context.Scene().Runtime().Update(1.0F / 60.0F));
    }
    const kb::scene::TransformComponent boxTransform = context.Scene().Transforms().Get(box);
    report.Check(boxTransform.localPosition.y < 4.0F, "Jolt plugin loaded through editor reload moves a dynamic body");
    report.Check(boxTransform.localPosition.y > 0.35F, "Jolt plugin loaded through editor reload keeps the body above the floor");

    report.Check(controller.HandlePointerDown(kContent, togglePoint.x, togglePoint.y), "Clicking plugin checkbox again is handled");
    report.Check(!context.IsProjectPluginEnabled(descriptor->id), "Second click disables the plugin");
    report.Check(context.Plugins().HasPendingReload(), "Disabling plugin keeps pending reload marked");
    {
        const kb::project::ProjectDescriptorReadResult reloaded = kb::project::ProjectManager::LoadProject(context.ProjectFile());
        const auto iter = std::find_if(reloaded.descriptor.plugins.begin(), reloaded.descriptor.plugins.end(), [descriptor](const kb::project::ProjectPluginReference& plugin) {
            return plugin.name == descriptor->id;
        });
        report.Check(reloaded.succeeded && iter != reloaded.descriptor.plugins.end() && !iter->enabled, "Disabled plugin state persisted to descriptor");
    }
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
    out << "Suites: Project Settings + Plugins + Gameplay loop + Script editor/attach/log + Hierarchy commands + Selection transform + Prefab placement\n";
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
    RunSuiteInScratch(report, "script_editor", &RunScriptEditorSuite);
    RunSuiteInScratch(report, "script_attach", &RunScriptAttachSuite);
    RunSuiteInScratch(report, "hierarchy_commands", &RunHierarchyCommandSuite);
    RunSuiteInScratch(report, "selection_transform", &RunSelectionTransformSuite);
    RunSuiteInScratch(report, "prefab_placement", &RunPrefabPlacementSuite);
    RunSuiteInScratch(report, "script_log", &RunScriptLogSuite);
    RunSuiteInScratch(report, "plugins", &RunPluginsPanelSuite);
    WriteReport(reportPath, report);
    return report.Ok() ? 0 : 1;
}

} // namespace kb::editor

#endif
