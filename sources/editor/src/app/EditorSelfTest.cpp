#include "app/EditorSelfTest.hpp"

#if defined(_WIN32)
#include "app/plugins/EditorPluginsPointerController.hpp"
#include "app/project_settings/EditorProjectSettingsPointerController.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/PluginsPanelRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "inspection/InspectorPanelInteraction.hpp"
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
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
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

[[nodiscard]] kb::render::RenderMaterialGraphLink MakeSelfTestGraphLink(
    kb::render::RenderMaterialGraphNodeKind fromKind,
    std::uint32_t fromNodeId,
    std::string fromPin,
    kb::render::RenderMaterialGraphNodeKind toKind,
    std::uint32_t toNodeId,
    std::string toPin) {
    kb::render::RenderMaterialGraphLink link{
        .fromNodeId = fromNodeId,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(fromKind, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNodeId,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(toKind, toPin, false),
        .toPin = std::move(toPin),
    };
    link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
    return link;
}

// Centers of each interactive control, derived from the shared panel layout.
struct ProjectSettingsClickPoints {
    POINT inputCategory{};    // First sidebar category ("Inputs").
    POINT graphicsCategory{}; // Second sidebar category ("Graphics").
    POINT field{};      // Mapping Context selector box.
    POINT optionRow1{}; // First named option inside the open dropdown.
    POINT checkbox{};   // Enabled checkbox.
    POINT vulkanBackend{};
    POINT forwardPlusLightingPath{};
    POINT deferredLightingPath{};
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
        .forwardPlusLightingPath = Center(rects.lightingPathForwardPlusButton),
        .deferredLightingPath = Center(rects.lightingPathDeferredButton),
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
    report.Check(HitKindAt(context, click.forwardPlusLightingPath) == ProjectSettingsHitKind::LightingPathOption, "Forward+ lighting path point hit-tests as LightingPathOption");
    report.Check(controller.HandlePointerDown(kContent, click.forwardPlusLightingPath.x, click.forwardPlusLightingPath.y, renderBackendSettings), "Clicking Forward+ lighting path is handled");
    report.Check(context.Project().sceneLightingPath == kb::project::ProjectSceneLightingPath::ForwardPlus, "Project lighting path changed to Forward+");
    {
        const kb::project::ProjectDescriptorReadResult reloaded = kb::project::ProjectManager::LoadProject(context.ProjectFile());
        report.Check(reloaded.succeeded && reloaded.descriptor.sceneLightingPath == kb::project::ProjectSceneLightingPath::ForwardPlus, "Forward+ lighting path persisted to descriptor");
        report.Check(reloaded.succeeded && reloaded.descriptor.fileVersion >= 4U, "Descriptor written at file version >= 4 after Forward+");
    }
    report.Check(HitKindAt(context, click.deferredLightingPath) == ProjectSettingsHitKind::LightingPathOption, "Deferred lighting path point hit-tests as LightingPathOption");
    report.Check(controller.HandlePointerDown(kContent, click.deferredLightingPath.x, click.deferredLightingPath.y, renderBackendSettings), "Clicking Deferred lighting path is handled");
    report.Check(context.Project().sceneLightingPath == kb::project::ProjectSceneLightingPath::Deferred, "Project lighting path changed to Deferred");
    {
        const kb::project::ProjectDescriptorReadResult reloaded = kb::project::ProjectManager::LoadProject(context.ProjectFile());
        report.Check(reloaded.succeeded && reloaded.descriptor.sceneLightingPath == kb::project::ProjectSceneLightingPath::Deferred, "Deferred lighting path persisted to descriptor");
        report.Check(reloaded.succeeded && reloaded.descriptor.fileVersion >= 4U, "Descriptor written at file version >= 4");
    }
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

// Proves the Inspector script attach flow at the model level:
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

void RunInspectorMaterialDropTargetSuite(Report& report) {
    EditorSceneContext context;
    kb::scene::SceneEntity mesh = context.CreateHierarchyObject();
    report.Check(mesh.IsValid(), "Create mesh entity for inspector material drop target");
    kb::scene::MeshRendererComponent renderer{ .meshAssetId = 909U };
    renderer.materialSlotOverrideCount = 2U;
    context.Scene().Components().MeshRenderers().Set(mesh, renderer);
    context.SelectEntity(mesh);

    InspectorPanelRenderer::Hit materialHit{};
    InspectorPanelRenderer::Hit materialPickerHit{};
    InspectorPanelRenderer::Hit overridePickerHit{};
    InspectorPanelRenderer::Hit slotHit{};
    for (int y = kContent.top; y < kContent.bottom; ++y) {
        for (int x = kContent.left; x < kContent.right; ++x) {
            const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(kContent, context, x, y);
            if (hit.section != InspectorSectionId::MeshRenderer) {
                continue;
            }
            if (materialHit.kind == InspectorHitKind::None && hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::MeshRendererMaterial) {
                materialHit = hit;
            }
            if (materialPickerHit.kind == InspectorHitKind::None && hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::MeshRendererMaterialPicker) {
                materialPickerHit = hit;
            }
            if (slotHit.kind == InspectorHitKind::None && hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::MeshRendererMaterialSlot0) {
                slotHit = hit;
            }
            if (overridePickerHit.kind == InspectorHitKind::None &&
                hit.kind == InspectorHitKind::TextField &&
                (hit.property == InspectorPropertyId::MeshRendererMaterialOverridePicker || hit.property == InspectorPropertyId::MeshRendererMaterialSlotPicker0)) {
                overridePickerHit = hit;
            }
        }
    }
    report.Check(materialHit.kind != InspectorHitKind::None, "Inspector Mesh Renderer Material row hit-tests as the main material assignment target");
    report.Check(materialPickerHit.kind != InspectorHitKind::None, "Inspector Mesh Renderer Material row exposes a material picker button");
    report.Check(slotHit.kind != InspectorHitKind::None, "Inspector Mesh Renderer material override row hit-tests as a concrete material slot target");
    report.Check(overridePickerHit.kind != InspectorHitKind::None, "Inspector Mesh Renderer Material Override row exposes a material picker button");

    auto refreshMeshEntityFromSelection = [&]() {
        const kb::scene::SceneEntity selected = context.SelectedEntity();
        if (selected.IsValid() && context.Scene().Components().MeshRenderers().Has(selected)) {
            mesh = selected;
        }
    };

    const kb::assets::AssetId materialId{ 31337U };
    report.Check(context.Scene().Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                     .id = materialId,
                     .type = "RenderMaterial",
                     .name = "DropSlotMaterial",
                     .virtualPath = "/Game/Materials/DropSlotMaterial.kbmat",
                     .runtimeLoadable = true,
                 }),
        "Register material asset for inspector slot assignment");
    report.Check(InspectorPanelInteraction::HandlePointerDown(context, slotHit, 360, 240), "Clicking an empty Mesh Renderer material override is handled");
    const kb::scene::MeshRendererComponent* emptySlotClicked = context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(
        emptySlotClicked != nullptr &&
            emptySlotClicked->materialSlotOverrideCount == 2U &&
            emptySlotClicked->materialSlotAssetIds[1] == 0U,
        "Clicking an empty Mesh Renderer material override keeps it as None");

    context.AcknowledgeSceneRenderSubmitted();
    report.Check(!context.SceneRenderFullDirty(), "Scene render dirty acknowledgement clears full sync before material assignment");
    report.Check(context.SetMeshRendererMaterialAsset(mesh, materialId), "Assign Mesh Renderer main material through command path");
    report.Check(context.SceneRenderFullDirty(), "Assigning Mesh Renderer main material marks scene rendering for resync");
    const kb::scene::MeshRendererComponent* mainAssigned = context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(mainAssigned != nullptr && mainAssigned->materialAssetId == materialId.value, "Mesh Renderer main material assignment stores the material asset id");
    context.AcknowledgeSceneRenderSubmitted();
    report.Check(context.UndoSceneCommand(), "Undo Mesh Renderer main material assignment");
    refreshMeshEntityFromSelection();
    report.Check(context.SceneRenderFullDirty(), "Undoing Mesh Renderer main material assignment marks scene rendering for resync");
    const kb::scene::MeshRendererComponent* mainUndone = context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(mainUndone != nullptr && mainUndone->materialAssetId == 0U, "Undo restores the previous Mesh Renderer main material");
    context.AcknowledgeSceneRenderSubmitted();
    report.Check(context.RedoSceneCommand(), "Redo Mesh Renderer main material assignment");
    refreshMeshEntityFromSelection();
    report.Check(context.SceneRenderFullDirty(), "Redoing Mesh Renderer main material assignment marks scene rendering for resync");
    const kb::scene::MeshRendererComponent* mainRedone = context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(mainRedone != nullptr && mainRedone->materialAssetId == materialId.value, "Redo reapplies the Mesh Renderer main material");

    context.AcknowledgeSceneRenderSubmitted();
    report.Check(context.SetMeshRendererMaterialSlotAsset(mesh, 1U, materialId), "Assign Mesh Renderer material slot through command path");
    report.Check(context.SceneRenderFullDirty(), "Assigning Mesh Renderer material slot marks scene rendering for resync");
    const kb::scene::MeshRendererComponent* assigned = context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(
        assigned != nullptr &&
            assigned->materialSlotOverrideCount == 2U &&
            assigned->materialSlotAssetIds[1] == materialId.value,
        "Mesh Renderer material slot assignment stores the dropped material asset id");
    report.Check(context.UndoSceneCommand(), "Undo Mesh Renderer material slot assignment");
    refreshMeshEntityFromSelection();
    const kb::scene::MeshRendererComponent* slotUndone = context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(
        slotUndone != nullptr &&
            slotUndone->materialSlotOverrideCount == 0U &&
            slotUndone->materialSlotAssetIds[1] == 0U,
        "Undo restores the cleared Mesh Renderer material slot state");
    report.Check(context.RedoSceneCommand(), "Redo Mesh Renderer material slot assignment");
    refreshMeshEntityFromSelection();
    const kb::scene::MeshRendererComponent* slotRedone = context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(
        slotRedone != nullptr &&
            slotRedone->materialSlotOverrideCount == 2U &&
            slotRedone->materialSlotAssetIds[1] == materialId.value,
        "Redo reapplies the Mesh Renderer material slot override");
    report.Check(InspectorPanelInteraction::HandlePointerDown(context, slotHit, 360, 240), "Clicking an assigned Mesh Renderer material override is handled");
    const kb::scene::MeshRendererComponent* slotInspectedByClick = context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(
        slotInspectedByClick != nullptr &&
            slotInspectedByClick->materialSlotOverrideCount == 2U &&
            slotInspectedByClick->materialSlotAssetIds[1] == materialId.value,
        "Clicking an assigned Mesh Renderer material override keeps the assignment for inspection");
    report.Check(context.SetMeshRendererMaterialSlotAsset(mesh, 1U, {}), "Clear Mesh Renderer material slot through command path");
    const kb::scene::MeshRendererComponent* slotClearedByCommand = context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(
        slotClearedByCommand != nullptr &&
            slotClearedByCommand->materialSlotOverrideCount == 0U &&
            slotClearedByCommand->materialSlotAssetIds[1] == 0U,
        "Clear Mesh Renderer material slot removes the override");
    report.Check(context.SetMeshRendererMaterialSlotAsset(mesh, 1U, materialId), "Reassign Mesh Renderer material slot before rejection test");

    const kb::assets::AssetId wrongTypeId{ 31338U };
    report.Check(context.Scene().Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                     .id = wrongTypeId,
                     .type = "RenderTexture",
                     .name = "WrongTypeTexture",
                     .virtualPath = "/Game/Textures/WrongTypeTexture.ktx",
                     .runtimeLoadable = true,
                 }),
        "Register wrong-type asset for inspector material slot rejection");
    const std::size_t consoleCountBeforeWrongDrop = context.Console().Entries().size();
    report.Check(!context.SetMeshRendererMaterialSlotAsset(mesh, 1U, wrongTypeId), "Reject wrong-type asset for Mesh Renderer material slot");
    const kb::scene::MeshRendererComponent* rejected = context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(
        rejected != nullptr &&
            rejected->materialSlotOverrideCount == 2U &&
            rejected->materialSlotAssetIds[1] == materialId.value,
        "Rejected wrong-type material slot assignment leaves the existing override unchanged");
    const auto wrongTypeWarning = std::find_if(
        context.Console().Entries().begin() + static_cast<std::ptrdiff_t>(consoleCountBeforeWrongDrop),
        context.Console().Entries().end(),
        [](const EditorConsoleEntry& entry) {
            return entry.level == EditorConsoleLevel::Warning &&
                entry.category == "Inspector" &&
                entry.message.find("Only material assets can be assigned to a Mesh Renderer slot.") != std::string::npos;
        });
    report.Check(wrongTypeWarning != context.Console().Entries().end(), "Rejected wrong-type material slot assignment reports a warning");
}

void RunMaterialGraphContextMenuSuite(Report& report) {
    EditorSceneContext context;
    const kb::assets::AssetId materialId{ 0x57D00U };
    report.Check(context.Scene().Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                     .id = materialId,
                     .type = "RenderMaterial",
                     .name = "ContextMenuMaterial",
                     .virtualPath = "/Game/Materials/ContextMenuMaterial.kbmat",
                     .runtimeLoadable = true,
                 }),
        "Register material asset for graph context-menu authoring");

    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.shadingModel = "unlit";
    context.MaterialEditor().Open(materialId, material);

    report.Check(context.OpenMaterialGraphContextMenu(materialId, 320, 240, -160, 96), "Open material graph context menu in headless editor context");
    report.Check(MaterialEditorGraphContextMenuMaxScroll(context) == 0, "Collapsed material graph context menu does not require scrolling");
    report.Check(context.ToggleMaterialGraphContextMenuCategory(6U), "Expand Utility category in material graph context menu");
    const int utilityMenuMaxScroll = MaterialEditorGraphContextMenuMaxScroll(context);
    report.Check(utilityMenuMaxScroll > 0, "Expanded Utility node palette exposes a scroll range");
    const RECT utilityMenuRect = MaterialEditorPanelRenderer::GraphContextMenuRect(context);
    const RECT utilityViewport = MaterialEditorGraphContextMenuViewportRect(utilityMenuRect);
    const MaterialEditorGraphContextMenuHit topHitBeforeScroll =
        MaterialEditorPanelRenderer::GraphContextMenuHit(context, utilityMenuRect.left + 40, utilityViewport.top + 11);
    report.Check(
        topHitBeforeScroll.kind == MaterialEditorGraphContextMenuHitKind::Category && topHitBeforeScroll.categoryIndex == 0U,
        "Unscrolled material graph context menu hit-tests the first visible category");
    report.Check(context.ScrollMaterialGraphContextMenu(-120, utilityMenuMaxScroll), "Mouse wheel scrolls expanded material graph context menu");
    report.Check(context.MaterialGraphContextMenuScrollOffset() > 0, "Material graph context menu stores non-zero scroll offset");
    report.Check(context.SetMaterialGraphContextMenuScrollOffset(utilityMenuMaxScroll, utilityMenuMaxScroll), "Material graph context menu can scroll to the bottom");
    const MaterialEditorGraphContextMenuHit topHitAfterScroll =
        MaterialEditorPanelRenderer::GraphContextMenuHit(context, utilityMenuRect.left + 40, utilityViewport.top + 11);
    report.Check(
        !(topHitAfterScroll.kind == MaterialEditorGraphContextMenuHitKind::Category && topHitAfterScroll.categoryIndex == 0U),
        "Scrolled material graph context menu hit-test follows the visible rows instead of stale unscrolled rows");
    report.Check(context.SetMaterialGraphContextMenuScrollOffset(0, utilityMenuMaxScroll), "Reset material graph context menu scroll before command execution");
    context.SetMaterialGraphCanvasViewport(0, 0, 1000, 720);
    report.Check(context.OpenMaterialGraphContextMenu(materialId, 320, 500, -160, 96), "Open material graph context menu near the bottom of a large canvas");
    const int actualMenuHeight = MaterialEditorGraphContextMenuHeight(context);
    report.Check(
        context.MaterialGraphContextMenuY() == 720 - actualMenuHeight,
        "Material graph context menu clamps against its actual visible height instead of the maximum palette height");
    context.SetMaterialGraphCanvasViewport(100, 100, 260, 300);
    report.Check(context.OpenMaterialGraphContextMenu(materialId, 350, 390, -160, 96), "Open material graph context menu in a constrained canvas");
    const RECT constrainedMenuRect = MaterialEditorPanelRenderer::GraphContextMenuRect(context);
    report.Check(
        constrainedMenuRect.left >= 100 &&
            constrainedMenuRect.top >= 100 &&
            constrainedMenuRect.right <= 360 &&
            constrainedMenuRect.bottom <= 400,
        "Material graph context menu is clamped inside a small graph canvas");
    report.Check(context.MoveMaterialGraphContextMenuKeyboardSelection(1), "Keyboard Down selects the first material graph context-menu row");
    report.Check(context.ActivateMaterialGraphContextMenuKeyboardSelection(), "Keyboard Enter activates the selected material graph context-menu category");
    report.Check(context.IsMaterialGraphContextMenuCategoryExpanded(0U), "Keyboard Enter expands a selected material graph context-menu category");
    context.SetMaterialGraphContextMenuSearchQuery("pixel depth");
    report.Check(context.ActivateMaterialGraphContextMenuKeyboardSelection(), "Keyboard Enter creates the first matching searched material graph node");
    report.Check(context.OpenMaterialGraphContextMenu(materialId, 250, 250, -120, 128), "Reopen material graph context menu after keyboard command execution");
    report.Check(context.ExecuteMaterialGraphContextMenuCommand(MaterialEditorGraphMenuCommand::CreatePixelDepth),
        "Execute PixelDepth through material graph context-menu command path");
    const std::optional<kb::render::RenderMaterialAssetData>& workingCopy = context.MaterialEditor().WorkingCopy();
    const auto pixelDepth = workingCopy.has_value()
        ? std::find_if(
              workingCopy->graph.nodes.begin(),
              workingCopy->graph.nodes.end(),
              [](const kb::render::RenderMaterialGraphNode& node) {
                  return node.kind == kb::render::RenderMaterialGraphNodeKind::PixelDepth;
              })
        : std::vector<kb::render::RenderMaterialGraphNode>::const_iterator{};
    report.Check(workingCopy.has_value() && pixelDepth != workingCopy->graph.nodes.end(),
        "PixelDepth context-menu command leaves a real node in the material graph working copy");
}

[[nodiscard]] kb::render::RenderMaterialGraphDocument MakeMaterialGraphPanelHitTestGraph() {
    kb::render::RenderMaterialGraphDocument graph{};
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 120,
        .positionY = 120,
    });
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 1U,
        .kind = kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        .positionX = 680,
        .positionY = 132,
    });
    graph.links.push_back(MakeSelfTestGraphLink(
        kb::render::RenderMaterialGraphNodeKind::TextureSample,
        2U,
        "color",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));
    return graph;
}

void RunMaterialGraphPanelCanvasHitTestSuite(Report& report) {
    EditorSceneContext context;
    const kb::assets::AssetId materialId{ 0xCA11U };
    const RECT content{ 0, 0, 1200, 800 };
    const kb::render::RenderMaterialGraphDocument graph = MakeMaterialGraphPanelHitTestGraph();
    const float zoom = std::max(0.1F, context.MaterialGraphZoom());
    MaterialGraphCanvasDocumentBuildResult canvasResult =
        MaterialEditorPanelBuildInteractiveGraphCanvas(content, graph, context, materialId);

    const MaterialGraphCanvasPoint baseColorPin = canvasResult.canvas.PinCenterWindow(1U, 0U, false);
    const std::optional<MaterialEditorGraphPinHit> baseColor = MaterialEditorPanelRenderer::GraphPinAt(
        content,
        graph,
        context,
        materialId,
        static_cast<int>(std::lround(baseColorPin.x + (24.0F * zoom))),
        static_cast<int>(std::lround(baseColorPin.y)));
    report.Check(baseColor.has_value(), "Material panel graph hits an input pin from its row edge");
    report.Check(
        baseColor.has_value() &&
            baseColor->nodeId == 1U &&
            baseColor->pin == "baseColor" &&
            baseColor->direction == MaterialEditorGraphPinDirection::Input,
        "Material panel graph input hit preserves node, pin, and direction");
    report.Check(
        MaterialEditorPanelRenderer::GraphPinAt(
            content,
            graph,
            context,
            materialId,
            static_cast<int>(std::lround(baseColorPin.x + (160.0F * zoom))),
            static_cast<int>(std::lround(baseColorPin.y)))
             .has_value(),
        "Material panel graph hits a one-sided input across the row");

    const MaterialGraphCanvasPoint colorPin = canvasResult.canvas.PinCenterWindow(0U, 0U, true);
    const std::optional<MaterialEditorGraphPinHit> color = MaterialEditorPanelRenderer::GraphPinAt(
        content,
        graph,
        context,
        materialId,
        static_cast<int>(std::lround(colorPin.x - (70.0F * zoom))),
        static_cast<int>(std::lround(colorPin.y)));
    report.Check(color.has_value(), "Material panel graph hits a texture output from its side lane");
    report.Check(
        color.has_value() &&
            color->nodeId == 2U &&
            color->pin == "color" &&
            color->direction == MaterialEditorGraphPinDirection::Output,
        "Material panel graph output hit preserves node, pin, and direction");
    report.Check(
        !MaterialEditorPanelRenderer::GraphPinAt(
            content,
            graph,
            context,
            materialId,
            static_cast<int>(std::lround(colorPin.x - (210.0F * zoom))),
            static_cast<int>(std::lround(colorPin.y)))
             .has_value(),
        "Material panel graph texture preview center stays reserved for picker interaction");

    const std::optional<MaterialEditorGraphLinkHit> link = MaterialEditorPanelRenderer::GraphLinkAt(
        content,
        graph,
        context,
        materialId,
        static_cast<int>(std::lround((colorPin.x + baseColorPin.x) * 0.5F)),
        static_cast<int>(std::lround((colorPin.y + baseColorPin.y) * 0.5F)));
    report.Check(link.has_value(), "Material panel graph hits links through canvas bezier distance");
    report.Check(
        link.has_value() &&
            link->fromNodeId == 2U &&
            link->fromPin == "color" &&
            link->toNodeId == 1U &&
            link->toPin == "baseColor",
        "Material panel graph link hit preserves source and target endpoints");
}

void RunMaterialGraphColorWatcherSuite(Report& report) {
    EditorSceneContext context;
    const kb::assets::AssetId materialId{ 0xC010U };
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.shadingModel = "unlit";
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 40U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantVector,
        .positionX = -240,
        .positionY = 40,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "RGB",
            .defaultValueHint = "0.25 0.5 0.75",
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 41U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 160,
        .positionY = 40,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "RGBA",
            .defaultValueHint = "0.9 0.2 0.1 0.6",
            .hasRange = true,
            .rangeMin = 0.0F,
            .rangeMax = 1.0F,
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 42U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = -240,
        .positionY = 220,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "brandTint",
            .displayName = "Brand Tint",
            .defaultValueHint = "0.1 0.2 0.3 1",
            .overrideSupported = true,
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 43U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ColorRamp,
        .positionX = 160,
        .positionY = 240,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Color Ramp",
            .defaultValueHint = "0 0 0 0 1 1 0.8 0.2",
        },
    });
    material.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "brandTint",
        .type = kb::render::RenderMaterialParameterType::Color,
        .numbers = { 0.4F, 0.5F, 0.6F, 1.0F },
    });
    context.MaterialEditor().Open(materialId, material);

    const RECT content{ 0, 0, 960, 720 };
    const std::optional<RECT> rgbRect = MaterialEditorPanelRenderer::GraphNodeRect(content, material.graph, 40U, context, materialId);
    const std::optional<RECT> rgbaRect = MaterialEditorPanelRenderer::GraphNodeRect(content, material.graph, 41U, context, materialId);
    const std::optional<RECT> parameterRect = MaterialEditorPanelRenderer::GraphNodeRect(content, material.graph, 42U, context, materialId);
    const std::optional<RECT> rampRect = MaterialEditorPanelRenderer::GraphNodeRect(content, material.graph, 43U, context, materialId);
    report.Check(rgbRect.has_value() && rgbaRect.has_value() && parameterRect.has_value() && rampRect.has_value(),
        "Material graph color watcher test resolves all color node rects");
    if (!rgbRect.has_value() || !rgbaRect.has_value() || !parameterRect.has_value() || !rampRect.has_value()) {
        return;
    }

    const SIZE rgbSize = MaterialEditorPanelGraphNodeSize(kb::render::RenderMaterialGraphNodeKind::ConstantVector);
    const SIZE rgbaSize = MaterialEditorPanelGraphNodeSize(kb::render::RenderMaterialGraphNodeKind::ConstantColor);
    const SIZE parameterSize = MaterialEditorPanelGraphNodeSize(kb::render::RenderMaterialGraphNodeKind::ParameterColor);
    report.Check(rgbSize.cx == 320 && rgbSize.cy == 196, "RGB node uses the color watcher footprint");
    report.Check(rgbaSize.cx == 320 && rgbaSize.cy == 196, "RGBA node uses the color watcher footprint");
    report.Check(parameterSize.cx == rgbaSize.cx && parameterSize.cy == rgbaSize.cy, "Color parameter node shares the RGBA watcher layout");
    const std::vector<std::string> rgbOutputPins =
        MaterialEditorPanelOutputPins(kb::render::RenderMaterialGraphNodeKind::ConstantVector);
    report.Check(
        rgbOutputPins.size() == 4U &&
            rgbOutputPins[0] == "xyz" &&
            rgbOutputPins[1] == "r" &&
            rgbOutputPins[2] == "g" &&
            rgbOutputPins[3] == "b",
        "RGB node exposes RGB/R/G/B output pins");

    const RECT rgbSwatch = MaterialEditorPanelColorWatcherSwatchRect(*rgbRect, kb::render::RenderMaterialGraphNodeKind::ConstantVector);
    const int rgbSwatchX = (rgbSwatch.left + rgbSwatch.right) / 2;
    const int rgbSwatchY = (rgbSwatch.top + rgbSwatch.bottom) / 2;
    report.Check(
        !MaterialEditorPanelRenderer::GraphPinAt(content, material.graph, context, materialId, rgbSwatchX, rgbSwatchY).has_value(),
        "RGB node swatch does not start a material graph wire drag");
    const std::optional<MaterialEditorGraphColorWatcherHit> rgbHit =
        MaterialEditorPanelRenderer::GraphColorWatcherAt(content, material, context, materialId, rgbSwatchX, rgbSwatchY);
    report.Check(rgbHit.has_value() &&
            rgbHit->target == MaterialEditorGraphColorWatcherTarget::ConstantRgb &&
            rgbHit->value.numbers[0] > 0.24F &&
            !rgbHit->applyImmediately,
        "RGB node swatch hit-test opens the color watcher picker with parsed RGB values");

    const RECT rgbaSwatch = MaterialEditorPanelColorWatcherSwatchRect(*rgbaRect, kb::render::RenderMaterialGraphNodeKind::ConstantColor);
    const int rgbaSwatchX = (rgbaSwatch.left + rgbaSwatch.right) / 2;
    const int rgbaSwatchY = (rgbaSwatch.top + rgbaSwatch.bottom) / 2;
    report.Check(
        !MaterialEditorPanelRenderer::GraphPinAt(content, material.graph, context, materialId, rgbaSwatchX, rgbaSwatchY).has_value(),
        "RGBA node swatch does not start a material graph wire drag");
    const RECT rgbaAlphaChannel = MaterialEditorPanelColorWatcherChannelRect(*rgbaRect, kb::render::RenderMaterialGraphNodeKind::ConstantColor, 3U, 4U);
    report.Check(MaterialEditorPanelRectWidth(rgbaAlphaChannel) >= 36, "RGBA watcher keeps the alpha channel legible at default graph zoom");
    const std::optional<MaterialEditorGraphColorWatcherHit> rgbaHit =
        MaterialEditorPanelRenderer::GraphColorWatcherAt(content, material, context, materialId, rgbaSwatchX, rgbaSwatchY);
    report.Check(rgbaHit.has_value() &&
            rgbaHit->target == MaterialEditorGraphColorWatcherTarget::ConstantColor &&
            !rgbaHit->applyImmediately &&
            rgbaHit->value.numbers[0] > 0.89F &&
            rgbaHit->value.numbers[3] > 0.59F,
        "RGBA node swatch opens the color picker without hidden palette hit zones");

    const RECT parameterSwatch = MaterialEditorPanelColorWatcherSwatchRect(*parameterRect, kb::render::RenderMaterialGraphNodeKind::ParameterColor);
    const std::optional<MaterialEditorGraphColorWatcherHit> parameterHit =
        MaterialEditorPanelRenderer::GraphColorWatcherAt(content, material, context, materialId, parameterSwatch.left + 3, parameterSwatch.top + 3);
    report.Check(parameterHit.has_value() &&
            parameterHit->target == MaterialEditorGraphColorWatcherTarget::ParameterColor &&
            parameterHit->stableId == "brandTint" &&
            parameterHit->value.numbers[0] > 0.39F &&
            parameterHit->value.numbers[2] > 0.59F,
        "ParameterColor watcher reads the graph parameter override as its single source of displayed truth");

    const RECT rgbGreen = MaterialEditorPanelColorWatcherChannelRect(*rgbRect, kb::render::RenderMaterialGraphNodeKind::ConstantVector, 1U, 3U);
    const std::optional<MaterialEditorGraphConstantValueHit> rgbChannel =
        MaterialEditorPanelRenderer::GraphConstantValueAt(content, material.graph, context, materialId, rgbGreen.left + 2, rgbGreen.top + 2);
    report.Check(rgbChannel.has_value() &&
            rgbChannel->nodeId == 40U &&
            rgbChannel->componentIndex == 1U &&
            rgbChannel->type == kb::render::RenderMaterialParameterType::Vec3,
        "RGB watcher channel fields still hit-test as editable constant components");

    const RECT rampGradient = MaterialEditorPanelColorRampGradientRect(*rampRect);
    const std::optional<MaterialEditorGraphColorWatcherHit> rampHit =
        MaterialEditorPanelRenderer::GraphColorWatcherAt(content, material, context, materialId, rampGradient.left + 2, rampGradient.top + 2);
    report.Check(rampHit.has_value() &&
            rampHit->target == MaterialEditorGraphColorWatcherTarget::ColorRampStop &&
            rampHit->propertyId == "colorRamp.stop0.color",
        "ColorRamp watcher exposes gradient stop color editing from the graph node");
}

void RunMaterialGraphTextureNodeSuite(Report& report) {
    EditorSceneContext context;
    const kb::assets::AssetId materialId{ 0x7E570U };
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    struct TextureNodeCase {
        std::uint32_t id;
        kb::render::RenderMaterialGraphNodeKind kind;
        int x;
        int y;
        const char* stableId;
        const char* displayName;
    };
    const std::array<TextureNodeCase, 4U> sampleNodes{ {
        { 50U, kb::render::RenderMaterialGraphNodeKind::TextureSample, 80, 120, "", "Texture Sample" },
        { 52U, kb::render::RenderMaterialGraphNodeKind::TextureSampleCube, 560, 120, "", "Texture Sample Cube" },
        { 53U, kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume, 80, 390, "", "Texture Sample Volume" },
        { 54U, kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray, 560, 390, "", "Texture Sample Array" },
    } };
    const std::array<TextureNodeCase, 5U> textureValueNodes{ {
        { 51U, kb::render::RenderMaterialGraphNodeKind::ParameterTexture, 80, 700, "albedo", "Albedo Texture" },
        { 55U, kb::render::RenderMaterialGraphNodeKind::TextureObject, 390, 700, "", "Texture Object" },
        { 56U, kb::render::RenderMaterialGraphNodeKind::TextureObjectCube, 700, 700, "", "Texture Object Cube" },
        { 57U, kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume, 80, 900, "", "Texture Object Volume" },
        { 58U, kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray, 390, 900, "", "Texture Object Array" },
    } };
    for (const TextureNodeCase& nodeCase : sampleNodes) {
        material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
            .id = nodeCase.id,
            .kind = nodeCase.kind,
            .positionX = nodeCase.x,
            .positionY = nodeCase.y,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = nodeCase.stableId,
                .displayName = nodeCase.displayName,
            },
        });
    }
    for (const TextureNodeCase& nodeCase : textureValueNodes) {
        material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
            .id = nodeCase.id,
            .kind = nodeCase.kind,
            .positionX = nodeCase.x,
            .positionY = nodeCase.y,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = nodeCase.stableId,
                .displayName = nodeCase.displayName,
            },
        });
    }
    std::vector<kb::assets::AssetId> pickerTextureIds;
    bool pickerTexturesRegistered = true;
    const std::filesystem::path pickerTextureRoot =
        std::filesystem::temp_directory_path() / "21kb_selftest" / "material_graph_picker_textures";
    std::error_code pickerDirectoryError;
    std::filesystem::create_directories(pickerTextureRoot, pickerDirectoryError);
    report.Check(!pickerDirectoryError, "Create structural texture metadata fixtures for texture picker self-test");
    for (std::uint32_t index = 0U; index < 20U; ++index) {
        const kb::assets::AssetId textureId{ 0x7E580U + index };
        const std::string suffix = (index < 10U ? "0" : "") + std::to_string(index);
        const std::string name = "PickerTexture" + suffix;
        const std::filesystem::path texturePath = pickerTextureRoot / (name + ".kbtex");
        {
            std::ofstream texture{ texturePath, std::ios::binary | std::ios::trunc };
            texture << "size 1 1\nsemantic baseColor\ncolorSpace srgb\nrgba8 255 255 255 255\n";
            pickerTexturesRegistered = static_cast<bool>(texture) && pickerTexturesRegistered;
        }
        pickerTexturesRegistered = context.Scene().Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
            .id = textureId,
            .type = "RenderTexture",
            .name = name,
            .virtualPath = "/Game/Textures/" + name + ".kbtex",
            .physicalPath = texturePath,
            .runtimeLoadable = true,
        }) && pickerTexturesRegistered;
        pickerTextureIds.push_back(textureId);
    }
    report.Check(pickerTexturesRegistered, "Register texture picker self-test texture assets");
    context.MaterialEditor().Open(materialId, material);

    const RECT content{ 0, 0, 1280, 1180 };
    report.Check(context.OpenMaterialGraphTexturePicker(materialId, 50U, kb::assets::AssetId{}), "Texture graph picker opens inside Material Editor state");
    report.Check(
        context.IsMaterialGraphTexturePickerOpen() &&
            context.MaterialGraphTexturePickerAssetId() == materialId &&
            context.MaterialGraphTexturePickerNodeId() == 50U,
        "Texture graph picker remembers the edited material and node");
    const MaterialEditorGraphTexturePickerLayout pickerLayout =
        MaterialEditorPanelRenderer::ResolveGraphTexturePickerLayout(content, pickerTextureIds.size() + 1U);
    const RECT narrowContent{ 0, 0, 420, 360 };
    const MaterialEditorGraphTexturePickerLayout narrowPickerLayout =
        MaterialEditorPanelRenderer::ResolveGraphTexturePickerLayout(narrowContent, pickerTextureIds.size() + 1U);
    report.Check(
        narrowPickerLayout.picker.left >= narrowContent.left &&
            narrowPickerLayout.picker.top >= narrowContent.top &&
            narrowPickerLayout.picker.right <= narrowContent.right &&
            narrowPickerLayout.picker.bottom <= narrowContent.bottom &&
            narrowPickerLayout.viewport.top >= narrowPickerLayout.search.bottom &&
            narrowPickerLayout.viewport.top >= narrowPickerLayout.accept.bottom &&
            narrowPickerLayout.columns >= 1 &&
            narrowPickerLayout.acceptEnabled,
        "P1.34 texture picker reflows inside a narrow editor panel with active Accept/Clear controls");
    const MaterialEditorGraphTexturePickerHit searchHit =
        MaterialEditorPanelRenderer::GraphTexturePickerHit(content, context, Center(pickerLayout.search).x, Center(pickerLayout.search).y);
    report.Check(searchHit.kind == MaterialEditorGraphTexturePickerHitKind::Search, "Texture graph picker search field owns its hit-test");
    context.AppendMaterialGraphTexturePickerSearchText(L'P');
    context.AppendMaterialGraphTexturePickerSearchText(L'i');
    context.AppendMaterialGraphTexturePickerSearchText(L'c');
    report.Check(context.MaterialGraphTexturePickerSearchQuery() == "Pic", "Texture graph picker search accepts text input");
    context.BackspaceMaterialGraphTexturePickerSearch();
    report.Check(context.MaterialGraphTexturePickerSearchQuery() == "Pi", "Texture graph picker search handles backspace");
    context.ClearMaterialGraphTexturePickerSearch();
    const MaterialEditorGraphTexturePickerHit clearHit = MaterialEditorPanelRenderer::GraphTexturePickerHit(
        content,
        context,
        Center(pickerLayout.itemRects[0]).x,
        Center(pickerLayout.itemRects[0]).y);
    report.Check(
        clearHit.kind == MaterialEditorGraphTexturePickerHitKind::Clear && !clearHit.assetId.IsValid(),
        "Texture graph picker exposes an explicit None / Clear item");
    const MaterialEditorGraphTexturePickerHit tileHit = MaterialEditorPanelRenderer::GraphTexturePickerHit(
        content,
        context,
        Center(pickerLayout.itemRects[1]).x,
        Center(pickerLayout.itemRects[1]).y);
    report.Check(
        tileHit.kind == MaterialEditorGraphTexturePickerHitKind::Texture &&
            !pickerTextureIds.empty() &&
            tileHit.assetId == pickerTextureIds.front(),
        "Texture graph picker tile hit-test resolves a texture asset without falling through to the graph canvas");
    report.Check(context.SetMaterialGraphTexturePickerSelected(tileHit.assetId), "Texture graph picker selects a texture tile");
    const int pickerMaxScroll = MaterialEditorPanelRenderer::GraphTexturePickerMaxScroll(content, context);
    report.Check(pickerMaxScroll > 0, "Texture graph picker computes a scroll range for multi-row texture lists");
    report.Check(context.ScrollMaterialGraphTexturePicker(-120, pickerMaxScroll), "Texture graph picker consumes wheel scrolling");
    const MaterialEditorGraphTexturePickerHit acceptHit =
        MaterialEditorPanelRenderer::GraphTexturePickerHit(content, context, Center(pickerLayout.accept).x, Center(pickerLayout.accept).y);
    report.Check(acceptHit.kind == MaterialEditorGraphTexturePickerHitKind::Accept, "Texture graph picker Accept button owns its hit-test");
    report.Check(
        context.SetMaterialGraphTextureSampleAsset(materialId, 50U, context.MaterialGraphTexturePickerSelectedAssetId()),
        "Texture graph picker selected asset applies to the texture node");
    report.Check(context.CloseMaterialGraphTexturePicker() && !context.IsMaterialGraphTexturePickerOpen(), "Texture graph picker closes cleanly");

    const MaterialEditorPanelLayout opaqueLayout = MaterialEditorPanelRenderer::ResolveLayout(content);
    context.MaterialEditor().SetInfoPanelVisible(true);
    MaterialEditorOpaqueOverlayHit opaqueHit = MaterialEditorPanelRenderer::OpaqueOverlayAt(
        content, context, Center(opaqueLayout.detailsPanel).x, Center(opaqueLayout.detailsPanel).y);
    report.Check(opaqueHit.kind == MaterialEditorOpaqueOverlayKind::Details,
        "P1.33 Details panel is an opaque input overlay over the graph canvas");
    context.MaterialEditor().SetInfoPanelVisible(false);
    context.MaterialEditor().SetDiagnostics({ "P1.33 diagnostic" }, true);
    opaqueHit = MaterialEditorPanelRenderer::OpaqueOverlayAt(
        content, context, Center(opaqueLayout.diagnosticsPanel).x, Center(opaqueLayout.diagnosticsPanel).y);
    report.Check(opaqueHit.kind == MaterialEditorOpaqueOverlayKind::Diagnostics,
        "P1.33 Diagnostics panel is an opaque input overlay over the graph canvas");
    context.MaterialEditor().ClearDiagnostics();
    opaqueHit = MaterialEditorPanelRenderer::OpaqueOverlayAt(
        content, context, Center(opaqueLayout.previewFrame).x, Center(opaqueLayout.previewFrame).y);
    report.Check(opaqueHit.kind == MaterialEditorOpaqueOverlayKind::Preview,
        "P1.33 Preview frame is an opaque input overlay over the graph canvas");
    report.Check(context.OpenMaterialGraphContextMenu(materialId, 300, 300, 0, 0),
        "P1.33 context menu opens for opaque-overlay verification");
    opaqueHit = MaterialEditorPanelRenderer::OpaqueOverlayAt(content, context, 305, 305);
    report.Check(opaqueHit.kind == MaterialEditorOpaqueOverlayKind::ContextMenu,
        "P1.33 blank context-menu pixels are owned by the context-menu overlay");
    static_cast<void>(context.CloseMaterialGraphContextMenu());

    const std::optional<RECT> textureRect =
        MaterialEditorPanelRenderer::GraphNodeRect(content, material.graph, 50U, context, materialId);
    const std::optional<RECT> parameterRect =
        MaterialEditorPanelRenderer::GraphNodeRect(content, material.graph, 51U, context, materialId);
    report.Check(textureRect.has_value() && parameterRect.has_value(), "Texture graph UX test resolves texture node rects");
    if (!textureRect.has_value() || !parameterRect.has_value()) {
        return;
    }

    const RECT preview = MaterialEditorPanelTextureSamplePreviewRect(*textureRect);
    const RECT picker = MaterialEditorPanelTextureSamplePickerRect(*textureRect);
    report.Check(
        preview.left >= textureRect->left &&
            preview.top > textureRect->top &&
            preview.right <= textureRect->right &&
            preview.bottom < textureRect->bottom,
        "TextureSample preview stays inside the node frame");
    report.Check(
        picker.left == preview.left &&
            picker.right == preview.right &&
            picker.top == preview.top &&
            picker.bottom == preview.bottom,
        "TextureSample image slot is the picker hit target");
    const std::optional<std::uint32_t> slotHit =
        MaterialEditorPanelRenderer::GraphTextureSampleAt(content, material.graph, context, materialId, (picker.left + picker.right) / 2, (picker.top + picker.bottom) / 2);
    report.Check(slotHit.has_value() && *slotHit == 50U, "TextureSample image slot opens the texture asset picker");
    report.Check(
        !MaterialEditorPanelRenderer::GraphPinAt(
            content,
            material.graph,
            context,
            materialId,
            (picker.left + picker.right) / 2,
            (picker.top + picker.bottom) / 2)
             .has_value(),
        "TextureSample image slot does not start a material graph wire drag");

    const RECT parameterValue = MaterialEditorPanelTextureParameterRect(*parameterRect);
    report.Check(
        parameterValue.left >= parameterRect->left &&
            parameterValue.top > parameterRect->top &&
            parameterValue.right <= parameterRect->right &&
            parameterValue.bottom <= parameterRect->bottom,
        "ParameterTexture value picker stays inside the node frame");

    for (const TextureNodeCase& nodeCase : sampleNodes) {
        const std::optional<RECT> rect =
            MaterialEditorPanelRenderer::GraphNodeRect(content, material.graph, nodeCase.id, context, materialId);
        report.Check(rect.has_value(), "Texture sample family node resolves a graph rect");
        if (!rect.has_value()) {
            continue;
        }
        const RECT familyPreview = MaterialEditorPanelTextureSamplePreviewRect(*rect);
        const RECT familyPicker = MaterialEditorPanelTextureSamplePickerRect(*rect);
        report.Check(
            familyPreview.left >= rect->left &&
                familyPreview.top > rect->top &&
                familyPreview.right <= rect->right &&
                familyPreview.bottom < rect->bottom &&
                familyPicker.left == familyPreview.left &&
                familyPicker.right == familyPreview.right &&
                familyPicker.top == familyPreview.top &&
                familyPicker.bottom == familyPreview.bottom,
            "Texture sample family keeps a single image picker slot inside the node");
        const std::optional<std::uint32_t> hit =
            MaterialEditorPanelRenderer::GraphTextureSampleAt(content, material.graph, context, materialId, (familyPicker.left + familyPicker.right) / 2, (familyPicker.top + familyPicker.bottom) / 2);
        report.Check(hit.has_value() && *hit == nodeCase.id, "Texture sample family image slot hit-tests to the editable texture node");
        report.Check(
            context.SetMaterialGraphTextureSampleAsset(materialId, nodeCase.id, kb::assets::AssetId{}),
            "Texture sample family accepts texture asset edits");
        const std::vector<MaterialEditorGraphNodeProperty> properties =
            context.MaterialEditor().GraphNodeProperties(nodeCase.id);
        report.Check(
            std::any_of(properties.begin(), properties.end(), [](const MaterialEditorGraphNodeProperty& property) {
                return property.stableId == "texture.asset" &&
                    property.kind == MaterialEditorGraphNodePropertyKind::TextureAsset;
            }),
            "Texture sample family exposes texture asset details property");
    }

    for (const TextureNodeCase& nodeCase : textureValueNodes) {
        const std::optional<RECT> rect =
            MaterialEditorPanelRenderer::GraphNodeRect(content, material.graph, nodeCase.id, context, materialId);
        report.Check(rect.has_value(), "Texture object family node resolves a graph rect");
        if (!rect.has_value()) {
            continue;
        }
        const RECT valueRect = MaterialEditorPanelTextureParameterRect(*rect);
        report.Check(
            valueRect.left >= rect->left &&
                valueRect.top > rect->top &&
                valueRect.right <= rect->right &&
                valueRect.bottom <= rect->bottom,
            "Texture object family keeps preview and picker inside the node");
        const std::optional<std::uint32_t> hit =
            MaterialEditorPanelRenderer::GraphTextureSampleAt(content, material.graph, context, materialId, valueRect.left + 2, valueRect.top + 2);
        report.Check(hit.has_value() && *hit == nodeCase.id, "Texture object family hit-tests to the editable texture node");
        report.Check(
            context.SetMaterialGraphTextureSampleAsset(materialId, nodeCase.id, kb::assets::AssetId{}),
            "Texture object family accepts texture asset edits");
        const std::vector<MaterialEditorGraphNodeProperty> properties =
            context.MaterialEditor().GraphNodeProperties(nodeCase.id);
        report.Check(
            std::any_of(properties.begin(), properties.end(), [](const MaterialEditorGraphNodeProperty& property) {
                return property.stableId == "texture.asset" &&
                    property.kind == MaterialEditorGraphNodePropertyKind::TextureAsset;
            }),
            "Texture object family exposes texture asset details property");
    }

    context.MaterialEditor().MarkSaved();
    const kb::assets::AssetId rawGraphId{ 0x7E5F0U };
    const std::filesystem::path rawGraphPath =
        std::filesystem::temp_directory_path() / "21kb_selftest" / "RawOpenGraph.kbmaterialgraph";
    const kb::render::RenderMaterialGraphDocument rawGraph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    report.Check(kb::render::RenderMaterialGraphAssetLoader::SaveGraph(rawGraphPath, rawGraph),
        "P1.9 create standalone raw Material Graph fixture");
    report.Check(context.Scene().Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
            .id = rawGraphId,
            .type = kb::render::kRenderMaterialGraphAssetType,
            .name = "RawOpenGraph",
            .virtualPath = "/Game/Materials/RawOpenGraph.kbmaterialgraph",
            .physicalPath = rawGraphPath,
            .runtimeLoadable = true,
        }),
        "P1.9 register standalone raw Material Graph fixture");
    report.Check(context.OpenMaterialEditorAsset(rawGraphId) &&
            context.MaterialEditor().OpenAssetId() == rawGraphId &&
            context.MaterialEditor().WorkingCopy().has_value(),
        "P1.9 standalone raw Material Graph opens directly in Material Editor");
    std::uint32_t rawNodeId = 0U;
    const bool rawNodeAdded = context.AddMaterialGraphNode(rawGraphId, kb::render::RenderMaterialGraphNodeKind::ConstantScalar, -120, 80);
    rawNodeId = context.SelectedMaterialGraphNodeId();
    report.Check(rawNodeAdded && rawNodeId != 0U,
        "P1.9 standalone raw Material Graph accepts working-copy edits");
    report.Check(context.SaveMaterialEditorAsset(rawGraphId),
        "P1.9 standalone raw Material Graph saves without creating a Material asset");
    const std::optional<kb::render::RenderMaterialGraphDocument> savedRawGraph =
        kb::render::RenderMaterialGraphAssetLoader::LoadGraph(rawGraphPath);
    report.Check(savedRawGraph.has_value() && kb::render::FindRenderMaterialGraphNode(*savedRawGraph, rawNodeId) != nullptr,
        "P1.9 standalone raw Material Graph persists direct editor changes");
}

void RunMaterialGraphDenseNodeLayoutSuite(Report& report) {
    auto nodeRectFor = [](kb::render::RenderMaterialGraphNodeKind kind) {
        const SIZE size = MaterialEditorPanelGraphNodeSize(kind);
        return RECT{ 40, 40, 40 + size.cx, 40 + size.cy };
    };
    auto nodeRectForNode = [](const kb::render::RenderMaterialGraphNode& node) {
        const SIZE size = MaterialEditorPanelGraphNodeSize(node);
        return RECT{ 40, 40, 40 + size.cx, 40 + size.cy };
    };
    auto checkOutputSpacing = [&](kb::render::RenderMaterialGraphNodeKind kind, std::size_t count, const char* label) {
        const RECT rect = nodeRectFor(kind);
        int previousY = std::numeric_limits<int>::min();
        bool spaced = true;
        for (std::size_t index = 0U; index < count; ++index) {
            const POINT pin = MaterialEditorPanelOutputPinPoint(rect, kind, index, count);
            if (index > 0U && pin.y - previousY < 15) {
                spaced = false;
            }
            previousY = pin.y;
        }
        report.Check(spaced, label);
    };
    auto checkInputSpacing = [&](kb::render::RenderMaterialGraphNodeKind kind, std::size_t count, const char* label) {
        const RECT rect = nodeRectFor(kind);
        int previousY = std::numeric_limits<int>::min();
        bool spaced = true;
        for (std::size_t index = 0U; index < count; ++index) {
            const POINT pin = MaterialEditorPanelInputPinPoint(rect, kind, index);
            if (index > 0U && pin.y - previousY < 15) {
                spaced = false;
            }
            previousY = pin.y;
        }
        report.Check(spaced, label);
    };

    checkOutputSpacing(
        kb::render::RenderMaterialGraphNodeKind::CollectionParameter,
        8U,
        "CollectionParameter output pins keep readable spacing");
    checkInputSpacing(
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        14U,
        "MaterialOutput input pins keep readable spacing");
    checkInputSpacing(
        kb::render::RenderMaterialGraphNodeKind::SetMaterialAttributes,
        11U,
        "SetMaterialAttributes input pins keep readable spacing");

    bool allCatalogInputsSpaced = true;
    bool allCatalogOutputsSpaced = true;
    for (const kb::render::RenderMaterialGraphNodeKind kind : kb::render::AllRenderMaterialGraphNodeKinds()) {
        const RECT rect = nodeRectFor(kind);
        const std::vector<std::string> inputPins = kb::render::RenderMaterialGraphNodeInputPinNames(kind);
        int previousInputY = std::numeric_limits<int>::min();
        for (std::size_t index = 0U; index < inputPins.size(); ++index) {
            const POINT pin = MaterialEditorPanelInputPinPoint(rect, kind, index);
            if (index > 0U && pin.y - previousInputY < 15) {
                allCatalogInputsSpaced = false;
                break;
            }
            previousInputY = pin.y;
        }
        const std::vector<std::string> outputPins = kb::render::RenderMaterialGraphNodeOutputPinNames(kind);
        int previousOutputY = std::numeric_limits<int>::min();
        for (std::size_t index = 0U; index < outputPins.size(); ++index) {
            const POINT pin = MaterialEditorPanelOutputPinPoint(rect, kind, index, outputPins.size());
            if (index > 0U && pin.y - previousOutputY < 15) {
                allCatalogOutputsSpaced = false;
                break;
            }
            previousOutputY = pin.y;
        }
    }
    report.Check(allCatalogInputsSpaced, "Every catalog node kind keeps readable input pin spacing");
    report.Check(allCatalogOutputsSpaced, "Every catalog node kind keeps readable output pin spacing");

    kb::render::RenderMaterialGraphNode denseCustom{
        .id = 80U,
        .kind = kb::render::RenderMaterialGraphNodeKind::CustomCode,
    };
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        denseCustom.customCode.inputs.push_back(kb::render::RenderMaterialGraphCustomPin{
            .name = "Input" + std::to_string(index),
            .type = kb::render::RenderMaterialGraphPinType::Float4,
        });
    }
    const RECT denseCustomRect = nodeRectForNode(denseCustom);
    bool denseCustomInputsSpaced = true;
    int previousCustomInputY = std::numeric_limits<int>::min();
    for (std::size_t index = 0U; index < denseCustom.customCode.inputs.size(); ++index) {
        const POINT pin = MaterialEditorPanelInputPinPoint(denseCustomRect, denseCustom.kind, index);
        if (index > 0U && pin.y - previousCustomInputY < 15) {
            denseCustomInputsSpaced = false;
        }
        previousCustomInputY = pin.y;
    }
    report.Check(denseCustomInputsSpaced, "CustomCode dynamic input pins resize the node before they overlap");

    kb::render::RenderMaterialGraphNode denseFunction{
        .id = 81U,
        .kind = kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall,
    };
    for (std::uint32_t index = 0U; index < 6U; ++index) {
        denseFunction.customCode.inputs.push_back(kb::render::RenderMaterialGraphCustomPin{
            .name = "In" + std::to_string(index),
            .type = kb::render::RenderMaterialGraphPinType::Float4,
        });
        denseFunction.customCode.outputs.push_back(kb::render::RenderMaterialGraphCustomPin{
            .name = "Out" + std::to_string(index),
            .type = kb::render::RenderMaterialGraphPinType::Float4,
        });
    }
    const RECT denseFunctionRect = nodeRectForNode(denseFunction);
    bool denseFunctionOutputsSpaced = true;
    int previousFunctionOutputY = std::numeric_limits<int>::min();
    for (std::size_t index = 0U; index < denseFunction.customCode.outputs.size(); ++index) {
        const POINT pin = MaterialEditorPanelOutputPinPoint(denseFunctionRect, denseFunction.kind, index, denseFunction.customCode.outputs.size());
        if (index > 0U && pin.y - previousFunctionOutputY < 15) {
            denseFunctionOutputsSpaced = false;
        }
        previousFunctionOutputY = pin.y;
    }
    report.Check(denseFunctionOutputsSpaced, "MaterialFunctionCall dynamic output pins resize the node before they overlap");
}

[[nodiscard]] std::optional<CLSID> GdiplusEncoderClsid(const wchar_t* mimeType) {
    UINT count = 0;
    UINT bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (count == 0 || bytes == 0) {
        return std::nullopt;
    }
    std::vector<std::byte> buffer(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) {
        return std::nullopt;
    }
    for (UINT index = 0; index < count; ++index) {
        if (std::wcscmp(encoders[index].MimeType, mimeType) == 0) {
            return encoders[index].Clsid;
        }
    }
    return std::nullopt;
}

void RunMaterialGraphVisualRedesignSuite(Report& report) {
    std::error_code error;

    EditorSceneContext context;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Register material loader for node visual redesign capture");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "NodeRedesignAudit.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);

    kb::render::RenderMaterialAssetData material{};
    material.materialType = kb::render::kRenderMaterialAssetBuiltInPbrType;
    material.materialTypeVersion = kb::render::kRenderMaterialAssetBuiltInPbrTypeVersion;
    material.hasExplicitMaterialType = true;
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    for (kb::render::RenderMaterialGraphNode& node : material.graph.nodes) {
        if (node.id == 1U) {
            node.positionX = 1180;
            node.positionY = 180;
        }
    }
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 20U,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 190,
        .positionY = 90,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "auditImageTexture",
            .displayName = "Image Texture",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 21U,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureObjectCube,
        .positionX = 680,
        .positionY = 80,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "auditCubeObject",
            .displayName = "Cube Texture Object",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
        },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 22U,
        .kind = kb::render::RenderMaterialGraphNodeKind::NormalUnpack,
        .positionX = 720,
        .positionY = 300,
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 30U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 190,
        .positionY = 430,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.95 0.72 0.18 1" },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 31U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ColorRamp,
        .positionX = 500,
        .positionY = 440,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0.1 0.2 0.9;1 1 0.75 0.18" },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 40U,
        .kind = kb::render::RenderMaterialGraphNodeKind::Multiply,
        .positionX = 840,
        .positionY = 450,
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 50U,
        .kind = kb::render::RenderMaterialGraphNodeKind::RuntimeSwitch,
        .positionX = 1030,
        .positionY = 520,
    });
    kb::render::RenderMaterialGraphCustomCode customCode{};
    customCode.body = "return float4(A.rgb * B.rgb, 1.0);";
    customCode.inputs = {
        kb::render::RenderMaterialGraphCustomPin{ .name = "A", .type = kb::render::RenderMaterialGraphPinType::Float4 },
        kb::render::RenderMaterialGraphCustomPin{ .name = "B", .type = kb::render::RenderMaterialGraphPinType::Float4 },
        kb::render::RenderMaterialGraphCustomPin{ .name = "Mask", .type = kb::render::RenderMaterialGraphPinType::Float },
    };
    customCode.outputs = {
        kb::render::RenderMaterialGraphCustomPin{ .name = "Color", .type = kb::render::RenderMaterialGraphPinType::Float4 },
        kb::render::RenderMaterialGraphCustomPin{ .name = "Alpha", .type = kb::render::RenderMaterialGraphPinType::Float },
    };
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 60U,
        .kind = kb::render::RenderMaterialGraphNodeKind::CustomCode,
        .positionX = 840,
        .positionY = 700,
        .customCode = std::move(customCode),
    });

    {
        std::ofstream output{ materialPath, std::ios::binary | std::ios::trunc };
        kb::render::RenderMaterialAssetWriter::Write(output, material);
    }
    report.Check(context.Scene().Assets().Discover() >= 1U, "Discover material node visual redesign audit asset");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/NodeRedesignAudit.kbmat");
    report.Check(metadata != nullptr, "Resolve material node visual redesign audit asset metadata");
    if (metadata == nullptr) {
        return;
    }
    report.Check(context.OpenMaterialEditorAsset(metadata->id), "Open material node visual redesign audit asset");

    constexpr int width = 1480;
    constexpr int height = 900;
    HDC screenDc = GetDC(nullptr);
    report.Check(screenDc != nullptr, "Create screen DC for material node visual redesign capture");
    if (screenDc == nullptr) {
        return;
    }
    HDC memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
    HGDIOBJ previous = SelectObject(memoryDc, bitmap);

    HeroIconGdiplusRuntime::EnsureStarted();
    MaterialEditorPanelRenderer renderer;
    renderer.Paint(memoryDc, RECT{ 0, 0, width, height }, EditorTheme{}, context);

    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path(error) / "21kb_selftest" / "_materialEditorNodeRedesignCurrent.bmp";
    std::filesystem::create_directories(outputPath.parent_path(), error);
    const std::optional<CLSID> bmpEncoder = GdiplusEncoderClsid(L"image/bmp");
    report.Check(bmpEncoder.has_value(), "Resolve BMP encoder for material node visual redesign capture");
    if (bmpEncoder.has_value()) {
        Gdiplus::Bitmap image(bitmap, nullptr);
        report.Check(image.Save(outputPath.wstring().c_str(), &*bmpEncoder, nullptr) == Gdiplus::Ok,
            "Save material node visual redesign screenshot artifact");
        report.Note("Material node visual redesign screenshot: " + outputPath.string());
    }

    SelectObject(memoryDc, previous);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
}

void RunMaterialGraphFirstNodeVisualCheckpointSuite(Report& report) {
    std::error_code error;

    EditorSceneContext context;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Register material loader for first material node checkpoint capture");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "FirstNodeCheckpoint.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);

    kb::render::RenderMaterialAssetData material{};
    material.materialType = kb::render::kRenderMaterialAssetBuiltInPbrType;
    material.materialTypeVersion = kb::render::kRenderMaterialAssetBuiltInPbrTypeVersion;
    material.hasExplicitMaterialType = true;
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    for (kb::render::RenderMaterialGraphNode& node : material.graph.nodes) {
        if (node.id == 1U) {
            node.positionX = 560;
            node.positionY = 165;
        }
    }
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 190,
        .positionY = 170,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "RGBA Checkpoint",
            .defaultValueHint = "0.92 0.18 0.10 1",
        },
    });
    material.graph.links.push_back(MakeSelfTestGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, material), "Create first material node checkpoint fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Discover first material node checkpoint fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/FirstNodeCheckpoint.kbmat");
    report.Check(metadata != nullptr, "Resolve first material node checkpoint metadata");
    if (metadata == nullptr) {
        return;
    }
    report.Check(context.OpenMaterialEditorAsset(metadata->id), "Open first material node checkpoint in Material Editor");

    constexpr int width = 960;
    constexpr int height = 560;
    const RECT content{ 0, 0, width, height };
    const std::optional<RECT> colorRect =
        MaterialEditorPanelRenderer::GraphNodeRect(content, material.graph, 2U, context, metadata->id);
    const std::optional<RECT> outputRect =
        MaterialEditorPanelRenderer::GraphNodeRect(content, material.graph, 1U, context, metadata->id);
    report.Check(colorRect.has_value() && outputRect.has_value(), "First material node checkpoint resolves ConstantColor and MaterialOutput rects");

    HDC screenDc = GetDC(nullptr);
    report.Check(screenDc != nullptr, "Create screen DC for first material node checkpoint capture");
    if (screenDc == nullptr) {
        return;
    }
    HDC memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
    HGDIOBJ previous = SelectObject(memoryDc, bitmap);

    HeroIconGdiplusRuntime::EnsureStarted();
    MaterialEditorPanelRenderer renderer;
    renderer.Paint(memoryDc, content, EditorTheme{}, context);

    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path(error) / "21kb_selftest" / "_materialEditorFirstNodeCheckpoint.bmp";
    std::filesystem::create_directories(outputPath.parent_path(), error);
    const std::optional<CLSID> bmpEncoder = GdiplusEncoderClsid(L"image/bmp");
    report.Check(bmpEncoder.has_value(), "Resolve BMP encoder for first material node checkpoint capture");
    if (bmpEncoder.has_value()) {
        Gdiplus::Bitmap image(bitmap, nullptr);
        report.Check(image.Save(outputPath.wstring().c_str(), &*bmpEncoder, nullptr) == Gdiplus::Ok,
            "Save first material node checkpoint screenshot artifact");
        report.Note("First material node checkpoint screenshot: " + outputPath.string());
    }

    SelectObject(memoryDc, previous);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
}

void RunMaterialGraphCanvasClipSuite(Report& report) {
    std::error_code error;

    EditorSceneContext context;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Register material loader for graph canvas clipping fixture");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "ClipProbe.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);

    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureSample,
        .positionX = -160,
        .positionY = 70,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "clipProbeTexture",
            .displayName = "Clip Probe",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        },
    });
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, material), "Create graph canvas clipping material fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Discover graph canvas clipping material fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/ClipProbe.kbmat");
    report.Check(metadata != nullptr, "Resolve graph canvas clipping material metadata");
    if (metadata == nullptr) {
        return;
    }
    report.Check(context.OpenMaterialEditorAsset(metadata->id), "Open graph canvas clipping material in Material Editor");

    constexpr int width = 820;
    constexpr int height = 460;
    constexpr COLORREF sentinel = RGB(247, 13, 193);
    HDC screenDc = GetDC(nullptr);
    report.Check(screenDc != nullptr, "Create screen DC for graph canvas clipping capture");
    if (screenDc == nullptr) {
        return;
    }
    HDC memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
    HGDIOBJ previous = SelectObject(memoryDc, bitmap);
    const RECT full{ 0, 0, width, height };
    HBRUSH sentinelBrush = CreateSolidBrush(sentinel);
    FillRect(memoryDc, &full, sentinelBrush);
    DeleteObject(sentinelBrush);

    MaterialEditorPanelRenderer{}.Paint(memoryDc, RECT{ 180, 0, 780, 440 }, EditorTheme{}, context);
    bool outsideCanvasClean = true;
    int firstLeakX = -1;
    int firstLeakY = -1;
    COLORREF firstLeakColor = CLR_INVALID;
    for (int y = 60; y < 360 && outsideCanvasClean; ++y) {
        for (int x = 0; x < 180; ++x) {
            const COLORREF pixel = GetPixel(memoryDc, x, y);
            if (pixel != sentinel) {
                outsideCanvasClean = false;
                firstLeakX = x;
                firstLeakY = y;
                firstLeakColor = pixel;
                break;
            }
        }
    }
    if (!outsideCanvasClean) {
        report.Note(
            "First graph canvas clip leak at (" + std::to_string(firstLeakX) + ", " + std::to_string(firstLeakY) +
            ") color=" + std::to_string(static_cast<std::uint32_t>(firstLeakColor)));
    }
    report.Check(outsideCanvasClean,
        "Material graph node paint is clipped before the Material Editor canvas");
    report.Check(GetPixel(memoryDc, 220, 180) != sentinel,
        "Material graph clipping fixture drew the node inside the Material Editor canvas");

    SelectObject(memoryDc, previous);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
}

void RunMaterialEditorGlobalSaveSuite(Report& report) {
    std::error_code error;
    const auto readFileBytes = [](const std::filesystem::path& path) {
        std::ifstream input{ path, std::ios::binary };
        return std::string{
            std::istreambuf_iterator<char>{ input },
            std::istreambuf_iterator<char>{} };
    };
    const auto serializeMaterial = [](const kb::render::RenderMaterialAssetData& asset) {
        std::ostringstream output;
        kb::render::RenderMaterialAssetWriter::Write(output, asset);
        return output.str();
    };
    const auto serializeInstance = [](const kb::render::RenderMaterialInstanceAssetData& asset) {
        std::ostringstream output;
        kb::render::RenderMaterialInstanceAssetWriter::Write(output, asset);
        return output.str();
    };

    EditorSceneContext context;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Register material loader for global Save fixture");
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialInstanceAssetLoader>()),
        "Register material instance loader for global Save fixture");
    const kb::assets::AssetId normalTextureId{ 424242U };
    report.Check(context.Scene().Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                     .id = normalTextureId,
                     .type = "RenderTexture",
                     .name = "GlobalSaveNormalTexture",
                     .virtualPath = "/Game/Textures/GlobalSaveNormalTexture.ktx",
                     .runtimeLoadable = true,
                 }),
        "Register normal texture asset for global Save fixture");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "GlobalSaveNormal.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);

    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, material), "Create material fixture for global Save");
    const std::string originalMaterialBytes = readFileBytes(materialPath);
    report.Check(context.Scene().Assets().Discover() >= 1U, "Discover global Save material fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/GlobalSaveNormal.kbmat");
    report.Check(metadata != nullptr, "Resolve global Save material metadata");
    if (metadata == nullptr) {
        return;
    }
    report.Check(context.OpenMaterialEditorAsset(metadata->id), "Open global Save material in Material Editor");
    if (!context.MaterialEditor().WorkingCopy().has_value()) {
        report.Check(false, "Global Save material working copy exists");
        return;
    }

    kb::render::RenderMaterialAssetData working = *context.MaterialEditor().WorkingCopy();
    working.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    working.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 80,
        .positionY = 120,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "normalTex",
            .displayName = "Normal Texture",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
        },
    });
    working.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::NormalUnpack,
        .positionX = 320,
        .positionY = 120,
    });
    working.graph.links.push_back(kb::render::RenderMaterialGraphLink{
        .id = 1U,
        .fromNodeId = 2U,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(kb::render::RenderMaterialGraphNodeKind::TextureSample, "color", true),
        .fromPin = "color",
        .toNodeId = 3U,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(kb::render::RenderMaterialGraphNodeKind::NormalUnpack, "color", false),
        .toPin = "color",
    });
    working.graph.links.push_back(kb::render::RenderMaterialGraphLink{
        .id = 2U,
        .fromNodeId = 3U,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(kb::render::RenderMaterialGraphNodeKind::NormalUnpack, "normal", true),
        .fromPin = "normal",
        .toNodeId = 1U,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(kb::render::RenderMaterialGraphNodeKind::MaterialOutput, "normal", false),
        .toPin = "normal",
    });
    working.graphParameterValues = {
        kb::render::RenderMaterialGraphParameterValue{
            .stableId = "normalTex",
            .type = kb::render::RenderMaterialParameterType::Texture,
            .assetId = normalTextureId.value,
        },
        kb::render::RenderMaterialGraphParameterValue{
            .stableId = "textureSample99",
            .type = kb::render::RenderMaterialParameterType::Texture,
            .assetId = 987654321ULL,
        },
    };
    context.MaterialEditor().SetWorkingCopy(std::move(working));
    report.Check(context.HasDirtyMaterialAssetEdit(), "Global Save fixture material is dirty after graph edit");
    const bool globalSaveSucceeded = context.SaveOpenDocuments();
    report.Check(globalSaveSucceeded, "Global Save persists the dirty Material Editor working copy");
    if (!globalSaveSucceeded) {
        for (const std::string& diagnostic : context.MaterialEditor().Diagnostics()) {
            report.Note("Global Save diagnostic: " + diagnostic);
        }
    }
    report.Check(!context.HasDirtyMaterialAssetEdit(), "Global Save clears dirty Material Editor state");

    const std::optional<kb::render::RenderMaterialAssetData> reloaded = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    report.Check(reloaded.has_value(), "Reload global Save material from disk");
    if (!reloaded.has_value()) {
        return;
    }
    const kb::render::RenderMaterialGraphNode* savedSample = kb::render::FindRenderMaterialGraphNode(reloaded->graph, 2U);
    report.Check(savedSample != nullptr, "Saved graph keeps TextureSample node");
    report.Check(savedSample != nullptr && savedSample->parameter.textureRole == "normal", "Saved normal-map TextureSample role is normalized to normal");
    report.Check(savedSample != nullptr && savedSample->parameter.expectedTextureColorSpace == kb::render::RenderMaterialTextureColorSpace::Linear,
        "Saved normal-map TextureSample color space is Linear");
    report.Check(std::ranges::any_of(reloaded->graph.links, [](const kb::render::RenderMaterialGraphLink& link) {
        return link.fromNodeId == 3U && link.fromPin == "normal" && link.toNodeId == 1U && link.toPin == "normal";
    }), "Saved graph keeps NormalUnpack -> MaterialOutput.normal link");
    report.Check(std::ranges::none_of(reloaded->graphParameterValues, [](const kb::render::RenderMaterialGraphParameterValue& value) {
        return value.stableId == "textureSample99";
    }), "Saved graph prunes orphan generated texture parameter values");

    const std::string savedBytes = readFileBytes(materialPath);
    const std::string savedSnapshot = context.MaterialEditor().CleanSnapshot().has_value()
        ? serializeMaterial(*context.MaterialEditor().CleanSnapshot())
        : std::string{};
    kb::render::RenderMaterialAssetData invalid = *context.MaterialEditor().WorkingCopy();
    invalid.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 99U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = -180,
        .positionY = 40,
    });
    invalid.graph.links.push_back(kb::render::RenderMaterialGraphLink{
        .id = 99U,
        .fromNodeId = 99U,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(kb::render::RenderMaterialGraphNodeKind::ConstantScalar, "value", true),
        .fromPin = "value",
        .toNodeId = 1U,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(kb::render::RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false),
        .toPin = "baseColor",
    });
    context.MaterialEditor().SetWorkingCopy(std::move(invalid));
    const std::string invalidWorkingCopy = serializeMaterial(*context.MaterialEditor().WorkingCopy());
    context.FocusMaterialGraph(true);
    const bool materialCanUndoBeforeFailure = context.CanUndoSceneCommand();
    const bool materialCanRedoBeforeFailure = context.CanRedoSceneCommand();
    report.Check(context.MaterialEditor().Dirty(), "P0.2 invalid Save fixture is dirty before Save");
    report.Check(!context.SaveOpenDocuments(), "P0.2 invalid material is rejected before source mutation");
    report.Check(context.MaterialEditor().Dirty(), "P0.2 invalid Save leaves the working document dirty");
    const std::string preservedBytes = readFileBytes(materialPath);
    report.Check(preservedBytes == savedBytes, "P0.2 invalid Save preserves the previous material file byte-for-byte");
    report.Check(context.MaterialEditor().WorkingCopy().has_value() &&
            serializeMaterial(*context.MaterialEditor().WorkingCopy()) == invalidWorkingCopy,
        "P0.2 invalid Save preserves the exact dirty material working copy");
    report.Check(context.MaterialEditor().CleanSnapshot().has_value() &&
            serializeMaterial(*context.MaterialEditor().CleanSnapshot()) == savedSnapshot,
        "P0.2 invalid Save preserves the material clean snapshot");
    report.Check(context.CanUndoSceneCommand() == materialCanUndoBeforeFailure &&
            context.CanRedoSceneCommand() == materialCanRedoBeforeFailure,
        "P0.2 invalid Save preserves the material history state");
    report.Check(context.UndoSceneCommand(),
        "P0.2 material history head remains the preceding successful Save after invalid preflight");
    report.Check(readFileBytes(materialPath) == originalMaterialBytes,
        "P0.2 first Undo after invalid material Save restores the pre-save source, proving no failed Save command was inserted");
    report.Check(context.RedoSceneCommand(),
        "P0.2 material history can redo the preceding successful Save after invalid preflight");
    report.Check(readFileBytes(materialPath) == savedBytes,
        "P0.2 material redo restores the last valid byte-identical source");

    const kb::assets::AssetId parentMaterialId = metadata->id;
    kb::render::RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMaterialId;
    const std::filesystem::path instancePath =
        EditorProjectPaths::AssetsRoot() / "Materials" / "GlobalSaveNormal_Inst.kbmatinst";
    report.Check(kb::render::RenderMaterialInstanceAssetWriter::Save(instancePath, instance),
        "Create material instance fixture for global Save preflight");
    const std::string originalInstanceBytes = readFileBytes(instancePath);
    report.Check(context.Scene().Assets().Discover() >= 2U,
        "Discover global Save material instance fixture");
    const kb::assets::AssetMetadata* instanceMetadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/GlobalSaveNormal_Inst.kbmatinst");
    report.Check(instanceMetadata != nullptr, "Resolve global Save material instance metadata");
    if (instanceMetadata == nullptr) {
        return;
    }
    report.Check(context.OpenMaterialEditorAsset(instanceMetadata->id),
        "Open global Save material instance in Material Editor");
    if (!context.MaterialEditor().InstanceWorkingCopy().has_value() ||
        !context.MaterialEditor().InstanceParentSnapshot().has_value()) {
        report.Check(false, "Global Save material instance working copy and parent snapshot exist");
        return;
    }

    kb::render::RenderMaterialInstanceAssetData validInstance = *context.MaterialEditor().InstanceWorkingCopy();
    validInstance.basePropertyOverrides.overrideTwoSided = true;
    validInstance.basePropertyOverrides.twoSided = true;
    context.MaterialEditor().SetInstanceWorkingCopy(
        validInstance,
        kb::render::BuildEffectiveRenderMaterialInstanceAsset(
            *context.MaterialEditor().InstanceParentSnapshot(),
            validInstance));
    report.Check(context.SaveOpenDocuments(),
        "Global Save persists the valid material instance working copy");
    const std::string savedInstanceBytes = readFileBytes(instancePath);
    const std::string savedInstanceSnapshot = context.MaterialEditor().InstanceCleanSnapshot().has_value()
        ? serializeInstance(*context.MaterialEditor().InstanceCleanSnapshot())
        : std::string{};

    kb::render::RenderMaterialInstanceAssetData invalidInstance =
        *context.MaterialEditor().InstanceWorkingCopy();
    invalidInstance.hasOverrides = true;
    invalidInstance.overrides = *context.MaterialEditor().InstanceParentSnapshot();
    invalidInstance.overrides.graphParameterValues.clear();
    invalidInstance.overrides.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "missingParameter",
        .type = kb::render::RenderMaterialParameterType::Scalar,
        .numbers = { 0.5F, 0.0F, 0.0F, 0.0F },
    });
    context.MaterialEditor().SetInstanceWorkingCopy(
        invalidInstance,
        kb::render::BuildEffectiveRenderMaterialInstanceAsset(
            *context.MaterialEditor().InstanceParentSnapshot(),
            invalidInstance));
    const std::string invalidInstanceWorkingCopy =
        serializeInstance(*context.MaterialEditor().InstanceWorkingCopy());
    context.FocusMaterialGraph(true);
    const bool instanceCanUndoBeforeFailure = context.CanUndoSceneCommand();
    const bool instanceCanRedoBeforeFailure = context.CanRedoSceneCommand();
    report.Check(context.MaterialEditor().Dirty(),
        "P0.2 invalid instance Save fixture is dirty before Save");
    report.Check(!context.SaveOpenDocuments(),
        "P0.2 invalid material instance is rejected before source mutation");
    report.Check(context.MaterialEditor().Dirty(),
        "P0.2 invalid material instance Save leaves the working document dirty");
    report.Check(readFileBytes(instancePath) == savedInstanceBytes,
        "P0.2 invalid material instance Save preserves the previous file byte-for-byte");
    report.Check(context.MaterialEditor().InstanceWorkingCopy().has_value() &&
            serializeInstance(*context.MaterialEditor().InstanceWorkingCopy()) == invalidInstanceWorkingCopy,
        "P0.2 invalid material instance Save preserves the exact dirty working copy");
    report.Check(context.MaterialEditor().InstanceCleanSnapshot().has_value() &&
            serializeInstance(*context.MaterialEditor().InstanceCleanSnapshot()) == savedInstanceSnapshot,
        "P0.2 invalid material instance Save preserves the clean snapshot");
    report.Check(context.CanUndoSceneCommand() == instanceCanUndoBeforeFailure &&
            context.CanRedoSceneCommand() == instanceCanRedoBeforeFailure,
        "P0.2 invalid material instance Save preserves the history state");
    report.Check(context.UndoSceneCommand(),
        "P0.2 material instance history head remains the preceding successful Save after invalid preflight");
    report.Check(readFileBytes(instancePath) == originalInstanceBytes,
        "P0.2 first Undo after invalid instance Save restores the pre-save source, proving no failed Save command was inserted");
    report.Check(context.RedoSceneCommand(),
        "P0.2 material instance history can redo the preceding successful Save after invalid preflight");
    report.Check(readFileBytes(instancePath) == savedInstanceBytes,
        "P0.2 material instance redo restores the last valid byte-identical source");
}

void RunInspectorLightComponentSuite(Report& report) {
    EditorSceneContext context;
    const kb::scene::SceneEntity lightEntity = context.CreateLightObject(kb::scene::LightKind::Point);
    report.Check(lightEntity.IsValid(), "Create Point Light from editor command");
    report.Check(context.Scene().Components().Lights().Has(lightEntity), "Created Point Light owns a real Light component");
    context.SelectEntity(lightEntity);

    InspectorPanelRenderer::Hit typeHit{};
    InspectorPanelRenderer::Hit intensityHit{};
    InspectorPanelRenderer::Hit castsShadowHit{};
    for (int y = kContent.top; y < kContent.bottom; ++y) {
        for (int x = kContent.left; x < kContent.right; ++x) {
            const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(kContent, context, x, y);
            if (hit.section != InspectorSectionId::Light) {
                continue;
            }
            if (typeHit.kind == InspectorHitKind::None && hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::LightKind) {
                typeHit = hit;
            }
            if (intensityHit.kind == InspectorHitKind::None && hit.kind == InspectorHitKind::FloatField && hit.property == InspectorPropertyId::LightIntensity) {
                intensityHit = hit;
            }
            if (castsShadowHit.kind == InspectorHitKind::None && hit.kind == InspectorHitKind::BoolField && hit.property == InspectorPropertyId::LightCastsShadow) {
                castsShadowHit = hit;
            }
        }
    }

    report.Check(typeHit.kind != InspectorHitKind::None, "Light Inspector exposes the light type field");
    report.Check(intensityHit.kind != InspectorHitKind::None, "Light Inspector exposes editable intensity");
    report.Check(castsShadowHit.kind != InspectorHitKind::None, "Light Inspector exposes shadow toggle");

    if (intensityHit.kind != InspectorHitKind::None) {
        const POINT point = Center(intensityHit.rect);
        report.Check(InspectorPanelInteraction::HandlePointerDown(context, intensityHit, point.x, point.y), "Clicking Light intensity starts editing");
        context.Inspector().ClearText();
        context.Inspector().InsertText("4.25");
        report.Check(InspectorPanelInteraction::HandleKeyDown(nullptr, context, static_cast<WPARAM>(0x0D)), "Committing Light intensity is handled");
        const kb::scene::LightComponent* light = context.Scene().Components().Lights().TryGet(lightEntity);
        report.Check(light != nullptr && std::abs(light->intensity - 4.25F) < 0.001F, "Committed Light intensity updates the runtime component");
    }

    if (castsShadowHit.kind != InspectorHitKind::None) {
        const POINT point = Center(castsShadowHit.rect);
        report.Check(InspectorPanelInteraction::HandlePointerDown(context, castsShadowHit, point.x, point.y), "Clicking Light shadow toggle is handled");
        const kb::scene::LightComponent* light = context.Scene().Components().Lights().TryGet(lightEntity);
        report.Check(light != nullptr && !light->castsShadow, "Light shadow toggle updates the runtime component");
    }

    if (typeHit.kind != InspectorHitKind::None) {
        const POINT point = Center(typeHit.rect);
        report.Check(InspectorPanelInteraction::HandlePointerDown(context, typeHit, point.x, point.y), "Clicking Light type cycles the component kind");
        const kb::scene::LightComponent* light = context.Scene().Components().Lights().TryGet(lightEntity);
        report.Check(light != nullptr && light->kind == kb::scene::LightKind::Spot, "Point Light cycles to Spot Light through the Inspector");
    }
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
    report.Check(
        context.ProjectPluginBinaryPath(descriptor->id) == EditorPluginCatalog::PersistentBinaryPath(descriptor->id),
        "Enabled plugin stores a config-agnostic binary path");
    report.Check(context.Plugins().HasPendingReload(), "Enabling plugin marks pending reload");

    {
        const kb::project::ProjectDescriptorReadResult reloaded = kb::project::ProjectManager::LoadProject(context.ProjectFile());
        report.Check(reloaded.succeeded, "Project descriptor reloads after enabling plugin");
        const auto iter = std::find_if(reloaded.descriptor.plugins.begin(), reloaded.descriptor.plugins.end(), [descriptor](const kb::project::ProjectPluginReference& plugin) {
            return plugin.name == descriptor->id;
        });
        report.Check(iter != reloaded.descriptor.plugins.end(), "Enabled plugin reference persisted to descriptor");
        report.Check(iter != reloaded.descriptor.plugins.end() && iter->enabled, "Persisted plugin reference is enabled");
        report.Check(
            iter != reloaded.descriptor.plugins.end() && iter->binaryPath == EditorPluginCatalog::PersistentBinaryPath(descriptor->id),
            "Persisted plugin reference keeps a config-agnostic binary path");
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

void RunMaterialGraphInteractionLifecycleSuite(Report& report) {
    EditorSceneContext context;
    const kb::assets::AssetId materialId{ 0x7110U };
    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 20,
        .positionY = 20,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .displayName = "Old Name" },
    });
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 320,
        .positionY = 20,
    });
    kb::render::RenderMaterialGraphLink link{
        .fromNodeId = 2U,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(kb::render::RenderMaterialGraphNodeKind::ConstantColor, "rgba", true),
        .fromPin = "rgba",
        .toNodeId = 1U,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(kb::render::RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false),
        .toPin = "baseColor",
    };
    link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
    material.graph.links.push_back(link);
    material.graph.comments.push_back(kb::render::RenderMaterialGraphCommentBox{
        .id = 10U, .positionX = 0, .positionY = 0, .width = 200, .height = 200, .text = "Group",
    });
    material.graph.composites.push_back(kb::render::RenderMaterialGraphCompositeSubgraph{
        .id = 20U, .positionX = 0, .positionY = 0, .width = 420, .height = 260,
        .collapsed = true, .name = "Collapsed", .nodeIds = { 2U, 3U },
    });
    context.MaterialEditor().Open(materialId, material);
    context.FocusMaterialGraph(true);

    report.Check(context.DetachMaterialGraphInputPinConnection(materialId, 1U, "baseColor", 20, 20),
        "P1.22 begin production rewire transaction");
    report.Check(context.CancelMaterialGraphPinConnection() && context.MaterialEditor().WorkingCopy()->graph.links.size() == 1U &&
            !context.MaterialEditor().Dirty() && !context.CanUndoSceneCommand(),
        "P1.22 cancel rewire restores original link without history");

    static_cast<void>(context.SetMaterialGraphNodeSelection({ 2U, 3U }, 2U));
    static_cast<void>(context.SelectMaterialGraphContextTarget(3U, 0U));
    report.Check(context.SelectedMaterialGraphNodeIds() == std::vector<std::uint32_t>({ 2U, 3U }) &&
            context.SelectedMaterialGraphNodeId() == 2U,
        "P1.27 RMB target preserves selected multi-selection and primary");
    report.Check(context.BeginMaterialGraphPan(10, 10) && !context.DragMaterialGraphPan(11, 10) &&
            !context.HasMaterialGraphPanMoved() && context.EndMaterialGraphPan(),
        "P1.27 one-pixel RMB jitter remains a context-menu click");

    static_cast<void>(context.SelectMaterialGraphComment(10U));
    report.Check(context.BeginMaterialGraphCommentDrag(materialId, 10U, 0, 0) &&
            context.DragMaterialGraphComment(300, 0) && context.DragMaterialGraphComment(320, 0),
        "P1.25/P1.26 begin live comment drag with membership snapshot");
    const auto outside = context.MaterialEditor().GraphNodePosition(3U);
    report.Check(outside.has_value() && outside->first == 320 && outside->second == 20,
        "P1.26 encountered node is not added to comment drag group");
    report.Check(context.CancelMaterialGraphInteractions() && !context.MaterialEditor().Dirty() &&
            context.MaterialEditor().GraphCommentPosition(10U)->first == 0 && !context.CanUndoSceneCommand(),
        "P1.25 capture-loss rollback restores comment drag without history");

    report.Check(context.ExpandMaterialGraphComposite(materialId, 20U) &&
            !context.MaterialEditor().GraphCompositeSubgraph(20U)->collapsed && context.UndoSceneCommand() &&
            context.MaterialEditor().GraphCompositeSubgraph(20U)->collapsed && context.RedoSceneCommand(),
        "P1.30 production Expand supports Undo/Redo");

    static_cast<void>(context.SelectMaterialGraphNode(2U));
    report.Check(context.BeginMaterialGraphNodeRenameEdit(materialId, 2U), "P1.31 begin node rename");
    context.ClearMaterialGraphNodeRenameEditText();
    context.InsertMaterialGraphNodeRenameEditText("Committed Name");
    static_cast<void>(context.SelectMaterialGraphNode(3U));
    report.Check(!context.IsMaterialGraphNodeRenameEditing() && context.MaterialEditor().GraphNodeDisplayName(2U) == "Committed Name",
        "P1.31 selection change commits rename into material history");
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
    out << "Suites: Project Settings + Plugins + Gameplay loop + Script editor/attach/log + Hierarchy commands + Selection transform + Prefab placement + Material graph context menu + Material graph panel canvas hit-test + Material graph color watcher + Material graph texture nodes + Material graph dense node layout + Material graph visual redesign + Material graph canvas clipping\n";
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
    RunSuiteInScratch(report, "material_graph_context_menu", &RunMaterialGraphContextMenuSuite);
    RunSuiteInScratch(report, "material_graph_panel_canvas_hit_test", &RunMaterialGraphPanelCanvasHitTestSuite);
    RunSuiteInScratch(report, "material_graph_color_watcher", &RunMaterialGraphColorWatcherSuite);
    RunSuiteInScratch(report, "material_graph_texture_nodes", &RunMaterialGraphTextureNodeSuite);
    RunSuiteInScratch(report, "material_graph_dense_node_layout", &RunMaterialGraphDenseNodeLayoutSuite);
    RunSuiteInScratch(report, "material_graph_first_node_checkpoint", &RunMaterialGraphFirstNodeVisualCheckpointSuite);
    RunSuiteInScratch(report, "material_graph_visual_redesign", &RunMaterialGraphVisualRedesignSuite);
    RunSuiteInScratch(report, "material_graph_canvas_clip", &RunMaterialGraphCanvasClipSuite);
    RunSuiteInScratch(report, "material_graph_interaction_lifecycle", &RunMaterialGraphInteractionLifecycleSuite);
    RunSuiteInScratch(report, "material_editor_global_save", &RunMaterialEditorGlobalSaveSuite);
    RunSuiteInScratch(report, "inspector_material_drop_target", &RunInspectorMaterialDropTargetSuite);
    RunSuiteInScratch(report, "inspector_light_component", &RunInspectorLightComponentSuite);
    RunSuiteInScratch(report, "prefab_placement", &RunPrefabPlacementSuite);
    RunSuiteInScratch(report, "script_log", &RunScriptLogSuite);
    RunSuiteInScratch(report, "plugins", &RunPluginsPanelSuite);
    WriteReport(reportPath, report);
    return report.Ok() ? 0 : 1;
}

} // namespace kb::editor

#endif
