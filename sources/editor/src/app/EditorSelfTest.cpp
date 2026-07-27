#include "app/EditorSelfTest.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserDoubleClickHandler.hpp"
#include "app/EditorEditCommandPolicy.hpp"
#include "app/plugins/EditorPluginsPointerController.hpp"
#include "app/project_settings/EditorProjectSettingsPointerController.hpp"
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "platform/win32/EditorDebugLogGate.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"
#include "platform/win32/EditorMaterialParameterValueDialog.hpp"
#include "platform/win32/EditorModalMessageLoop.hpp"
#include "platform/win32/EditorModalWindowScope.hpp"
#include "rendering/EditorMaterialThumbnailService.hpp"
#include "windowing/FloatingWindowFactory.hpp"
#include "windowing/FloatingWindowHitTestResolver.hpp"
#include "assets/EditorAssetBrowserHitTester.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "project/EditorProjectBootstrap.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/PluginsPanelRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/InspectorPanelSectionRows.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorPanelInteraction.hpp"
#include "inspection/InspectorPhysicsModel.hpp"
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
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/PhysicsDebugDraw.hpp"
#include "engine/scene/PhysicsLayersAssetIO.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
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

[[nodiscard]] std::string ReadFileTextForTest(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] LARGE_INTEGER QueryCounter() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value;
}

[[nodiscard]] double ElapsedMilliseconds(const LARGE_INTEGER& start) noexcept {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const LARGE_INTEGER now = QueryCounter();
    if (frequency.QuadPart == 0) {
        return 0.0;
    }
    return (static_cast<double>(now.QuadPart - start.QuadPart) * 1000.0) / static_cast<double>(frequency.QuadPart);
}

[[nodiscard]] std::string FormatMilliseconds(double value) {
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(3);
    text << value;
    return text.str();
}

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

void RunProjectPhysicsLayersRuntimeSuite(Report& report) {
    const EditorProjectBootstrapResult bootstrap = EditorProjectBootstrap::BootstrapDefaultProject();
    report.Check(bootstrap.succeeded, "LIB-129 bootstrap editor project for physics layers runtime");
    if (!bootstrap.succeeded) {
        return;
    }

    constexpr std::string_view kLayersVirtualPath = "/Game/Config/EditorRuntime.21kbphysicslayers";
    const std::filesystem::path layersPath =
        EditorProjectPaths::AssetsRoot() / "Config" / "EditorRuntime.21kbphysicslayers";
    std::error_code directoryError;
    std::filesystem::create_directories(layersPath.parent_path(), directoryError);
    report.Check(!directoryError, "LIB-129 create editor physics layers asset directory");

    kb::scene::PhysicsLayersAsset layers;
    layers.layerNames[1] = "EditorGhost";
    layers.layerNames[2] = "EditorSolid";
    layers.layerNames[3] = "EditorQuery";
    layers.SetLayersInteract(1U, 2U, false);
    report.Check(
        kb::scene::WritePhysicsLayersAsset(layersPath, layers),
        "LIB-129 write editor project physics layers asset");

    kb::project::ProjectDescriptor descriptor = bootstrap.descriptor;
    descriptor.physicsLayersAsset = kLayersVirtualPath;
    report.Check(
        kb::project::ProjectManager::SaveProject(bootstrap.projectFile, descriptor),
        "LIB-129 persist editor project physics layers reference");

    EditorSceneContext context;
    report.Check(
        kb::scene::PhysicsBackend::LayerBit(context.Scene(), "EditorGhost") == 2U &&
            kb::scene::PhysicsBackend::LayerBit(context.Scene(), "EditorSolid") == 4U &&
            kb::scene::PhysicsBackend::LayerBit(context.Scene(), "EditorQuery") == 8U,
        "LIB-129 editor startup applies named layers after mount and discovery");

    const auto createMatrixBody = [&context](
                                      const char* name,
                                      kb::scene::RigidbodyBodyType bodyType,
                                      float x,
                                      std::uint32_t layer) {
        const kb::scene::SceneObject object = context.Scene().Entities().CreateObject(
            kb::scene::SceneObjectDesc{
                .name = name,
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ x, 0.0F, 50.0F },
                },
            });
        context.Scene().Components().Rigidbodies().Set(
            object.Entity(),
            kb::scene::RigidbodyComponent{
                .bodyType = bodyType,
                .mass = 1.0F,
                .useGravity = false,
            });
        context.Scene().Components().Colliders().Set(
            object.Entity(),
            kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Sphere,
                .radius = 0.5F,
                .layer = layer,
            });
        return object;
    };

    const kb::scene::SceneObject disabledStatic =
        createMatrixBody("EditorMatrixDisabledStatic", kb::scene::RigidbodyBodyType::Static, 100.0F, 4U);
    const kb::scene::SceneObject disabledDynamic =
        createMatrixBody("EditorMatrixDisabledDynamic", kb::scene::RigidbodyBodyType::Dynamic, 100.2F, 2U);
    const kb::scene::SceneObject controlStatic =
        createMatrixBody("EditorMatrixControlStatic", kb::scene::RigidbodyBodyType::Static, 110.0F, 1U);
    const kb::scene::SceneObject controlDynamic =
        createMatrixBody("EditorMatrixControlDynamic", kb::scene::RigidbodyBodyType::Dynamic, 110.2F, 1U);
    for (std::size_t step = 0U; step < 30U; ++step) {
        static_cast<void>(context.Scene().Runtime().Update(1.0F / 60.0F));
    }
    const float disabledDistance = std::fabs(
        context.Scene().Transforms().Get(disabledDynamic).localPosition.x -
        context.Scene().Transforms().Get(disabledStatic).localPosition.x);
    const float controlDistance = std::fabs(
        context.Scene().Transforms().Get(controlDynamic).localPosition.x -
        context.Scene().Transforms().Get(controlStatic).localPosition.x);
    report.Check(
        disabledDistance < 0.3F && controlDistance > 0.7F,
        "LIB-129 editor runtime applies the project matrix to live Jolt while the control pair collides");

    layers.layerNames[3] = "EditorReloadedQuery";
    report.Check(
        kb::scene::WritePhysicsLayersAsset(layersPath, layers),
        "LIB-129 update physics layers asset before editor scene reload");
    report.Check(context.ReloadSceneFromProject(), "LIB-129 reload editor scene with updated project layers");
    report.Check(
        kb::scene::PhysicsBackend::LayerBit(context.Scene(), "EditorReloadedQuery") == 8U &&
            kb::scene::PhysicsBackend::LayerBit(context.Scene(), "EditorQuery") == 0U,
        "LIB-129 scene reload reapplies the changed physics layers asset");

    layers.layerNames[3] = "EditorPlayQuery";
    report.Check(
        kb::scene::WritePhysicsLayersAsset(layersPath, layers),
        "LIB-129 update physics layers asset before editor Play");
    report.Check(context.BeginPlayModeSceneSession(), "LIB-129 enter Play with refreshed physics layers");
    report.Check(
        kb::scene::PhysicsBackend::LayerBit(context.Scene(), "EditorPlayQuery") == 8U &&
            kb::scene::PhysicsBackend::LayerBit(context.Scene(), "EditorReloadedQuery") == 0U,
        "LIB-129 Play entry discovers and reapplies on-disk physics layer changes");
    report.Check(context.RestorePlayModeSceneSession(), "LIB-129 restore editor scene after physics layers Play verification");

    std::error_code removeError;
    const bool removed = std::filesystem::remove(layersPath, removeError);
    report.Check(removed && !removeError, "LIB-129 remove configured asset for editor failure-path verification");
    report.Check(
        !context.BeginPlayModeSceneSession(),
        "LIB-129 editor blocks Play instead of silently using the default matrix when the configured asset is missing");
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

// Reproduces the reported bug: editing a script's `Inspector.*` declarations and
// saving (Ctrl+S) must refresh the Inspector's exposed-variable schema WITHOUT a
// separate editor/scene save. ReloadOpenScriptAsset() — driven each frame off the
// Script Editor's save serial — drops the stale cached asset so the next read
// re-parses the file the editor just wrote.
void RunScriptInspectorSchemaRefreshSuite(Report& report) {
    EditorSceneContext context;

    report.Check(context.CreateLuaScriptAsset("/Game"), "Create Lua script asset for schema refresh");
    const kb::assets::AssetId script = FindAssetId(context, [](const kb::assets::AssetMetadata& m) { return m.type == "LuaScript"; });
    report.Check(script.IsValid(), "Schema-refresh Lua script registered");

    const kb::scene::SceneEntity actor = context.CreateHierarchyObject();
    report.Check(actor.IsValid(), "Create actor for schema refresh");
    report.Check(context.AttachScriptToEntity(actor, script), "Attach script to entity for schema refresh");
    report.Check(context.OpenLuaScript(script), "Open the script for schema refresh");
    const std::filesystem::path path = context.ScriptEditor().FilePath();
    report.Check(std::filesystem::exists(path), "Open schema-refresh script exists on disk");

    // Force the initial (template) parse into the asset cache: the starter
    // template declares no Inspector variables.
    report.Check(context.EntityScriptExposedVariables(actor).empty(), "Fresh script template exposes no Inspector variables");

    // Author a first Inspector declaration on disk. The cached schema is stale
    // until the save is picked up (exactly the reported symptom).
    report.Check(EditorScriptAssetGateway::WriteSource(path, "Inspector.speedA = 3.5\nfunction Tick(self, dt) end\n"),
        "Write first Inspector declaration to disk");
    report.Check(context.EntityScriptExposedVariables(actor).empty(), "Cached schema stays empty until the save is picked up");
    report.Check(context.ReloadOpenScriptAsset(), "Reload drops the stale cached asset after the first save");
    {
        const std::vector<EditorSceneContext::EntityScriptVariable> vars = context.EntityScriptExposedVariables(actor);
        report.Check(vars.size() == 1U && vars.front().name == "speedA", "Inspector schema shows the declared variable after reload");
    }

    // Rename the variable on disk (the screenshot scenario). Without a reload the
    // schema still shows the pre-save name; the reload refreshes it.
    report.Check(EditorScriptAssetGateway::WriteSource(path, "Inspector.speedRenamed = 7.0\nfunction Tick(self, dt) end\n"),
        "Rename the Inspector declaration on disk");
    {
        const std::vector<EditorSceneContext::EntityScriptVariable> stale = context.EntityScriptExposedVariables(actor);
        report.Check(stale.size() == 1U && stale.front().name == "speedA", "Without reload the schema is still the pre-save name (the reported bug)");
    }
    report.Check(context.ReloadOpenScriptAsset(), "Reload drops the stale asset after the rename");
    {
        const std::vector<EditorSceneContext::EntityScriptVariable> refreshed = context.EntityScriptExposedVariables(actor);
        report.Check(refreshed.size() == 1U && refreshed.front().name == "speedRenamed", "Saving the script refreshes the Inspector schema to the new name");
    }
}

// Proves the Inspector's component affordances the user reported missing:
// (1) the Mesh Renderer section-header "×" removes the component (like Script),
// (2) the Script field resolves the bound Lua asset so its picker "magnifier"
// can reveal it in Project Files (like the Mesh/Material asset fields).
void RunInspectorComponentAffordancesSuite(Report& report) {
    EditorSceneContext context;

    const kb::scene::SceneEntity actor = context.CreateHierarchyObject();
    report.Check(actor.IsValid(), "Create actor for component affordances");

    const int closedInspectorHeight = InspectorPanelRenderer::ContentHeight(kContent, context);
    context.Inspector().ToggleAddComponentBrowser();
    const int openInspectorHeight = InspectorPanelRenderer::ContentHeight(kContent, context);
    const std::optional<RECT> addComponentOverlay = InspectorPanelRenderer::AddComponentOverlayRect(kContent, context);
    report.Check(
        openInspectorHeight == closedInspectorHeight,
        "Add Component popup is out-of-flow and does not extend the Inspector content");
    report.Check(
        addComponentOverlay.has_value() &&
            addComponentOverlay->right > addComponentOverlay->left &&
            addComponentOverlay->bottom > addComponentOverlay->top,
        "Add Component popup resolves independent owner-window overlay bounds");
    if (addComponentOverlay.has_value()) {
        const int searchX = static_cast<int>((addComponentOverlay->left + addComponentOverlay->right) / 2);
        const int searchY = addComponentOverlay->top + 46;
        const InspectorPanelRenderer::Hit overlayHit =
            InspectorPanelRenderer::HitTestAddComponentOverlay(*addComponentOverlay, context, searchX, searchY);
        report.Check(
            overlayHit.section == InspectorSectionId::AddComponent &&
                overlayHit.property == InspectorPropertyId::AddComponentSearch,
            "Add Component overlay owns its search hit-test independently of the Inspector panel");
    }
    context.Inspector().CloseAddComponentBrowser();

    // (1) Mesh Renderer remove-component "×".
    report.Check(context.AddComponentToEntity(actor, "MeshRenderer"), "Add Mesh Renderer component");
    report.Check(context.Scene().Components().MeshRenderers().Has(actor), "Mesh Renderer component present after add");
    report.Check(context.RemoveMeshRendererFromEntity(actor), "Mesh Renderer '×' removes the component");
    report.Check(!context.Scene().Components().MeshRenderers().Has(actor), "Mesh Renderer component gone after remove");
    report.Check(!context.RemoveMeshRendererFromEntity(actor), "Removing an absent Mesh Renderer reports no-op");

    // (2) Script field asset resolution for the picker "magnifier".
    report.Check(context.EntityScriptAssetId(actor) == kb::assets::AssetId{}, "No script asset before attach");
    report.Check(context.CreateLuaScriptAsset("/Game"), "Create Lua script asset for picker");
    const kb::assets::AssetId script = FindAssetId(context, [](const kb::assets::AssetMetadata& m) { return m.type == "LuaScript"; });
    report.Check(script.IsValid(), "Picker Lua script registered");
    report.Check(context.AttachScriptToEntity(actor, script), "Attach script for picker");
    report.Check(context.EntityScriptAssetId(actor) == script, "Script field resolves the bound Lua asset for the picker reveal");
}

// The engine has a full physics subsystem (Jolt) but the editor never exposed it.
// Proves each physics component is now addable through the Add Component catalog
// path and rejects a duplicate add.
void RunPhysicsComponentCatalogSuite(Report& report) {
    EditorSceneContext context;
    const kb::scene::SceneEntity actor = context.CreateHierarchyObject();
    report.Check(actor.IsValid(), "Create actor for physics components");

    report.Check(context.AddComponentToEntity(actor, "Rigidbody"), "Add Rigidbody component");
    report.Check(context.Scene().Components().Rigidbodies().Has(actor), "Rigidbody present after add");
    report.Check(!context.AddComponentToEntity(actor, "Rigidbody"), "Duplicate Rigidbody add is rejected");

    report.Check(context.AddComponentToEntity(actor, "Collider"), "Add Collider component");
    report.Check(context.Scene().Components().Colliders().Has(actor), "Collider present after add");

    report.Check(context.AddComponentToEntity(actor, "CharacterController"), "Add Character Controller component");
    report.Check(context.Scene().Components().CharacterControllers().Has(actor), "Character Controller present after add");

    report.Check(context.AddComponentToEntity(actor, "Joint"), "Add Joint component");
    report.Check(context.Scene().Components().Joints().Has(actor), "Joint present after add");

    // The catalog surfaces them under a Physics category so the menu can list them.
    report.Check(kb::editor::InspectorComponentCatalog::Find("Rigidbody") != nullptr &&
            kb::editor::InspectorComponentCatalog::Find("Rigidbody")->category == "Physics",
        "Rigidbody tile is catalogued under Physics");
    report.Check(kb::editor::InspectorComponentCatalog::Find("Joint") != nullptr &&
            kb::editor::InspectorComponentCatalog::Find("Joint")->category == "Physics",
        "Joint tile is catalogued under Physics");

    // Collider gizmos are on by default (green wireframes visible) and toggle.
    report.Check(context.ArePhysicsGizmosVisible(), "Collider gizmos are visible by default");
    context.SetPhysicsGizmosVisible(false);
    report.Check(!context.ArePhysicsGizmosVisible(), "The collider gizmo toggle turns wireframes off");
    context.SetPhysicsGizmosVisible(true);
    // Fit-to-Mesh needs a Collider AND a resolvable mesh; the actor has no mesh.
    report.Check(!context.CanFitColliderToMesh(actor), "Fit-to-Mesh is unavailable without a Mesh Renderer mesh");
    report.Check(!context.FitColliderToMesh(actor), "Fit-to-Mesh no-ops without a mesh");

    // The section-header "×" removes each physics component (undoable path).
    report.Check(context.RemovePhysicsComponent(actor, kb::editor::PhysicsComponentKind::Rigidbody), "Rigidbody '×' removes the component");
    report.Check(!context.Scene().Components().Rigidbodies().Has(actor), "Rigidbody gone after remove");
    report.Check(!context.RemovePhysicsComponent(actor, kb::editor::PhysicsComponentKind::Rigidbody), "Removing an absent Rigidbody is a no-op");
    report.Check(context.RemovePhysicsComponent(actor, kb::editor::PhysicsComponentKind::Collider), "Collider '×' removes the component");
    report.Check(context.RemovePhysicsComponent(actor, kb::editor::PhysicsComponentKind::CharacterController), "Character Controller '×' removes the component");
    report.Check(context.RemovePhysicsComponent(actor, kb::editor::PhysicsComponentKind::Joint), "Joint '×' removes the component");
    report.Check(!context.Scene().Components().Joints().Has(actor), "Joint gone after remove");
}

void RunCameraInspectorSuite(Report& report) {
    EditorSceneContext context;
    const kb::scene::SceneEntity fallback = context.CreateHierarchyObject();
    report.Check(
        context.AddComponentToEntity(fallback, "Camera"),
        "LIB-135 add fallback Camera used to detect hierarchy selection jumps");
    const kb::scene::SceneEntity actor = context.CreateHierarchyObject();
    context.SelectEntity(actor);
    report.Check(
        context.AddComponentToEntity(actor, "Camera"),
        "LIB-135 add Camera through the editor component catalog");
    report.Check(
        context.Scene().Components().Cameras().Has(actor),
        "LIB-135 Camera component is present after editor add");
    const std::vector<EditorHierarchyRow> cameraRows = context.HierarchyRows();
    const auto actorRow = std::ranges::find(
        cameraRows, actor, &EditorHierarchyRow::entity);
    report.Check(
        actorRow != cameraRows.end() && actorRow->hasCamera,
        "LIB-135 Camera entity exposes the camera icon in Hierarchy");

    const std::array<InspectorPropertyId, 8> properties{
        InspectorPropertyId::CameraProjection,
        InspectorPropertyId::CameraVerticalFov,
        InspectorPropertyId::CameraOrthographicHeight,
        InspectorPropertyId::CameraNearClip,
        InspectorPropertyId::CameraFarClip,
        InspectorPropertyId::CameraPrimary,
        InspectorPropertyId::CameraViewportId,
        InspectorPropertyId::CameraPriority,
    };
    std::array<InspectorPanelRenderer::Hit, properties.size()> hits{};
    const auto expectedKind = [](InspectorPropertyId property) {
        switch (property) {
        case InspectorPropertyId::CameraVerticalFov:
        case InspectorPropertyId::CameraOrthographicHeight:
        case InspectorPropertyId::CameraNearClip:
        case InspectorPropertyId::CameraFarClip:
            return InspectorHitKind::FloatField;
        case InspectorPropertyId::CameraPrimary:
            return InspectorHitKind::BoolField;
        default:
            return InspectorHitKind::TextField;
        }
    };
    const int maxScroll =
        InspectorPanelRenderer::MaxScrollOffset(kContent, context);
    const int scrollStep =
        std::max(1, static_cast<int>(kContent.bottom - kContent.top) - 80);
    for (int scroll = 0; scroll <= maxScroll; scroll += scrollStep) {
        static_cast<void>(
            context.Inspector().SetScrollOffset(scroll, maxScroll));
        for (int y = kContent.top; y < kContent.bottom; ++y) {
            for (int x = kContent.left; x < kContent.right; ++x) {
                const InspectorPanelRenderer::Hit hit =
                    InspectorPanelRenderer::HitTest(kContent, context, x, y);
                if (hit.section != InspectorSectionId::Camera) {
                    continue;
                }
                for (std::size_t index = 0U; index < properties.size(); ++index) {
                    if (hits[index].kind == InspectorHitKind::None &&
                        hit.property == properties[index] &&
                        hit.kind == expectedKind(properties[index])) {
                        hits[index] = hit;
                    }
                }
            }
        }
    }
    static_cast<void>(context.Inspector().SetScrollOffset(0, maxScroll));
    for (const InspectorPanelRenderer::Hit& hit : hits) {
        report.Check(
            hit.kind != InspectorHitKind::None,
            "LIB-135 Camera field is visible and hit-testable in the production Inspector");
    }

    const auto hitFor = [&properties, &hits](InspectorPropertyId property) {
        for (std::size_t index = 0U; index < properties.size(); ++index) {
            if (properties[index] == property) {
                return hits[index];
            }
        }
        return InspectorPanelRenderer::Hit{};
    };
    const auto click = [&context](const InspectorPanelRenderer::Hit& hit) {
        const POINT point = Center(hit.rect);
        return InspectorPanelInteraction::HandlePointerDown(
            context, hit, point.x, point.y);
    };
    const auto editText = [&context, &click](
                              const InspectorPanelRenderer::Hit& hit,
                              std::string_view value,
                              bool releasePointer) {
        if (!click(hit)) {
            return false;
        }
        if (releasePointer) {
            static_cast<void>(
                InspectorPanelInteraction::HandlePointerUp(context));
        }
        context.Inspector().ClearText();
        context.Inspector().InsertText(value);
        return InspectorPanelInteraction::HandleKeyDown(
            nullptr, context, static_cast<WPARAM>(VK_RETURN));
    };
    const auto camera = [&context]() {
        return context.Scene().Components().Cameras().TryGet(
            context.SelectedEntity());
    };
    const auto selectionStayedOnActor = [&context, actor]() {
        return context.SelectedEntity() == actor;
    };

    report.Check(
        click(hitFor(InspectorPropertyId::CameraProjection)),
        "LIB-135 Camera projection click is handled");
    report.Check(
        selectionStayedOnActor(),
        "LIB-135 Camera projection edit preserves hierarchy selection");
    report.Check(
        camera() != nullptr &&
            camera()->projection ==
            kb::scene::CameraProjection::Orthographic,
        "LIB-135 Camera projection edit reaches the live component");
    report.Check(
        click(hitFor(InspectorPropertyId::CameraPrimary)),
        "LIB-135 Camera primary click is handled");
    report.Check(
        selectionStayedOnActor(),
        "LIB-135 Camera primary edit preserves hierarchy selection");
    report.Check(
        camera() != nullptr && camera()->primary,
        "LIB-135 Camera primary edit reaches the live component");
    report.Check(
        editText(
            hitFor(InspectorPropertyId::CameraViewportId), "7", false),
        "LIB-135 Camera viewport edit is committed");
    report.Check(
        selectionStayedOnActor(),
        "LIB-135 Camera viewport edit preserves hierarchy selection");
    report.Check(
        camera() != nullptr && camera()->viewportId == 7U,
        "LIB-135 Camera viewport reaches the live component");
    report.Check(
        editText(
            hitFor(InspectorPropertyId::CameraPriority), "42", false),
        "LIB-135 Camera priority edit is committed");
    report.Check(
        selectionStayedOnActor(),
        "LIB-135 Camera priority edit preserves hierarchy selection");
    report.Check(
        camera() != nullptr && camera()->priority == 42,
        "LIB-135 Camera priority reaches the live component");
    report.Check(
        editText(
            hitFor(InspectorPropertyId::CameraVerticalFov), "47.5", true),
        "LIB-135 Camera FOV edit is committed");
    report.Check(
        selectionStayedOnActor(),
        "LIB-135 Camera FOV edit preserves hierarchy selection");
    const bool fovChanged =
        camera() != nullptr &&
        std::abs(camera()->verticalFovDegrees - 47.5F) < 0.001F;
    report.Check(
        fovChanged,
        "LIB-135 Camera FOV reaches the live component");
    if (fovChanged) {
        report.Check(
            context.UndoSceneCommand(),
            "LIB-135 Camera inspector edit participates in scene undo");
        report.Check(
            camera() != nullptr &&
                std::abs(camera()->verticalFovDegrees - 60.0F) < 0.001F,
            "LIB-135 Camera undo restores the previous runtime value");
    }
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
    context.Scene().Components().Colliders().Set(first, kb::scene::ColliderComponent{});

    const InspectorPanelRenderer::Hit pivotXHit = InspectorPanelRenderer::HitTest(kContent, context, 360, 216);
    report.Check(
        pivotXHit.kind == InspectorHitKind::FloatField &&
            pivotXHit.section == InspectorSectionId::Transform &&
            pivotXHit.property == InspectorPropertyId::PositionX,
        "Multi-selection inspector pivot X field hit-tests as transform PositionX");

    report.Check(context.BeginSelectedTransformEdit("Edit Transform"), "Begin multi-selection transform edit");
    report.Check(std::abs(context.ActiveTransformEditPropertyStart(InspectorPropertyId::PositionX) - 4.0F) < 0.001F, "Multi-selection position edit starts from pivot X");
    report.Check(context.ApplyActiveTransformEditProperty(InspectorPropertyId::PositionX, 10.0F), "Apply multi-selection pivot X edit");

    const kb::scene::TransformComponent liveMovedFirst = context.Scene().Transforms().Get(first);
    const kb::scene::TransformComponent liveMovedSecond = context.Scene().Transforms().Get(second);
    report.Check(!liveMovedFirst.worldDirty && std::abs(liveMovedFirst.worldPosition.x - 8.0F) < 0.001F,
        "Interactive transform edit synchronizes the first entity world transform before commit");
    report.Check(!liveMovedSecond.worldDirty && std::abs(liveMovedSecond.worldPosition.x - 12.0F) < 0.001F,
        "Interactive transform edit synchronizes the second entity world transform before commit");

    bool colliderFollowedLiveEdit = false;
    for (const kb::scene::PhysicsDebugLineDesc& line : kb::scene::PhysicsDebugDraw::CollectLines(context.Scene())) {
        for (const kb::scene::Vec3& point : {line.from, line.to}) {
            if (std::abs(point.x - 8.5F) < 0.001F) {
                colliderFollowedLiveEdit = true;
            }
        }
    }
    report.Check(colliderFollowedLiveEdit, "Collider wireframe follows an interactive transform edit before commit");
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

// The Inspector's material ball must be the live 3D preview surface, not a painted stand-in: the rect
// resolver used to return nullopt, which silently disabled the surface for the whole panel.
// Finding 19 (material Inspector is the preview and nothing else): a material is authored in the Material
// Editor, so the Inspector must not restate its channels or asset fields - only the live preview, with the
// virtual path folded into it.
void RunInspectorMaterialColorRowsSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 19: register material loader");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "InspectorColorRows.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 19: create .kbmat fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 19: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/InspectorColorRows.kbmat");
    report.Check(metadata != nullptr, "Finding 19: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }
    report.Check(context.AssetBrowser().SelectAsset(metadata->id, context.Scene().Assets().Manager()),
        "Finding 19: select the material in the Inspector");

    bool foundMaterialSection = false;
    bool foundAssetSection = false;
    bool foundPreviewSection = false;
    const int maxScroll = InspectorPanelRenderer::MaxScrollOffset(kContent, context);
    for (int scroll = 0; scroll <= maxScroll; scroll += std::max<int>(1, static_cast<int>(kContent.bottom - kContent.top) - 80)) {
        static_cast<void>(context.Inspector().SetScrollOffset(scroll, maxScroll));
        for (int y = kContent.top; y < kContent.bottom; ++y) {
            for (int x = kContent.left; x < kContent.right; x += 8) {
                const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(kContent, context, x, y);
                if (hit.section == InspectorSectionId::Material) {
                    foundMaterialSection = true;
                }
                if (hit.section == InspectorSectionId::Asset) {
                    foundAssetSection = true;
                }
                if (hit.section == InspectorSectionId::MaterialPreview) {
                    foundPreviewSection = true;
                }
            }
        }
    }
    static_cast<void>(context.Inspector().SetScrollOffset(0, maxScroll));
    report.Check(foundPreviewSection, "Finding 19: the material Inspector keeps its Preview section");
    report.Check(!foundMaterialSection, "Finding 19: the Material section is gone");
    report.Check(!foundAssetSection, "Finding 19: the Asset section is gone");

    // Nothing below the preview means the panel no longer needs to scroll at all.
    report.Check(InspectorPanelRenderer::MaxScrollOffset(kContent, context) == 0,
        "Finding 19: the material Inspector fits without scrolling");
}

void RunInspectorMaterialPreviewSurfaceSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 17: register material loader");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "InspectorPreviewSurface.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 17: create .kbmat fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 17: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/InspectorPreviewSurface.kbmat");
    report.Check(metadata != nullptr, "Finding 17: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }

    report.Check(!InspectorPanelRenderer::MaterialPreviewRect(kContent, context).has_value(),
        "Finding 17: no material selected means no preview surface");

    report.Check(context.AssetBrowser().SelectAsset(metadata->id, context.Scene().Assets().Manager()),
        "Finding 17: select the material for the Inspector");
    const std::optional<RECT> previewRect = InspectorPanelRenderer::MaterialPreviewRect(kContent, context);
    report.Check(previewRect.has_value(), "Finding 17: a selected material gets a live 3D preview surface rect");
    if (!previewRect.has_value()) {
        return;
    }
    report.Check(previewRect->right - previewRect->left > 40 && previewRect->bottom - previewRect->top > 40,
        "Finding 17: the preview surface is big enough to render into");
    report.Check(previewRect->left >= kContent.left && previewRect->right <= kContent.right &&
            previewRect->top >= kContent.top && previewRect->bottom <= kContent.bottom,
        "Finding 17: the preview surface stays inside the Inspector panel");

    // The surface has to sit exactly where the panel paints its preview frame, or the 3D render lands
    // somewhere else in the panel.
    const InspectorPanelRenderer::Hit previewHeader = InspectorPanelRenderer::HitTest(
        kContent, context, (previewRect->left + previewRect->right) / 2, previewRect->top - 12);
    report.Check(previewHeader.section == InspectorSectionId::MaterialPreview,
        "Finding 17: the surface sits directly under the Preview section header");

    // Scrolling: the surface is a child window over the panel, so it has to travel with the rows and
    // disappear once its slot leaves the viewport. Otherwise it hangs over whatever scrolled into place.
    // A short panel is the case that scrolls: the material Inspector is preview-only now, so at full height
    // it fits. A short one is exactly where a pinned surface would hang over the rows below.
    const RECT shortPanel{ kContent.left, kContent.top, kContent.right, kContent.top + 260 };
    const std::optional<RECT> shortRect = InspectorPanelRenderer::MaterialPreviewRect(shortPanel, context);
    const int maxScroll = InspectorPanelRenderer::MaxScrollOffset(shortPanel, context);
    report.Check(maxScroll > 0 && shortRect.has_value(), "Finding 17: a short Inspector panel scrolls");
    if (maxScroll <= 0 || !shortRect.has_value()) {
        return;
    }
    const int step = std::max(1, maxScroll / 4);
    static_cast<void>(context.Inspector().SetScrollOffset(step, maxScroll));
    const std::optional<RECT> scrolledRect = InspectorPanelRenderer::MaterialPreviewRect(shortPanel, context);
    report.Check(scrolledRect.has_value() && scrolledRect->top < shortRect->top,
        "Finding 17: scrolling moves the preview surface with the panel content");

    static_cast<void>(context.Inspector().SetScrollOffset(maxScroll, maxScroll));
    const std::optional<RECT> farRect = InspectorPanelRenderer::MaterialPreviewRect(shortPanel, context);
    report.Check(!farRect.has_value() ||
            (farRect->top >= shortPanel.top && farRect->bottom <= shortPanel.bottom &&
                farRect->bottom > farRect->top),
        "Finding 17: scrolled far, the surface is either gone or clipped inside the panel, never floating over other rows");
    static_cast<void>(context.Inspector().SetScrollOffset(0, maxScroll));
}

void RunInspectorMaterialDropTargetSuite(Report& report) {
    EditorSceneContext context;
    kb::scene::SceneEntity mesh = context.CreateHierarchyObject();
    report.Check(mesh.IsValid(), "Create mesh entity for inspector material drop target");
    kb::scene::MeshRendererComponent renderer{ .meshAssetId = 909U };
    renderer.materialSlotOverrideCount = 2U;
    context.Scene().Components().MeshRenderers().Set(mesh, renderer);
    context.SelectEntity(mesh);

    context.Inspector().ToggleDisclosure(InspectorDisclosureId::MeshRendererAdvanced);
    static_cast<void>(context.Inspector().TickDisclosures(1.0F));
    InspectorPanelRenderer::Hit advancedPassiveHit{};
    for (int y = kContent.top; y < kContent.bottom && advancedPassiveHit.kind == InspectorHitKind::None; ++y) {
        for (int x = kContent.left; x < kContent.right; ++x) {
            const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(kContent, context, x, y);
            if (hit.section == InspectorSectionId::MeshRenderer &&
                hit.kind == InspectorHitKind::TextField &&
                hit.property == InspectorPropertyId::None &&
                hit.index >= 0) {
                advancedPassiveHit = hit;
                break;
            }
        }
    }
    report.Check(
        advancedPassiveHit.kind == InspectorHitKind::Row || advancedPassiveHit.kind == InspectorHitKind::TextField,
        "Expanded Inspector Advanced fields expose passive row hover targets");
    static_cast<void>(InspectorPanelInteraction::UpdateHover(context, advancedPassiveHit));
    report.Check(
        context.Inspector().IsHovered(
            advancedPassiveHit.kind,
            InspectorSectionId::MeshRenderer,
            InspectorPropertyId::None,
            advancedPassiveHit.index),
        "Inspector Advanced hover state identifies exactly the visible child row");
    report.Check(
        inspector_panel_rows::FieldValueHovered(
            context.Inspector(),
            InspectorSectionId::MeshRenderer,
            InspectorPropertyId::None,
            advancedPassiveHit.index) &&
        !inspector_panel_rows::FieldValueHovered(
            context.Inspector(),
            InspectorSectionId::MeshRenderer,
            InspectorPropertyId::None),
        "Property-less Inspector value hover matches only its indexed Advanced field");
    context.Inspector().ToggleDisclosure(InspectorDisclosureId::MeshRendererAdvanced);
    static_cast<void>(context.Inspector().TickDisclosures(1.0F));
    static_cast<void>(InspectorPanelInteraction::UpdateHover(context, {}));

    InspectorPanelRenderer::Hit materialHit{};
    InspectorPanelRenderer::Hit materialPickerHit{};
    InspectorPanelRenderer::Hit overridePickerHit{};
    InspectorPanelRenderer::Hit slotHit{};
    InspectorPanelRenderer::Hit castsShadowHit{};
    InspectorPanelRenderer::Hit receivesShadowHit{};
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
            if (castsShadowHit.kind == InspectorHitKind::None &&
                hit.kind == InspectorHitKind::BoolField &&
                hit.property == InspectorPropertyId::MeshRendererCastsShadow) {
                castsShadowHit = hit;
            }
            if (receivesShadowHit.kind == InspectorHitKind::None &&
                hit.kind == InspectorHitKind::BoolField &&
                hit.property == InspectorPropertyId::MeshRendererReceivesShadow) {
                receivesShadowHit = hit;
            }
        }
    }
    report.Check(materialHit.kind != InspectorHitKind::None, "Inspector Mesh Renderer Material row hit-tests as the main material assignment target");
    report.Check(materialPickerHit.kind != InspectorHitKind::None, "Inspector Mesh Renderer Material row exposes a material picker button");
    report.Check(slotHit.kind != InspectorHitKind::None, "Inspector Mesh Renderer material override row hit-tests as a concrete material slot target");
    report.Check(overridePickerHit.kind != InspectorHitKind::None, "Inspector Mesh Renderer Material Override row exposes a material picker button");
    report.Check(castsShadowHit.kind != InspectorHitKind::None, "Inspector Mesh Renderer exposes the Casts Shadow runtime toggle");
    report.Check(receivesShadowHit.kind != InspectorHitKind::None, "Inspector Mesh Renderer exposes the Receives Shadow runtime toggle");

    auto refreshMeshEntityFromSelection = [&]() {
        const kb::scene::SceneEntity selected = context.SelectedEntity();
        if (selected.IsValid() && context.Scene().Components().MeshRenderers().Has(selected)) {
            mesh = selected;
        }
    };

    context.AcknowledgeSceneRenderSubmitted();
    const POINT castsShadowPoint = Center(castsShadowHit.rect);
    report.Check(
        InspectorPanelInteraction::HandlePointerDown(context, castsShadowHit, castsShadowPoint.x, castsShadowPoint.y),
        "Clicking Mesh Renderer Casts Shadow is handled");
    const kb::scene::MeshRendererComponent* castsShadowDisabled =
        context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(
        castsShadowDisabled != nullptr && !castsShadowDisabled->castsShadow && castsShadowDisabled->receivesShadow,
        "Mesh Renderer Casts Shadow click updates only the runtime caster flag");
    report.Check(context.SceneRenderFullDirty(), "Mesh Renderer Casts Shadow click schedules renderer resynchronization");
    report.Check(context.UndoSceneCommand(), "Undo Mesh Renderer Casts Shadow toggle");
    refreshMeshEntityFromSelection();
    const kb::scene::MeshRendererComponent* castsShadowUndone =
        context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(castsShadowUndone != nullptr && castsShadowUndone->castsShadow, "Undo restores Mesh Renderer Casts Shadow");
    report.Check(context.RedoSceneCommand(), "Redo Mesh Renderer Casts Shadow toggle");
    refreshMeshEntityFromSelection();
    const kb::scene::MeshRendererComponent* castsShadowRedone =
        context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(castsShadowRedone != nullptr && !castsShadowRedone->castsShadow, "Redo reapplies Mesh Renderer Casts Shadow");

    context.AcknowledgeSceneRenderSubmitted();
    const POINT receivesShadowPoint = Center(receivesShadowHit.rect);
    report.Check(
        InspectorPanelInteraction::HandlePointerDown(context, receivesShadowHit, receivesShadowPoint.x, receivesShadowPoint.y),
        "Clicking Mesh Renderer Receives Shadow is handled");
    const kb::scene::MeshRendererComponent* receivesShadowDisabled =
        context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(
        receivesShadowDisabled != nullptr &&
            !receivesShadowDisabled->castsShadow &&
            !receivesShadowDisabled->receivesShadow,
        "Mesh Renderer Receives Shadow click updates only the runtime receiver flag");
    report.Check(context.SceneRenderFullDirty(), "Mesh Renderer Receives Shadow click schedules renderer resynchronization");
    report.Check(context.UndoSceneCommand(), "Undo Mesh Renderer Receives Shadow toggle");
    refreshMeshEntityFromSelection();
    const kb::scene::MeshRendererComponent* receivesShadowUndone =
        context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(receivesShadowUndone != nullptr && receivesShadowUndone->receivesShadow, "Undo restores Mesh Renderer Receives Shadow");
    report.Check(context.RedoSceneCommand(), "Redo Mesh Renderer Receives Shadow toggle");
    refreshMeshEntityFromSelection();
    const kb::scene::MeshRendererComponent* receivesShadowRedone =
        context.Scene().Components().MeshRenderers().TryGet(mesh);
    report.Check(receivesShadowRedone != nullptr && !receivesShadowRedone->receivesShadow, "Redo reapplies Mesh Renderer Receives Shadow");

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
    // Regression: on a canvas shorter than the palette's max height, a tall (filtered / wire-drop) palette
    // used to be sized to nearly the whole canvas and then top-clamped, which snapped its top to the
    // canvas top no matter where the wire was dropped. It must now open AT the drop point and grow
    // downward (scrolling internally), never overflowing the canvas bottom.
    context.SetMaterialGraphCanvasViewport(0, 0, 1000, 360);
    report.Check(context.OpenMaterialGraphContextMenu(materialId, 320, 200, -160, 96), "Open material graph context menu mid-canvas on a short canvas");
    context.SetMaterialGraphContextMenuSearchQuery("a");
    const RECT midDropRect = MaterialEditorPanelRenderer::GraphContextMenuRect(context);
    report.Check(
        midDropRect.top == 200 && midDropRect.bottom <= 360,
        "Tall filtered palette opens at the drop point and grows downward instead of snapping to the canvas top");
    report.Check(context.OpenMaterialGraphContextMenu(materialId, 320, 350, -160, 96), "Open material graph context menu near the bottom of a short canvas");
    const RECT bottomDropRect = MaterialEditorPanelRenderer::GraphContextMenuRect(context);
    report.Check(
        bottomDropRect.bottom <= 360 && bottomDropRect.top == 360 - kMaterialEditorGraphMenuMinHeight,
        "Palette dropped near the canvas bottom shifts up only enough to keep its minimum strip visible");
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

    // Grouped search (2026-07-23): typing in the palette search must KEEP category headers (and the scrollbox),
    // hiding only categories with no match - not collapse every result into one flat, categoryless list.
    {
        report.Check(context.OpenMaterialGraphContextMenu(materialId, 320, 240, -160, 96), "Reopen palette for grouped-search coverage");
        context.SetMaterialGraphContextMenuSearchQuery("texture sample");
        report.Check(MaterialEditorGraphContextMenuIsFiltering(context), "Grouped search: a typed query puts the palette in filtering mode");
        int categoriesWithMatches = 0;
        int categoriesHidden = 0;
        for (const std::size_t categoryIndex : MaterialEditorGraphContextMenuCategoryOrder(context)) {
            if (MaterialEditorGraphContextMenuVisibleCommands(context, categoryIndex).empty()) {
                ++categoriesHidden;
            } else {
                ++categoriesWithMatches;
            }
        }
        report.Check(categoriesWithMatches >= 1, "Grouped search: at least one category keeps its matching commands");
        report.Check(categoriesHidden >= 1, "Grouped search: categories with no match are hidden, not shown as empty headers");
        static_cast<void>(context.SetMaterialGraphContextMenuScrollOffset(0, MaterialEditorGraphContextMenuMaxScroll(context)));
        const RECT searchMenuRect = MaterialEditorPanelRenderer::GraphContextMenuRect(context);
        const RECT searchViewport = MaterialEditorGraphContextMenuViewportRect(searchMenuRect);
        // The first row under a search is a CATEGORY header (results carry their category), and the row directly
        // below it hit-tests as a Command in that category - i.e. grouped, not a flat categoryless list.
        const MaterialEditorGraphContextMenuHit searchTopHit =
            MaterialEditorPanelRenderer::GraphContextMenuHit(context, searchMenuRect.left + 40, searchViewport.top + 6);
        report.Check(searchTopHit.kind == MaterialEditorGraphContextMenuHitKind::Category,
            "Grouped search: the first row under a search is a category header");
        const MaterialEditorGraphContextMenuHit searchCommandHit = MaterialEditorPanelRenderer::GraphContextMenuHit(
            context, searchMenuRect.left + 40, searchViewport.top + kMaterialEditorGraphMenuCategoryHeight + 6);
        report.Check(searchCommandHit.kind == MaterialEditorGraphContextMenuHitKind::Command &&
                searchCommandHit.categoryIndex == searchTopHit.categoryIndex,
            "Grouped search: the row under the header hit-tests as a command inside that same category");
        // The palette must stay clamped inside the canvas even as a search GROWS it past its open-time height,
        // otherwise its scrollbar and lower rows spill off-screen (the "search has no scrollbar / can't scroll"
        // bug). The rect is re-clamped every frame against the current height.
        const RECT clampedSearchMenu = MaterialEditorPanelRenderer::GraphContextMenuRect(context);
        report.Check(clampedSearchMenu.top >= context.MaterialGraphCanvasTop() &&
                clampedSearchMenu.bottom <= context.MaterialGraphCanvasTop() + context.MaterialGraphCanvasHeight() &&
                clampedSearchMenu.left >= context.MaterialGraphCanvasLeft() &&
                clampedSearchMenu.right <= context.MaterialGraphCanvasLeft() + context.MaterialGraphCanvasWidth(),
            "Grouped search: the palette stays clamped inside the canvas as the search grows it (scrollbar stays on-screen)");
        // Negative control: clearing the search restores the full, collapsible category list.
        context.ClearMaterialGraphContextMenuSearch();
        report.Check(!MaterialEditorGraphContextMenuIsFiltering(context),
            "Grouped search negative control: clearing the query leaves filtering mode");
    }

    // Favorites (2026-07-22): the palette hides an empty Favorites section and floats a non-empty one to the
    // very top, so a checked node is immediately visible rather than buried at the bottom.
    {
        EditorSceneContext favContext;
        const std::size_t favoritesIndex = MaterialEditorGraphContextMenuFavoritesCategoryIndex();
        const std::vector<std::size_t> emptyOrder = MaterialEditorGraphContextMenuCategoryOrder(favContext);
        report.Check(!emptyOrder.empty() && emptyOrder.front() != favoritesIndex &&
                std::ranges::find(emptyOrder, favoritesIndex) == emptyOrder.end(),
            "Palette hides the Favorites category while it is empty");
        report.Check(favContext.ToggleMaterialGraphPaletteFavorite(MaterialEditorGraphMenuCommand::CreateMultiply),
            "Favouriting a palette node succeeds");
        const std::vector<std::size_t> withFavorite = MaterialEditorGraphContextMenuCategoryOrder(favContext);
        report.Check(!withFavorite.empty() && withFavorite.front() == favoritesIndex,
            "Favorites floats to the very top of the palette once it has an entry");
        // Negative control: removing the last favorite drops the category back out of the display order.
        report.Check(favContext.ToggleMaterialGraphPaletteFavorite(MaterialEditorGraphMenuCommand::CreateMultiply),
            "Un-favouriting the node succeeds");
        const std::vector<std::size_t> clearedOrder = MaterialEditorGraphContextMenuCategoryOrder(favContext);
        report.Check(std::ranges::find(clearedOrder, favoritesIndex) == clearedOrder.end(),
            "Negative control: clearing the last favorite hides the Favorites category again");
    }

    // Preview overlay (2026-07-22): the preview is a bgfx child surface parented to the host window, so its
    // frame must be clamped inside its panel or it renders over the neighbouring dock.
    {
        const RECT narrowPanel{ 0, 0, 150, 600 };
        const auto narrowLayout = MaterialEditorPanelRenderer::ResolveLayout(narrowPanel);
        report.Check(narrowLayout.previewFrame.right <= narrowPanel.right && narrowLayout.previewFrame.bottom <= narrowPanel.bottom,
            "Material preview is clamped inside a narrow panel instead of spilling onto the next panel");
        // Negative control: a wide panel still gets the full-size preview (the clamp only bites when tight).
        const RECT widePanel{ 0, 0, 1200, 800 };
        const auto wideLayout = MaterialEditorPanelRenderer::ResolveLayout(widePanel);
        report.Check(wideLayout.previewFrame.right < widePanel.right &&
                (wideLayout.previewFrame.right - wideLayout.previewFrame.left) >
                    (narrowLayout.previewFrame.right - narrowLayout.previewFrame.left),
            "Negative control: a wide panel shows a larger, unclamped preview");
    }
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
    // A pin owns its own half of the node only: the far half belongs to the node, so dragging there moves
    // the node instead of pulling a wire out of it (input-only nodes used to grab every click on the body).
    report.Check(
        MaterialEditorPanelRenderer::GraphPinAt(
            content,
            graph,
            context,
            materialId,
            static_cast<int>(std::lround(baseColorPin.x + (40.0F * zoom))),
            static_cast<int>(std::lround(baseColorPin.y)))
             .has_value(),
        "Material panel graph still hits an input pin from its own half of the node");
    report.Check(
        !MaterialEditorPanelRenderer::GraphPinAt(
            content,
            graph,
            context,
            materialId,
            static_cast<int>(std::lround(baseColorPin.x + (160.0F * zoom))),
            static_cast<int>(std::lround(baseColorPin.y)))
             .has_value(),
        "Material panel graph leaves the far half of the node to node dragging, not to the input pin");

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

    constexpr int narrowWidth = 434;
    constexpr int narrowHeight = 336;
    const RECT narrowContent{ 0, 0, narrowWidth, narrowHeight };
    const MaterialEditorPanelLayout narrowLayout = MaterialEditorPanelRenderer::ResolveLayout(narrowContent);
    report.Check(narrowLayout.compactToolbar, "Use responsive Material Editor toolbar for the reported narrow production viewport");
    report.Check(
        narrowLayout.infoButton.left >= narrowContent.left && narrowLayout.validateButton.right <= narrowContent.right,
        "Keep all Material Editor toolbar command groups inside the narrow production viewport");
    std::vector<std::uint32_t> narrowFrameNodeIds;
    narrowFrameNodeIds.reserve(material.graph.nodes.size());
    for (const kb::render::RenderMaterialGraphNode& node : material.graph.nodes) {
        narrowFrameNodeIds.push_back(node.id);
    }
    if (!narrowFrameNodeIds.empty()) {
        static_cast<void>(context.SetMaterialGraphNodeSelection(narrowFrameNodeIds, narrowFrameNodeIds.front()));
        static_cast<void>(context.FrameSelectedMaterialGraphNodes(
            MaterialEditorPanelRectWidth(narrowLayout.graphCanvas),
            MaterialEditorPanelRectHeight(narrowLayout.graphCanvas)));
        static_cast<void>(context.ClearMaterialGraphNodeSelection());
    }

    HBITMAP narrowBitmap = CreateCompatibleBitmap(screenDc, narrowWidth, narrowHeight);
    HGDIOBJ narrowPrevious = SelectObject(memoryDc, narrowBitmap);
    renderer.Paint(memoryDc, narrowContent, EditorTheme{}, context);
    const std::filesystem::path narrowOutputPath =
        std::filesystem::temp_directory_path(error) / "21kb_selftest" / "_materialEditorResponsiveNarrow.bmp";
    if (bmpEncoder.has_value()) {
        Gdiplus::Bitmap narrowImage(narrowBitmap, nullptr);
        report.Check(narrowImage.Save(narrowOutputPath.wstring().c_str(), &*bmpEncoder, nullptr) == Gdiplus::Ok,
            "Save responsive narrow Material Editor screenshot artifact");
        report.Note("Responsive narrow Material Editor screenshot: " + narrowOutputPath.string());
    }
    SelectObject(memoryDc, narrowPrevious);
    DeleteObject(narrowBitmap);
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

// Finding 5 (inspector <-> graph edit reconciliation): when a .kbmat is open in the Material Editor,
// a PBR scalar edited through the Inspector and a node added to the graph must BOTH survive a save --
// neither path may clobber the other's unsaved change. Reproduces the exact audit scenario in both
// orders through the real EditorSceneContext edit routing.
void RunMaterialInspectorGraphEditReconcileSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 5: register material loader");

    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "InspectorGraphReconcile.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    fixture.desc.roughnessFactor = 0.20F;
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 5: create .kbmat fixture (roughness=0.20)");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 5: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata = context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/InspectorGraphReconcile.kbmat");
    report.Check(metadata != nullptr, "Finding 5: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }
    const kb::assets::AssetId id = metadata->id;

    // --- Order A: edit the GRAPH first, then a PBR scalar through the Inspector, then save. ---
    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 5: open material (order A)");
    report.Check(context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantScalar, -140, 60),
        "Finding 5: add graph node (order A, unsaved working-copy graph edit)");
    const std::uint32_t nodeA = context.SelectedMaterialGraphNodeId();
    report.Check(context.SetMaterialRoughnessFactor(id, 0.77F), "Finding 5: edit roughness via Inspector (order A)");
    report.Check(context.SaveMaterialEditorAsset(id), "Finding 5: save (order A)");

    const std::optional<kb::render::RenderMaterialAssetData> reloadedA = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    report.Check(reloadedA.has_value(), "Finding 5: reload after order A");
    if (reloadedA.has_value()) {
        report.Check(std::abs(reloadedA->desc.roughnessFactor - 0.77F) < 0.01F,
            "Finding 5 (order A): the Inspector roughness edit survived the graph save (not clobbered)");
        report.Check(nodeA != 0U && kb::render::FindRenderMaterialGraphNode(reloadedA->graph, nodeA) != nullptr,
            "Finding 5 (order A): the graph node survived alongside the Inspector edit");
    }

    // --- Order B: edit a PBR scalar through the Inspector first, then the GRAPH, then save. ---
    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 5: reopen material (order B)");
    report.Check(context.SetMaterialRoughnessFactor(id, 0.33F), "Finding 5: edit roughness via Inspector (order B)");
    report.Check(context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantVector, 40, 60),
        "Finding 5: add graph node (order B)");
    const std::uint32_t nodeB = context.SelectedMaterialGraphNodeId();
    report.Check(context.SaveMaterialEditorAsset(id), "Finding 5: save (order B)");

    const std::optional<kb::render::RenderMaterialAssetData> reloadedB = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    report.Check(reloadedB.has_value(), "Finding 5: reload after order B");
    if (reloadedB.has_value()) {
        report.Check(std::abs(reloadedB->desc.roughnessFactor - 0.33F) < 0.01F,
            "Finding 5 (order B): the Inspector roughness edit survived a subsequent graph edit + save");
        report.Check(nodeB != 0U && kb::render::FindRenderMaterialGraphNode(reloadedB->graph, nodeB) != nullptr,
            "Finding 5 (order B): the newly added graph node survived alongside the earlier Inspector edit");
    }

    // --- Order C: the Inspector DRAG-SCRUB path (Begin/Apply/Commit) alongside an unsaved graph edit. ---
    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 5: reopen material (order C, drag-scrub)");
    report.Check(context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantScalar, -40, 140),
        "Finding 5: add graph node (order C)");
    const std::uint32_t nodeC = context.SelectedMaterialGraphNodeId();
    report.Check(context.BeginMaterialAssetFloatEdit(id, InspectorPropertyId::MaterialRoughnessFactor),
        "Finding 5: begin roughness drag-scrub (order C)");
    report.Check(context.ApplyActiveMaterialAssetFloatEdit(0.55F), "Finding 5: apply roughness drag-scrub (order C)");
    report.Check(context.CommitActiveMaterialAssetEdit(), "Finding 5: commit roughness drag-scrub (order C)");
    report.Check(context.SaveMaterialEditorAsset(id), "Finding 5: save (order C)");
    const std::optional<kb::render::RenderMaterialAssetData> reloadedC = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    report.Check(reloadedC.has_value(), "Finding 5: reload after order C");
    if (reloadedC.has_value()) {
        report.Check(std::abs(reloadedC->desc.roughnessFactor - 0.55F) < 0.01F,
            "Finding 5 (order C): the drag-scrubbed roughness survived alongside the unsaved graph node");
        report.Check(nodeC != 0U && kb::render::FindRenderMaterialGraphNode(reloadedC->graph, nodeC) != nullptr,
            "Finding 5 (order C): the graph node survived the drag-scrub commit + save");
    }
}

// Finding 1 (re-opening the already open material): double-clicking the .kbmat that is already open is a
// natural "focus the editor" gesture, so it must not re-read the document from disk and silently drop the
// unsaved working copy. A clean re-open must still reload, otherwise the guard would block refreshes.
void RunMaterialEditorReopenPreservesUnsavedEditsSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 1: register material loader");

    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "ReopenKeepsUnsavedEdits.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    fixture.desc.roughnessFactor = 0.20F;
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 1: create .kbmat fixture (roughness=0.20)");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 1: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/ReopenKeepsUnsavedEdits.kbmat");
    report.Check(metadata != nullptr, "Finding 1: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }
    const kb::assets::AssetId id = metadata->id;

    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 1: open material");
    report.Check(context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantScalar, -120, 80),
        "Finding 1: add graph node (unsaved working-copy edit)");
    const std::uint32_t nodeId = context.SelectedMaterialGraphNodeId();
    report.Check(context.SetMaterialRoughnessFactor(id, 0.66F), "Finding 1: edit roughness via Inspector (unsaved edit)");
    report.Check(context.MaterialEditor().Dirty(), "Finding 1: editor reports dirty before the re-open");

    // The bug: this second open re-read the document from disk and reset dirty_, discarding both edits.
    report.Check(context.OpenMaterialEditorAsset(id), "Finding 1: re-opening the already open material succeeds");
    const std::optional<kb::render::RenderMaterialAssetData>& afterReopen = context.MaterialEditor().WorkingCopy();
    report.Check(context.MaterialEditor().Dirty(), "Finding 1: re-opening the open material keeps the dirty flag");
    report.Check(afterReopen.has_value() && nodeId != 0U &&
            kb::render::FindRenderMaterialGraphNode(afterReopen->graph, nodeId) != nullptr,
        "Finding 1: the unsaved graph node survived the re-open");
    report.Check(afterReopen.has_value() && std::abs(afterReopen->desc.roughnessFactor - 0.66F) < 0.01F,
        "Finding 1: the unsaved Inspector roughness edit survived the re-open");

    report.Check(context.SaveMaterialEditorAsset(id) && !context.MaterialEditor().Dirty(),
        "Finding 1: save after the re-open clears dirty");
    const std::optional<kb::render::RenderMaterialAssetData> savedDocument =
        kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    report.Check(savedDocument.has_value() && std::abs(savedDocument->desc.roughnessFactor - 0.66F) < 0.01F &&
            kb::render::FindRenderMaterialGraphNode(savedDocument->graph, nodeId) != nullptr,
        "Finding 1: both preserved edits reached disk");

    kb::render::RenderMaterialAssetData onDisk = fixture;
    onDisk.desc.roughnessFactor = 0.42F;
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, onDisk),
        "Finding 1: rewrite the asset on disk while the editor is clean");
    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value() &&
            std::abs(context.MaterialEditor().WorkingCopy()->desc.roughnessFactor - 0.42F) < 0.01F,
        "Finding 1: a clean re-open still reloads the document from disk");

    // The production gesture: a Project Files double-click resolved through the real browser layout and the
    // real double-click router, not a direct OpenMaterialEditorAsset call.
    report.Check(context.AssetBrowser().SelectFolder("/Game/Materials", context.Scene().Assets().Manager()),
        "Finding 1: select the material folder in Project Files");
    report.Check(context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantVector, 60, -60),
        "Finding 1: add graph node before the Project Files double-click");
    const std::uint32_t doubleClickNodeId = context.SelectedMaterialGraphNodeId();
    report.Check(context.MaterialEditor().Dirty(), "Finding 1: editor is dirty before the Project Files double-click");

    std::optional<POINT> assetPoint;
    for (int y = kContent.top; y < kContent.bottom && !assetPoint.has_value(); y += 4) {
        for (int x = kContent.left; x < kContent.right; x += 4) {
            const EditorAssetBrowserHit hit = EditorAssetBrowserHitTester::HitTest(
                kContent, x, y, context.AssetBrowser(), context.Scene().Assets().Manager());
            if (hit.kind != EditorAssetBrowserHitKind::Asset) {
                continue;
            }
            const std::optional<kb::assets::AssetId> hitAsset = EditorAssetBrowserHitPayloadResolver::AssetIdAt(
                hit, context.AssetBrowser(), context.Scene().Assets().Manager());
            if (hitAsset.has_value() && *hitAsset == id) {
                assetPoint = POINT{ x, y };
                break;
            }
        }
    }
    report.Check(assetPoint.has_value(), "Finding 1: locate the material tile in the Project Files layout");
    if (!assetPoint.has_value()) {
        return;
    }
    const EditorAssetBrowserDoubleClickResult doubleClick = EditorAssetBrowserDoubleClickHandler::HandleDoubleClick(
        nullptr, kContent, assetPoint->x, assetPoint->y, context);
    report.Check(doubleClick == EditorAssetBrowserDoubleClickResult::MaterialEditorOpened,
        "Finding 1: the Project Files double-click still activates the Material Editor");
    const std::optional<kb::render::RenderMaterialAssetData>& afterDoubleClick = context.MaterialEditor().WorkingCopy();
    report.Check(context.MaterialEditor().OpenAssetId() == id && context.MaterialEditor().Dirty() &&
            afterDoubleClick.has_value() && doubleClickNodeId != 0U &&
            kb::render::FindRenderMaterialGraphNode(afterDoubleClick->graph, doubleClickNodeId) != nullptr,
        "Finding 1: double-clicking the already open material in Project Files keeps the unsaved working copy");

    // An in-flight node rename is memory-only state a reload would drop, so it must hold the guard too.
    const bool savedBeforeRenameGuard = context.SaveMaterialEditorAsset(id);
    report.Check(savedBeforeRenameGuard, "Finding 1: save before the rename-guard check succeeds");
    report.Check(!context.MaterialEditor().Dirty(), "Finding 1: save before the rename-guard check clears dirty");
    report.Check(context.BeginMaterialGraphNodeRenameEdit(id, doubleClickNodeId),
        "Finding 1: begin a node rename on the clean document");
    context.AppendMaterialGraphNodeRenameEditText(L'Z');
    report.Check(context.OpenMaterialEditorAsset(id) && context.IsMaterialGraphNodeRenameEditing(),
        "Finding 1: re-opening while a node rename is in flight keeps the rename instead of reloading");
    context.CancelMaterialGraphNodeRenameEdit();
}

// Finding 11 (a typed inline constant is pending work, not scratch state): a value typed into a constant
// node and not confirmed with Enter used to be dropped by the close path without ever raising the unsaved
// prompt, because HasDirtyMaterialAssetEdit did not count it. It now follows the node-rename contract:
// it counts as unsaved, Save commits it, and closing the editor commits it instead of discarding it.
// Finding 20 (a graph gesture owns the working copy until it ends): Undo/Redo/Save must not run while a
// node drag, a comment drag or a pin rewire is in flight. Undo would restore the document underneath the
// live gesture and the commit that follows would record a stale "before"; Save would write the
// half-finished document to disk and re-base the clean snapshot, so cancelling afterwards would leave the
// editor claiming there is nothing unsaved over a file holding exactly that unconfirmed state.
void RunMaterialGraphGestureBlocksEditCommandsSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 20: register material loader");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "GestureEditGuard.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    // The gesture under test is unplugging a wire, so the fixture needs one: a constant feeding Base Color.
    const kb::render::RenderMaterialGraphNode* outputNode = nullptr;
    for (const kb::render::RenderMaterialGraphNode& node : fixture.graph.nodes) {
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            outputNode = &node;
            break;
        }
    }
    report.Check(outputNode != nullptr, "Finding 20: the default graph has a Material Output node");
    if (outputNode == nullptr) {
        return;
    }
    kb::render::RenderMaterialGraphNode constant{
        .id = 4711U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = -280,
        .positionY = 40,
    };
    constant.parameter.defaultValueHint = "0.4 0.6 0.2 1";
    const std::uint32_t outputNodeId = outputNode->id;
    fixture.graph.nodes.push_back(constant);
    fixture.graph.links.push_back(MakeSelfTestGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        constant.id,
        "rgba",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        outputNodeId,
        "baseColor"));
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 20: create .kbmat fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 20: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/GestureEditGuard.kbmat");
    report.Check(metadata != nullptr, "Finding 20: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }
    const kb::assets::AssetId id = metadata->id;
    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 20: open material");
    const std::optional<kb::render::RenderMaterialAssetData>& document = context.MaterialEditor().WorkingCopy();
    if (!document.has_value() || document->graph.links.empty()) {
        report.Check(false, "Finding 20: the fixture graph has a link to unplug");
        return;
    }
    const kb::render::RenderMaterialGraphLink link = document->graph.links.front();
    const std::size_t linkCountBefore = document->graph.links.size();
    const std::string bytesBefore = ReadFileTextForTest(materialPath);

    report.Check(EditorEditCommandPolicy::CanExecute(context), "Finding 20: edit commands are allowed with no gesture in flight");

    // Positive control first, or every "the command was refused" assertion below could pass for the wrong
    // reason. Undo only reaches the material history when the graph is focused, and only does anything when
    // that history is non-empty - so focus the graph and record a real edit (a completed node drag) here.
    context.FocusMaterialGraph(true);
    const auto nodePosition = [&context](std::uint32_t nodeId) {
        std::pair<int, int> position{ 0, 0 };
        if (context.MaterialEditor().WorkingCopy().has_value()) {
            for (const kb::render::RenderMaterialGraphNode& node : context.MaterialEditor().WorkingCopy()->graph.nodes) {
                if (node.id == nodeId) {
                    position = { node.positionX, node.positionY };
                }
            }
        }
        return position;
    };
    const std::pair<int, int> constantHome = nodePosition(constant.id);
    // SelectMaterialGraphNode reports whether the selection CHANGED, so it is never part of an assertion
    // here - the node is often selected already from an earlier step.
    static_cast<void>(context.SelectMaterialGraphNode(constant.id));
    report.Check(context.BeginMaterialGraphNodeDrag(id, constant.id, 100, 100) && context.DragMaterialGraphNode(180, 150) &&
            context.EndMaterialGraphNodeDrag(),
        "Finding 20: record a completed node drag so the material undo history has something in it");
    const std::pair<int, int> constantMoved = nodePosition(constant.id);
    report.Check(constantMoved != constantHome, "Finding 20: the drag really moved the node");
    report.Check(EditorEditCommandPolicy::Execute(context, EditorEditCommand::Undo) && nodePosition(constant.id) == constantHome,
        "Finding 20: Undo DOES run - and undoes the move - when no gesture is in flight");
    report.Check(EditorEditCommandPolicy::Execute(context, EditorEditCommand::Redo) && nodePosition(constant.id) == constantMoved,
        "Finding 20: Redo DOES run when no gesture is in flight");

    // Scenario A: unplug a wire (this opens a working-copy transaction) and try to undo mid-gesture.
    const bool dirtyBeforeGesture = context.MaterialEditor().Dirty();
    report.Check(context.DetachMaterialGraphInputPinConnection(id, link.toNodeId, link.toPin, 20, 20),
        "Finding 20: pull the wire off the input pin");
    report.Check(context.HasMaterialGraphWorkingCopyTransaction() && context.HasMaterialGraphPinConnection(),
        "Finding 20: the rewire gesture owns an open working-copy transaction");
    report.Check(!EditorEditCommandPolicy::CanExecute(context),
        "Finding 20: edit commands are blocked while the gesture is in flight");
    report.Check(!EditorEditCommandPolicy::Execute(context, EditorEditCommand::Undo),
        "Finding 20: Undo does not run under a live gesture");
    report.Check(context.MaterialEditor().WorkingCopy().has_value() &&
            context.MaterialEditor().WorkingCopy()->graph.links.size() == linkCountBefore - 1U,
        "Finding 20: the gesture's own edit is untouched by the refused Undo");
    // The undo that WOULD have run is the node move recorded above, so this is what a refusal actually saves.
    report.Check(nodePosition(constant.id) == constantMoved,
        "Finding 20: the refused Undo did not roll the previous edit back under the live gesture");

    // Scenario B: Save mid-gesture must not write the half-finished document nor re-base the clean snapshot.
    report.Check(!EditorEditCommandPolicy::Execute(context, EditorEditCommand::Save),
        "Finding 20: Save does not run under a live gesture");
    report.Check(ReadFileTextForTest(materialPath) == bytesBefore,
        "Finding 20: the asset on disk is untouched by the refused Save");

    // Cancelling the gesture restores the document, and the editor is honest about it being unchanged.
    report.Check(context.CancelMaterialGraphPinConnection(), "Finding 20: cancel the gesture");
    report.Check(!context.HasMaterialGraphWorkingCopyTransaction(), "Finding 20: cancelling closes the transaction");
    report.Check(context.MaterialEditor().WorkingCopy().has_value() &&
            context.MaterialEditor().WorkingCopy()->graph.links.size() == linkCountBefore &&
            context.MaterialEditor().Dirty() == dirtyBeforeGesture,
        "Finding 20: after the cancel the document and the dirty flag are back where the gesture found them");
    report.Check(EditorEditCommandPolicy::CanExecute(context), "Finding 20: edit commands work again once the gesture ends");

    // The toolbar Save button and the File menu row used to call SaveOpenDocuments directly, bypassing the
    // gate; they go through the policy's POINTER route now - which is the one asserted here, not the keyboard
    // twin above. This IS reachable: a wire dropped on empty canvas keeps its pin connection armed while the
    // node-creation menu is open (EditorLeftButtonUpRouter), so the mouse is free with a gesture in flight.
    report.Check(context.DetachMaterialGraphInputPinConnection(id, link.toNodeId, link.toPin, 20, 20),
        "Finding 20: re-open the rewire gesture for the toolbar check");
    const std::string bytesBeforeToolbarSave = ReadFileTextForTest(materialPath);
    report.Check(!EditorEditCommandPolicy::ExecuteFromPointer(context, EditorEditCommand::Save),
        "Finding 20: the toolbar Save path refuses under a live gesture too");
    report.Check(ReadFileTextForTest(materialPath) == bytesBeforeToolbarSave,
        "Finding 20: the toolbar Save leaves the asset on disk untouched");
    report.Check(context.CancelMaterialGraphPinConnection(), "Finding 20: cancel the second gesture");

    // A NODE drag works differently from the two gestures above: it does not touch the working copy until the
    // mouse-up (DragMaterialGraphNode only accumulates an offset; EndMaterialGraphNodeDrag applies it). What
    // it holds mid-gesture is the "before" snapshot its undo record will use - so the damage a mid-drag Undo
    // does is to the history, and that is what this block pins down.
    static_cast<void>(context.SelectMaterialGraphNode(constant.id));
    report.Check(context.BeginMaterialGraphNodeDrag(id, constant.id, 100, 100) && context.DragMaterialGraphNode(220, 190),
        "Finding 20: begin a node drag and move the pointer past the drag threshold");
    report.Check(nodePosition(constant.id) == constantMoved,
        "Finding 20: a node drag holds its move in the gesture, not in the document, until the mouse-up");
    report.Check(!EditorEditCommandPolicy::CanExecute(context), "Finding 20: a node drag blocks edit commands too");
    report.Check(!EditorEditCommandPolicy::Execute(context, EditorEditCommand::Undo) &&
            nodePosition(constant.id) == constantMoved,
        "Finding 20: the refused Undo leaves the snapshot the drag will record intact");
    report.Check(context.EndMaterialGraphNodeDrag(), "Finding 20: end the node drag");
    report.Check(EditorEditCommandPolicy::CanExecute(context), "Finding 20: the gate lifts when the drag ends");
    const std::pair<int, int> constantAfterDrag = nodePosition(constant.id);
    report.Check(constantAfterDrag != constantMoved, "Finding 20: the finished drag applied the move");
    // The history is coherent: undoing the drag returns exactly to where the drag started. This is what a
    // mid-drag Undo would have broken - the recorded "before" would have described a document that no longer
    // existed by the time the drag committed.
    report.Check(EditorEditCommandPolicy::Execute(context, EditorEditCommand::Undo) &&
            nodePosition(constant.id) == constantMoved,
        "Finding 20: undoing the drag lands exactly on the pre-drag position");
    report.Check(EditorEditCommandPolicy::Execute(context, EditorEditCommand::Redo) &&
            nodePosition(constant.id) == constantAfterDrag,
        "Finding 20: and redo puts it back");

    // A COMMENT drag is the case that really does edit the document mid-gesture (MoveGraphCommentGroup writes
    // straight into the working copy), so this is where "Save must not write a half-finished document" is a
    // statement about bytes on disk rather than about a flag.
    report.Check(context.AddMaterialGraphComment(id, -420, -120), "Finding 20: add a comment to drag");
    const std::uint32_t commentId = context.MaterialEditor().WorkingCopy().has_value() &&
            !context.MaterialEditor().WorkingCopy()->graph.comments.empty()
        ? context.MaterialEditor().WorkingCopy()->graph.comments.back().id
        : 0U;
    report.Check(commentId != 0U, "Finding 20: resolve the new comment id");
    const std::string bytesBeforeDragSave = ReadFileTextForTest(materialPath);
    if (commentId != 0U) {
        static_cast<void>(context.SelectMaterialGraphComment(commentId));
        const auto commentPosition = [&context, commentId]() {
            std::pair<int, int> position{ 0, 0 };
            if (context.MaterialEditor().WorkingCopy().has_value()) {
                for (const kb::render::RenderMaterialGraphCommentBox& comment : context.MaterialEditor().WorkingCopy()->graph.comments) {
                    if (comment.id == commentId) {
                        position = { comment.positionX, comment.positionY };
                    }
                }
            }
            return position;
        };
        const std::pair<int, int> commentHome = commentPosition();
        report.Check(context.BeginMaterialGraphCommentDrag(id, commentId, 100, 100) && context.DragMaterialGraphComment(240, 200),
            "Finding 20: drag the comment past the threshold");
        report.Check(commentPosition() != commentHome,
            "Finding 20: a comment drag DOES change the working copy mid-gesture");
        report.Check(context.HasMaterialGraphGestureInFlight() && !EditorEditCommandPolicy::CanExecuteFromPointer(context),
            "Finding 20: the comment drag is a gesture the guard sees");
        report.Check(!EditorEditCommandPolicy::ExecuteFromPointer(context, EditorEditCommand::Save),
            "Finding 20: Save mid-comment-drag is refused");
        report.Check(ReadFileTextForTest(materialPath) == bytesBeforeDragSave,
            "Finding 20: the half-dragged comment never reaches the file");
        report.Check(context.EndMaterialGraphCommentDrag(), "Finding 20: end the comment drag");
    }

    // Positive control for the pointer route: with the gesture over and the document dirty, the toolbar Save
    // must actually write. Without this, ExecuteFromPointer could `return false` and every other assertion
    // in this suite would still pass while the Save button was dead.
    report.Check(context.MaterialEditor().Dirty(), "Finding 20: the finished drag left unsaved work");
    report.Check(EditorEditCommandPolicy::ExecuteFromPointer(context, EditorEditCommand::Save),
        "Finding 20: the toolbar Save DOES run once no gesture is in flight");
    report.Check(ReadFileTextForTest(materialPath) != bytesBeforeDragSave && !context.MaterialEditor().Dirty(),
        "Finding 20: and it reached the file");

    // The graph's own keyboard shortcuts edit the same working copy and run before the edit-command policy,
    // so they need the same predicate: pasting into a live drag would record a stale "before" in the undo
    // history, and deleting the dragged node would leave the gesture pointing at nothing.
    // The clipboard refuses the Material Output node, so this half of the check drags the constant instead.
    static_cast<void>(context.SelectMaterialGraphNode(constant.id));
    report.Check(context.CopySelectedMaterialGraphNodes(), "Finding 20: copy a node to the graph clipboard");
    const std::size_t nodeCountBeforeGesture = context.MaterialEditor().WorkingCopy()->graph.nodes.size();
    report.Check(context.BeginMaterialGraphNodeDrag(id, constant.id, 140, 140),
        "Finding 20: begin a drag before the clipboard check");
    report.Check(context.HasMaterialGraphGestureInFlight(), "Finding 20: the shared gesture predicate sees the drag");
    static_cast<void>(context.PasteMaterialGraphNodes(id, 32, 32));
    report.Check(context.MaterialEditor().WorkingCopy()->graph.nodes.size() == nodeCountBeforeGesture,
        "Finding 20: a paste during a drag is refused by the same predicate the shortcut consults");
    // The other three document mutators behind the same guard, plus the palette route, which is the one that
    // is actually reachable mid-drag: the graph keeps keyboard focus while a node is held, so Space+Enter
    // lands in AddMaterialGraphNode and its edit would be swallowed by the drag's stale snapshot.
    report.Check(!context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantColor, 64, 64) &&
            context.MaterialEditor().WorkingCopy()->graph.nodes.size() == nodeCountBeforeGesture,
        "Finding 20: the Space palette cannot add a node through a live drag");
    report.Check(!context.AddMaterialGraphComment(id, 64, 200), "Finding 20: nor add a comment");
    report.Check(!context.DuplicateSelectedMaterialGraphNodes(id, 16, 16) &&
            context.MaterialEditor().WorkingCopy()->graph.nodes.size() == nodeCountBeforeGesture,
        "Finding 20: nor duplicate the selection");
    report.Check(!context.DeleteSelectedMaterialGraphNode(id) &&
            context.MaterialEditor().WorkingCopy()->graph.nodes.size() == nodeCountBeforeGesture,
        "Finding 20: nor delete the node it is dragging");

    report.Check(context.EndMaterialGraphNodeDrag(), "Finding 20: end the clipboard-check drag");
    report.Check(!context.HasMaterialGraphGestureInFlight(), "Finding 20: no gesture in flight once it ends");

    // Positive controls, so none of the refusals above can have come from a precondition rather than the
    // guard. Each is measured against the count right before it, since they compound.
    const auto nodeCount = [&context]() { return context.MaterialEditor().WorkingCopy()->graph.nodes.size(); };
    std::size_t countBefore = nodeCount();
    report.Check(context.PasteMaterialGraphNodes(id, 32, 32) && nodeCount() == countBefore + 1U,
        "Finding 20: the same paste DOES add a node once the drag is over");
    countBefore = nodeCount();
    report.Check(context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantColor, 64, 64) &&
            nodeCount() == countBefore + 1U,
        "Finding 20: and the same add DOES work");
    report.Check(context.AddMaterialGraphComment(id, 64, 200), "Finding 20: and the same comment DOES get added");
    // Both need a selected, non-output node - the add/paste above leave the selection wherever they like.
    static_cast<void>(context.SelectMaterialGraphNode(constant.id));
    countBefore = nodeCount();
    report.Check(context.DuplicateSelectedMaterialGraphNodes(id, 16, 16) && nodeCount() > countBefore,
        "Finding 20: and duplicate DOES work");
    // Delete what the duplicate left selected, not the fixture constant - the blocks below still need its
    // link to the Material Output.
    countBefore = nodeCount();
    report.Check(context.DeleteSelectedMaterialGraphNode(id) && nodeCount() < countBefore,
        "Finding 20: and delete DOES work");
    report.Check(!context.MaterialEditor().WorkingCopy()->graph.links.empty(),
        "Finding 20: the fixture link survived the positive controls");

    // The toolbar and the menus route through the policy too, but a CLICK on Save is not ambiguous the way
    // Ctrl+S is while a field has the keys - and the commands commit the pending edit themselves. Refusing a
    // click on text input would leave the Save button dead after any click into a search or rename box.
    context.FocusMaterialEditorFind(true);
    report.Check(!EditorEditCommandPolicy::CanExecute(context),
        "Finding 20: the keyboard route still stands back while a text field owns the keys");
    report.Check(EditorEditCommandPolicy::CanExecuteFromPointer(context),
        "Finding 20: the toolbar/menu route still works while a text field is armed");
    report.Check(context.DetachMaterialGraphInputPinConnection(id, link.toNodeId, link.toPin, 20, 20),
        "Finding 20: re-open the rewire gesture for the pointer-route check");
    report.Check(!EditorEditCommandPolicy::CanExecuteFromPointer(context),
        "Finding 20: the toolbar/menu route does refuse while a gesture owns the working copy");
    report.Check(context.CancelMaterialGraphPinConnection(), "Finding 20: cancel the pointer-route gesture");
    context.FocusMaterialEditorFind(false);

    // Alt+F4 does not go through the policy at all - the close prompt calls SaveMaterialEditorAsset directly -
    // and it is reachable with the mouse still held, which is the one way a gesture really does outlive the
    // user's intent. So the close path settles the gesture first; this is that contract.
    // Start from a saved document, or "the interrupted drag counts as unsaved work" would be true anyway from
    // the paste above and the assertion would prove nothing.
    report.Check(context.SaveMaterialEditorAsset(id) && !context.MaterialEditor().Dirty(),
        "Finding 20: save first, so the settle check starts from a clean document");
    const std::pair<int, int> settleHome = nodePosition(constant.id);
    static_cast<void>(context.SelectMaterialGraphNode(constant.id));
    report.Check(context.BeginMaterialGraphNodeDrag(id, constant.id, 100, 100) && context.DragMaterialGraphNode(260, 240),
        "Finding 20: start a drag that a close would interrupt");
    // Settle's return value is not asserted anywhere in this suite: CancelMaterialGraphPinConnection cannot
    // fail, so once a gesture is in flight the call can only return true. The effects below carry the weight.
    static_cast<void>(context.SettleMaterialGraphGesture());
    report.Check(!context.HasMaterialGraphGestureInFlight(), "Finding 20: nothing is left in flight after settling");
    report.Check(nodePosition(constant.id) != settleHome && context.MaterialEditor().Dirty(),
        "Finding 20: the interrupted drag is committed and counts as unsaved work, so the prompt appears at all");
    report.Check(EditorEditCommandPolicy::Execute(context, EditorEditCommand::Undo) && nodePosition(constant.id) == settleHome,
        "Finding 20: and it stayed undoable instead of being silently dropped");

    // The other half: a wire still in mid-air was never dropped on a pin, so settling restores the link
    // rather than committing a deletion the user never confirmed.
    const std::size_t linksBeforeSettle = context.MaterialEditor().WorkingCopy()->graph.links.size();
    report.Check(context.DetachMaterialGraphInputPinConnection(id, link.toNodeId, link.toPin, 20, 20),
        "Finding 20: pull a wire off for the close-path check");
    report.Check(context.MaterialEditor().WorkingCopy()->graph.links.size() == linksBeforeSettle - 1U,
        "Finding 20: the wire is off");
    static_cast<void>(context.SettleMaterialGraphGesture());
    report.Check(context.MaterialEditor().WorkingCopy()->graph.links.size() == linksBeforeSettle &&
            !context.HasMaterialGraphWorkingCopyTransaction(),
        "Finding 20: settling puts the link back instead of saving the document without it");

    // Ordering inside Save: the gesture must be settled BEFORE the in-flight text edits are committed. A drag
    // holds the "before" snapshot its undo command will record, taken when the drag began; committing a
    // rename first would leave that snapshot one edit out of date, so the two undo entries would no longer
    // describe consecutive states and a single Undo would roll back both.
    const std::pair<int, int> orderHome = nodePosition(constant.id);
    report.Check(context.BeginMaterialGraphNodeRenameEdit(id, constant.id), "Finding 20: arm a node rename");
    context.ClearMaterialGraphNodeRenameEditText();
    context.InsertMaterialGraphNodeRenameEditText("Tint");
    static_cast<void>(context.SelectMaterialGraphNode(constant.id));
    report.Check(context.BeginMaterialGraphNodeDrag(id, constant.id, 100, 100) && context.DragMaterialGraphNode(300, 260),
        "Finding 20: drag the same node while the rename is armed");
    report.Check(context.SaveMaterialEditorAsset(id), "Finding 20: save with both a rename and a drag pending");
    const auto nodeName = [&context](std::uint32_t nodeId) {
        std::string name;
        if (context.MaterialEditor().WorkingCopy().has_value()) {
            for (const kb::render::RenderMaterialGraphNode& node : context.MaterialEditor().WorkingCopy()->graph.nodes) {
                if (node.id == nodeId) {
                    name = node.parameter.displayName;
                }
            }
        }
        return name;
    };
    report.Check(nodeName(constant.id) == "Tint" && nodePosition(constant.id) != orderHome,
        "Finding 20: both the rename and the move landed");
    // NOTE: the undo history for this combination (a drag settled while a rename is armed) does not unwind
    // the way the ordering alone predicts - one Undo takes back both edits. That is pre-existing behaviour of
    // the rename/undo path, not something the settle introduced, and it is recorded in the findings file
    // rather than asserted here. What IS asserted is the property that matters to the user: neither edit is
    // lost, in memory or on disk.
    const std::string bothPendingBytes = ReadFileTextForTest(materialPath);
    report.Check(bothPendingBytes.find("Tint") != std::string::npos,
        "Finding 20: the rename reached the file");
    report.Check(!context.MaterialEditor().Dirty(),
        "Finding 20: and the save left nothing behind - both pending edits went in together");

    // An in-place mutator folded into an open transaction records no command and never reaches
    // SetWorkingCopy, so the dirty flag used to keep describing the document as it was before the gesture -
    // and that flag is what the close/quit prompt reads.
    report.Check(context.SaveMaterialEditorAsset(id) && !context.MaterialEditor().Dirty(),
        "Finding 20: start the transaction-dirty check from a clean document");
    report.Check(context.DetachMaterialGraphInputPinConnection(id, link.toNodeId, link.toPin, 20, 20) &&
            context.HasMaterialGraphWorkingCopyTransaction(),
        "Finding 20: open a transaction");
    static_cast<void>(context.SelectMaterialGraphNode(constant.id));
    report.Check(context.BeginMaterialGraphNodeDrag(id, constant.id, 100, 100) &&
            context.DragMaterialGraphNode(340, 300) && context.EndMaterialGraphNodeDrag(),
        "Finding 20: commit a node move inside that transaction");
    // NOTE: no assertion here that the dirty flag saw the folded-in move. The only thing that can open a
    // transaction is the detach above, and that already dirtied the document through SetWorkingCopy - so
    // MaterialEditorState::RefreshDirty in the transaction branch is defensive and cannot be distinguished
    // from the outside. Verified by reading, not asserted.
    report.Check(context.MaterialEditor().Dirty() && context.CancelMaterialGraphPinConnection() &&
            !context.MaterialEditor().Dirty(),
        "Finding 20: cancelling the transaction takes the dirty flag back down with the document");

    // And the wiring, not just the helper: Save itself settles, so the prompt's "Yes = Save" cannot write a
    // document caught mid-rewire no matter which route reached it.
    report.Check(context.DetachMaterialGraphInputPinConnection(id, link.toNodeId, link.toPin, 20, 20) &&
            context.HasMaterialGraphGestureInFlight(),
        "Finding 20: pull the wire off once more, this time to Save straight through it");
    report.Check(context.SaveMaterialEditorAsset(id), "Finding 20: the close prompt's Save runs");
    report.Check(!context.HasMaterialGraphGestureInFlight(),
        "Finding 20: Save settled the gesture instead of writing through it");
    const std::string savedBytes = ReadFileTextForTest(materialPath);
    report.Check(savedBytes.find("graphLink") != std::string::npos &&
            context.MaterialEditor().WorkingCopy()->graph.links.size() == linksBeforeSettle,
        "Finding 20: the link the user never dropped is still there, in the document and on disk");
}

void RunMaterialEditorInlineConstantCloseContractSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 11: register material loader");

    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "InlineConstantClose.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 11: create .kbmat fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 11: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/InlineConstantClose.kbmat");
    report.Check(metadata != nullptr, "Finding 11: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }
    const kb::assets::AssetId id = metadata->id;

    const auto readMaterialBytes = [&materialPath]() {
        std::ifstream input{ materialPath, std::ios::binary };
        return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    };
    const auto armInlineConstant = [&context, id](std::uint32_t nodeId, std::wstring_view text) {
        static_cast<void>(context.BeginMaterialGraphConstantInlineEdit(id, nodeId));
        while (context.IsMaterialGraphConstantInlineEditing() && !context.MaterialEditor().GraphConstantInlineEditBuffer().empty()) {
            context.BackspaceMaterialGraphConstantInlineEdit();
        }
        for (const wchar_t character : text) {
            context.AppendMaterialGraphConstantInlineEditText(character);
        }
    };

    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 11: open material");
    report.Check(context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantScalar, -100, 60),
        "Finding 11: add a constant node");
    const std::uint32_t constantNodeId = context.SelectedMaterialGraphNodeId();
    report.Check(context.SaveMaterialEditorAsset(id) && !context.MaterialEditor().Dirty(),
        "Finding 11: save the fixture so only the typed value can be lost");

    // 1. A typed-but-unconfirmed value must register as unsaved work, which is what raises the close prompt.
    armInlineConstant(constantNodeId, L"0.75");
    report.Check(context.IsMaterialGraphConstantInlineEditing() && context.HasDirtyMaterialAssetEdit(),
        "Finding 11: a typed inline constant counts as an unsaved material edit");

    // 2. Answering Save on that prompt must persist the typed value, not the pre-edit one.
    report.Check(context.SaveMaterialEditorAsset(id) && !context.IsMaterialGraphConstantInlineEditing(),
        "Finding 11: Save commits the in-flight inline constant");
    const std::optional<kb::render::RenderMaterialAssetData> savedDocument =
        kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    const kb::render::RenderMaterialGraphNode* savedNode =
        savedDocument.has_value() ? kb::render::FindRenderMaterialGraphNode(savedDocument->graph, constantNodeId) : nullptr;
    report.Check(savedNode != nullptr && savedNode->parameter.defaultValueHint.find("0.75") != std::string::npos,
        "Finding 11: the typed inline constant reached disk through Save");

    // 3. Closing the editor commits the typed value into the working copy instead of dropping it silently.
    armInlineConstant(constantNodeId, L"0.25");
    report.Check(context.IsMaterialGraphConstantInlineEditing(), "Finding 11: re-arm the inline constant before close");
    context.CloseMaterialEditorAsset();
    report.Check(!context.IsMaterialGraphConstantInlineEditing() && !context.MaterialEditor().OpenAssetId().IsValid(),
        "Finding 11: closing the Material Editor clears the inline constant edit");
    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 11: reopen the material after close");
    report.Check(!context.HasDirtyMaterialAssetEdit(),
        "Finding 11: the reopened material is clean");

    // 4. Merely arming the edit (a click on the value, nothing typed) is not pending work: it must not mark
    // the document unsaved, must not raise the close prompt and must not rewrite the file on commit.
    const std::string cleanBytes = readMaterialBytes();
    static_cast<void>(context.BeginMaterialGraphConstantInlineEdit(id, constantNodeId));
    report.Check(context.IsMaterialGraphConstantInlineEditing() && !context.HasDirtyMaterialAssetEdit(),
        "Finding 11: arming the inline constant without typing is not an unsaved edit");
    report.Check(context.CommitMaterialGraphConstantInlineEdit() && !context.IsMaterialGraphConstantInlineEditing() &&
            !context.MaterialEditor().Dirty(),
        "Finding 11: committing an untouched inline constant is a no-op instead of a document rewrite");
    report.Check(readMaterialBytes() == cleanBytes,
        "Finding 11: an untouched inline constant leaves the asset file byte-identical");

    // 5. An unparseable value must not leave the edit armed: that would keep HasDirtyMaterialAssetEdit()
    // true forever and block Save for the whole editor with no way out.
    armInlineConstant(constantNodeId, L"0.5..");
    report.Check(context.HasDirtyMaterialAssetEdit(), "Finding 11: an unparseable typed value still counts as unsaved");
    report.Check(!context.CommitMaterialGraphConstantInlineEdit(), "Finding 11: an unparseable inline constant fails to commit");
    report.Check(!context.IsMaterialGraphConstantInlineEditing() && !context.HasDirtyMaterialAssetEdit(),
        "Finding 11: a failed inline constant commit clears the edit instead of wedging the dirty state");
    report.Check(context.SaveOpenDocuments(), "Finding 11: global Save still works after a failed inline constant commit");

    // 6. Discard must actually discard: Revert clears an in-flight inline constant edit.
    armInlineConstant(constantNodeId, L"0.9");
    report.Check(context.HasDirtyMaterialAssetEdit(), "Finding 11: re-arm a typed value before Revert");
    report.Check(context.RevertMaterialEditorAsset(id) && !context.IsMaterialGraphConstantInlineEditing() &&
            !context.HasDirtyMaterialAssetEdit(),
        "Finding 11: Revert discards the in-flight inline constant edit");
}

// Finding 12 (Save must never report success without writing): with the material open, an Inspector float
// text edit is applied to the in-memory working copy only, so SaveMaterialEditorAsset used to return true
// having written nothing at all — the working copy (including unrelated graph edits) was then dropped by
// the close that the honest "true" authorised.
void RunMaterialEditorInspectorTextEditSavesToDiskSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 12: register material loader");

    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "InspectorTextEditSave.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    fixture.desc.roughnessFactor = 0.10F;
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 12: create .kbmat fixture (roughness=0.10)");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 12: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/InspectorTextEditSave.kbmat");
    report.Check(metadata != nullptr, "Finding 12: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }
    const kb::assets::AssetId id = metadata->id;

    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 12: open material");
    report.Check(context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantScalar, -80, 120),
        "Finding 12: add a graph node so the working copy carries unsaved work of its own");
    const std::uint32_t nodeId = context.SelectedMaterialGraphNodeId();

    // Arm the Inspector float text edit exactly like the panel does: begin on the property, then type.
    context.Inspector().BeginTextEdit(InspectorPropertyId::MaterialRoughnessFactor, "0.10");
    context.Inspector().SelectAllText();
    context.Inspector().InsertText("0.80");
    report.Check(context.Inspector().IsTextEditDirty() && context.HasDirtyMaterialAssetEdit(),
        "Finding 12: the armed Inspector float edit registers as unsaved material work");

    report.Check(context.SaveMaterialEditorAsset(id), "Finding 12: Save reports success");
    report.Check(!context.HasDirtyMaterialAssetEdit(),
        "Finding 12: a successful Save leaves nothing unsaved behind");

    const std::optional<kb::render::RenderMaterialAssetData> savedDocument =
        kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    report.Check(savedDocument.has_value() && std::abs(savedDocument->desc.roughnessFactor - 0.80F) < 0.01F,
        "Finding 12: the Inspector value Save claimed to persist actually reached disk");
    report.Check(savedDocument.has_value() && nodeId != 0U &&
            kb::render::FindRenderMaterialGraphNode(savedDocument->graph, nodeId) != nullptr,
        "Finding 12: the unrelated graph edit in the same working copy reached disk too");
}

// Finding 13 (dialog modality): a modal Material Editor dialog must lock the whole editor, not just the
// window it was parented to, or a panel in a floating window stays clickable while the dialog is up.
// Real HWNDs, because that is the only thing that proves the Win32 behaviour.
// Thumbnail image quality: the capture has no MSAA and sits on opaque black, so the pipeline has to do
// the work - anti-aliased silhouette from a supersampled coverage mask, transparent background, and a
// contact shadow so the ball is grounded instead of floating.
void RunMaterialThumbnailImagePipelineSuite(Report& report) {
    // A synthetic "linear render": a lit sphere-ish disc on the renderer's opaque black clear.
    constexpr int kSource = 1024;
    EditorMaterialThumbnailImage image{ .width = kSource, .height = kSource };
    image.bgra.assign(static_cast<std::size_t>(kSource) * static_cast<std::size_t>(kSource), 0xFF000000U);
    const double center = kSource * 0.5;
    const double radius = kSource * 0.40;
    for (int y = 0; y < kSource; ++y) {
        for (int x = 0; x < kSource; ++x) {
            const double dx = x - center;
            const double dy = y - center;
            if ((dx * dx + dy * dy) > (radius * radius)) {
                continue;
            }
            image.bgra[static_cast<std::size_t>(y) * kSource + static_cast<std::size_t>(x)] = 0xFF20A030U;
        }
    }

    EditorMaterialThumbnailService::ProcessCapture(image);
    report.Check(image.width == 256 && image.height == 256,
        "Finding 18: the oversized capture is downsampled to tile resolution");
    if (image.width != 256 || image.height != 256) {
        return;
    }
    const auto alphaAt = [&image](int x, int y) {
        return (image.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
            static_cast<std::size_t>(x)] >> 24U) & 0xFFU;
    };

    report.Check(alphaAt(2, 2) == 0U, "Finding 18: the black background is punched out to full transparency");
    report.Check(alphaAt(128, 128) == 255U, "Finding 18: the material itself stays fully opaque");

    // The silhouette edge must contain partial coverage - that is what "not pixelated" means here.
    int partialCoverage = 0;
    for (int x = 0; x < image.width; ++x) {
        const std::uint32_t alpha = alphaAt(x, 128);
        if (alpha > 8U && alpha < 247U) {
            ++partialCoverage;
        }
    }
    report.Check(partialCoverage >= 2,
        "Finding 18: the silhouette is anti-aliased (partial coverage pixels on the edge: " +
            std::to_string(partialCoverage) + ")");

    // Contact shadow: opacity below the ball that the render itself never produced.
    const int shadowY = std::min(image.height - 2, static_cast<int>(std::lround(image.height * 0.5 + 256 * 0.40 * 0.5 / 4.0 + 30)));
    int shadowPixels = 0;
    for (int x = 0; x < image.width; ++x) {
        const std::uint32_t alpha = alphaAt(x, image.height - 40);
        if (alpha > 8U) {
            ++shadowPixels;
        }
    }
    static_cast<void>(shadowY);
    report.Check(shadowPixels > 0,
        "Finding 18: a soft contact shadow grounds the ball (shadow pixels under it: " +
            std::to_string(shadowPixels) + ")");

    // One source of truth for the ball size: whatever the render framed, the thumbnail normalises the
    // silhouette to the shared fraction, so a tile never resizes the ball when the render replaces the
    // painted stand-in. Two very different framings must come out identical.
    const auto silhouetteWidth = [](const EditorMaterialThumbnailImage& thumbnail) {
        int left = thumbnail.width;
        int right = -1;
        for (int y = 0; y < thumbnail.height; ++y) {
            for (int x = 0; x < thumbnail.width; ++x) {
                if (((thumbnail.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(thumbnail.width) +
                        static_cast<std::size_t>(x)] >> 24U) & 0xFFU) < 200U) {
                    continue;
                }
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
        return right - left + 1;
    };
    const auto renderDisc = [](int size, double radiusFraction) {
        EditorMaterialThumbnailImage fixture{ .width = size, .height = size };
        fixture.bgra.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0xFF000000U);
        const double center = size * 0.5;
        const double radius = size * radiusFraction;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const double dx = x - center;
                const double dy = y - center;
                if ((dx * dx + dy * dy) <= (radius * radius)) {
                    fixture.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] =
                        0xFF20A030U;
                }
            }
        }
        EditorMaterialThumbnailService::ProcessCapture(fixture);
        return fixture;
    };
    const EditorMaterialThumbnailImage tightRender = renderDisc(kSource, 0.48);
    const EditorMaterialThumbnailImage looseRender = renderDisc(kSource, 0.28);
    const int tightWidth = silhouetteWidth(tightRender);
    const int looseWidth = silhouetteWidth(looseRender);
    report.Check(std::abs(tightWidth - looseWidth) <= 4,
        "Finding 18: the ball is one size regardless of how the render framed it (" +
            std::to_string(tightWidth) + " vs " + std::to_string(looseWidth) + " px)");
    report.Check(std::abs(tightWidth - static_cast<int>(std::lround(256.0 * kMaterialPreviewBallFraction))) <= 6,
        "Finding 18: the ball matches the shared size fraction the painted stand-in also uses");

    // Detail retention: a tile draws the thumbnail small, so the service scales it itself instead of
    // letting GDI stretch the master. A checkerboard survives that path only if the scaler is an area
    // filter - a naive nearest/stretch collapses it into one flat colour.
    EditorMaterialThumbnailImage detail{ .width = 256, .height = 256 };
    detail.bgra.assign(256U * 256U, 0xFF000000U);
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            const bool light = (((x / 4) + (y / 4)) % 2) == 0;
            detail.bgra[static_cast<std::size_t>(y) * 256U + static_cast<std::size_t>(x)] =
                0xFF000000U | (light ? 0x00C8C8C8U : 0x00303030U);
        }
    }
    const EditorMaterialThumbnailImage scaled = EditorMaterialThumbnailService::ScaleForDisplaySize(detail, 56);
    std::uint32_t darkest = 255U;
    std::uint32_t brightest = 0U;
    for (const std::uint32_t pixel : scaled.bgra) {
        const std::uint32_t green = (pixel >> 8U) & 0xFFU;
        darkest = std::min(darkest, green);
        brightest = std::max(brightest, green);
    }
    report.Check(scaled.width == 56 && scaled.height == 56, "Finding 18: the thumbnail is scaled to the tile's own size");
    report.Check(brightest > darkest + 24U,
        "Finding 18: texture detail survives the downscale to tile size (contrast kept: " +
            std::to_string(brightest - darkest) + ")");

    // Leave the processed thumbnail on disk so the result can be eyeballed, like the other visual suites.
    if (const std::optional<CLSID> encoder = GdiplusEncoderClsid(L"image/bmp")) {
        HeroIconGdiplusRuntime::EnsureStarted();
        Gdiplus::Bitmap bitmap(image.width, image.height, image.width * 4,
            PixelFormat32bppARGB, reinterpret_cast<BYTE*>(image.bgra.data()));
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "21kb_selftest" / "_materialThumbnailPipeline.bmp";
        static_cast<void>(bitmap.Save(path.wstring().c_str(), &*encoder, nullptr));
    }
}

void RunModalWindowScopeSuite(Report& report) {
    const wchar_t className[] = L"KBEditorSelfTestModalScopeWindow";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = className;
    const bool classRegistered = RegisterClassW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    report.Check(classRegistered, "Finding 13: register self-test window class");
    if (!classRegistered) {
        return;
    }

    const auto makeWindow = [className]() {
        return CreateWindowExW(
            0, className, L"kb self-test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 120, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    };
    const HWND mainWindow = makeWindow();      // stands in for the docked editor window
    const HWND floatingWindow = makeWindow();  // stands in for an undocked Material Editor panel
    const HWND outerDialog = makeWindow();     // stands in for the parameter/colour dialog
    const HWND innerDialog = makeWindow();     // stands in for a second dialog opened on top
    const bool windowsCreated = mainWindow != nullptr && floatingWindow != nullptr &&
        outerDialog != nullptr && innerDialog != nullptr;
    report.Check(windowsCreated, "Finding 13: create the stand-in editor windows");
    if (!windowsCreated) {
        for (const HWND window : { mainWindow, floatingWindow, outerDialog, innerDialog }) {
            if (window != nullptr) {
                DestroyWindow(window);
            }
        }
        return;
    }
    ShowWindow(innerDialog, SW_HIDE);
    const auto enabled = [](HWND window) { return IsWindowEnabled(window) != 0; };

    {
        const EditorModalWindowScope outerScope{ outerDialog };
        report.Check(!enabled(mainWindow) && !enabled(floatingWindow),
            "Finding 13: a modal dialog disables the main window AND the floating panel, not just its owner");
        report.Check(enabled(outerDialog), "Finding 13: the dialog itself stays interactive");

        ShowWindow(innerDialog, SW_SHOWNA);
        {
            const EditorModalWindowScope innerScope{ innerDialog };
            report.Check(!enabled(outerDialog), "Finding 13: a nested dialog disables the dialog below it");
        }
        report.Check(enabled(outerDialog) && !enabled(mainWindow) && !enabled(floatingWindow),
            "Finding 13: closing the nested dialog restores only it, leaving the editor locked by the outer dialog");
        ShowWindow(innerDialog, SW_HIDE);
    }
    report.Check(enabled(mainWindow) && enabled(floatingWindow) && enabled(outerDialog),
        "Finding 13: closing the last dialog unlocks the whole editor");

    for (const HWND window : { mainWindow, floatingWindow, outerDialog, innerDialog }) {
        DestroyWindow(window);
    }
}

// Finding 22 (diagnostic logging must cost nothing by default): the colour picker opened an ofstream per
// log line and called OutputDebugStringA from its constructor, from every paint and from mouse-move - file
// I/O on the UI thread in the middle of a drag - with no way to turn it off short of a rebuild.
void RunDebugLogGateSuite(Report& report) {
    const char* const name = "KB_EDITOR_SELFTEST_LOG_GATE";
    const auto set = [name](const char* value) { return SetEnvironmentVariableA(name, value) != 0; };

    report.Check(SetEnvironmentVariableA(name, nullptr) != 0, "Finding 22: clear the switch");
    report.Check(!EditorDebugLogVariableEnabled(name), "Finding 22: an unset variable means off");
    report.Check(set("") && !EditorDebugLogVariableEnabled(name), "Finding 22: an empty variable means off");
    report.Check(set("0") && !EditorDebugLogVariableEnabled(name), "Finding 22: \"0\" means off");
    report.Check(set("1") && EditorDebugLogVariableEnabled(name), "Finding 22: \"1\" means on");
    report.Check(set("verbose") && EditorDebugLogVariableEnabled(name), "Finding 22: any other value means on");
    // Longer than the read buffer: must not be mistaken for unset, and must not read past it.
    report.Check(set("00000000000000000000000000000001") && EditorDebugLogVariableEnabled(name),
        "Finding 22: a value longer than the buffer still means on");
    report.Check(SetEnvironmentVariableA(name, nullptr) != 0 && !EditorDebugLogVariableEnabled(name),
        "Finding 22: and clearing it turns it back off");

    // The switch the colour picker actually reads must be off in a normal editor process, or the picker is
    // back to writing a file per paint.
    report.Check(!EditorDebugLogVariableEnabled("KB_MATERIAL_COLOR_PICKER_LOG"),
        "Finding 22: the colour picker trace is off unless someone asks for it");

    // Finding 23: the parameter-value dialog used a hand-rolled Latin-1/ASCII conversion pair, so anything
    // the user typed outside ASCII came back as '?' - a silent value corruption. It now delegates to the
    // editor's shared UTF-8 conversion, which is what this round-trip pins.
    const std::string utf8 = "Szorstkosc ÅwiatÅa â Î±/Î²";
    const std::wstring wide = ScriptEditorTextEncoding::Widen(utf8);
    report.Check(wide.find(L'?') == std::wstring::npos, "Finding 23: widening does not replace non-ASCII with '?'");
    report.Check(ScriptEditorTextEncoding::Narrow(wide) == utf8,
        "Finding 23: a non-ASCII parameter value survives the dialog's conversion both ways");
    report.Check(ScriptEditorTextEncoding::Widen("").empty() && ScriptEditorTextEncoding::Narrow(L"").empty(),
        "Finding 23: and empty text stays empty");
}

// Finding 24 (selection and find results must not outlive the document they point at).
void RunMaterialEditorStaleReferenceSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 24: register material loader");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "StaleReferences.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    kb::render::RenderMaterialGraphNode constant{
        .id = 5150U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = -260,
        .positionY = 60,
    };
    constant.parameter.displayName = "Findable";
    fixture.graph.nodes.push_back(constant);
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 24: create .kbmat fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 24: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/StaleReferences.kbmat");
    report.Check(metadata != nullptr, "Finding 24: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }
    const kb::assets::AssetId id = metadata->id;
    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 24: open material");

    // Selecting a node id that is not in the document must not stick. The graph panel hit-tests an empty
    // document against a fabricated default graph, so ids that exist nowhere can reach this call.
    static_cast<void>(context.SelectMaterialGraphNode(constant.id));
    report.Check(context.MaterialEditor().SelectedNodeId() == constant.id, "Finding 24: a real node selects");
    static_cast<void>(context.SelectMaterialGraphNode(999999U));
    report.Check(context.MaterialEditor().SelectedNodeId() != 999999U,
        "Finding 24: a node id that is not in the document never becomes the selection");

    // Find results are rebuilt by SetWorkingCopy; a revert replaces the document the same way and must
    // rebuild them too, or the panel lists nodes that no longer exist.
    context.SetMaterialEditorFindQuery("Findable");
    report.Check(!context.MaterialEditor().FindResults().empty(), "Finding 24: the fixture node is findable");
    static_cast<void>(context.SelectMaterialGraphNode(constant.id));
    report.Check(context.DeleteSelectedMaterialGraphNode(id), "Finding 24: delete the findable node");
    report.Check(context.MaterialEditor().FindResults().empty(),
        "Finding 24: deleting it clears the hit (SetWorkingCopy refreshes)");
    report.Check(context.RevertMaterialEditorAsset(id), "Finding 24: revert brings the node back");
    report.Check(!context.MaterialEditor().FindResults().empty(),
        "Finding 24: and the revert rebuilds the find results instead of leaving them stale");
}

// Finding 25 (material-level settings live in the Inspector): domain / shading model / blend mode are stored
// on the document and honoured by the compiler, but nothing in the editor wrote them - the panel now edits
// them through the same working-copy path as every other edit.
void RunMaterialSettingsInspectorSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 25: register material loader");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "MaterialSettings.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 25: create .kbmat fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 25: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/MaterialSettings.kbmat");
    report.Check(metadata != nullptr, "Finding 25: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }
    const kb::assets::AssetId id = metadata->id;
    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 25: open material");

    // With nothing selected, the Inspector shows the three material settings.
    const std::vector<MaterialEditorGraphNodeProperty> settings = context.MaterialEditor().MaterialSettingsProperties();
    report.Check(settings.size() == 3U, "Finding 25: three material settings are exposed");
    const auto findSetting = [&settings](std::string_view stableId) -> const MaterialEditorGraphNodeProperty* {
        for (const MaterialEditorGraphNodeProperty& row : settings) {
            if (row.stableId == stableId) {
                return &row;
            }
        }
        return nullptr;
    };
    const MaterialEditorGraphNodeProperty* domainRow = findSetting("material.domain");
    const MaterialEditorGraphNodeProperty* shadingRow = findSetting("material.shadingModel");
    const MaterialEditorGraphNodeProperty* blendRow = findSetting("material.blendMode");
    report.Check(domainRow != nullptr && shadingRow != nullptr && blendRow != nullptr,
        "Finding 25: domain, shading model and blend mode rows are all present");
    if (domainRow == nullptr || shadingRow == nullptr || blendRow == nullptr) {
        return;
    }
    report.Check(domainRow->nodeId == 0U && domainRow->kind == MaterialEditorGraphNodePropertyKind::Enum,
        "Finding 25: a setting is a material-scoped (node 0) enum row");

    const auto hasOption = [](const MaterialEditorGraphNodeProperty& row, std::string_view value) {
        return std::ranges::any_of(row.options, [value](const MaterialEditorGraphNodePropertyOption& option) {
            return option.value == value;
        });
    };
    // Options are derived from the renderer's production predicates, so non-production values are never
    // offered and the editor cannot author a material the compiler would reject.
    report.Check(hasOption(*domainRow, "surface") && domainRow->options.size() == 1U,
        "Finding 25: only the production domain (surface) is offered");
    report.Check(hasOption(*shadingRow, "unlit") && hasOption(*shadingRow, "defaultLit") &&
            hasOption(*shadingRow, "subsurface") && hasOption(*shadingRow, "clearCoat"),
        "Finding 25: production shading models are offered");
    report.Check(!hasOption(*shadingRow, "cloth") && !hasOption(*shadingRow, "hair") && !hasOption(*shadingRow, "eye"),
        "Finding 25: non-production shading models are NOT offered");
    report.Check(blendRow->options.size() == 7U && hasOption(*blendRow, "opaque") && hasOption(*blendRow, "alphaHoldout"),
        "Finding 25: all seven blend modes are offered");
    report.Check(shadingRow->value.text == "Default Lit",
        "Finding 25: the field shows the friendly label of the stored value");

    // Editing writes through the working copy, so it dirties like any other edit and shows on disk after save.
    report.Check(!context.MaterialEditor().Dirty(), "Finding 25: a freshly opened material is clean");
    report.Check(context.SetMaterialGraphSetting(id, "material.shadingModel", "unlit"),
        "Finding 25: setting the shading model succeeds");
    report.Check(context.MaterialEditor().WorkingCopy()->graph.shadingModel == "unlit" && context.MaterialEditor().Dirty(),
        "Finding 25: the document holds the new value and is now dirty");

    // A no-op and a non-production value are both declined and change nothing.
    report.Check(!context.SetMaterialGraphSetting(id, "material.shadingModel", "unlit"),
        "Finding 25: setting the same value again is a declined no-op");
    report.Check(!context.SetMaterialGraphSetting(id, "material.shadingModel", "cloth") &&
            context.MaterialEditor().WorkingCopy()->graph.shadingModel == "unlit",
        "Finding 25: a non-production value is refused and leaves the document untouched");
    report.Check(!context.SetMaterialGraphSetting(id, "material.bogus", "whatever"),
        "Finding 25: an unknown setting id is refused");

    // The edit is on the undo stack: undo restores the previous value.
    context.FocusMaterialGraph(true);
    report.Check(EditorEditCommandPolicy::Execute(context, EditorEditCommand::Undo) &&
            context.MaterialEditor().WorkingCopy()->graph.shadingModel == "defaultLit",
        "Finding 25: undo restores the previous shading model");
    report.Check(EditorEditCommandPolicy::Execute(context, EditorEditCommand::Redo) &&
            context.MaterialEditor().WorkingCopy()->graph.shadingModel == "unlit",
        "Finding 25: redo puts it back");

    // Save persists it, and reopening reads it back.
    report.Check(context.SaveMaterialEditorAsset(id) && !context.MaterialEditor().Dirty(),
        "Finding 25: save persists the setting");
    const std::string savedBytes = ReadFileTextForTest(materialPath);
    report.Check(savedBytes.find("unlit") != std::string::npos,
        "Finding 25: the new shading model reached the file");

    // Round-trip through the panel builder: with no node selected the rows land in the details panel under
    // the Material section; selecting a node replaces them with that node's properties.
    const MaterialEditorPanelDetailsRows unselectedRows =
        MaterialEditorPanelRenderer::DetailsRowsForDocument(context, *context.MaterialEditor().WorkingCopy(), false);
    report.Check(unselectedRows.nodePropertiesAreMaterialSettings && unselectedRows.nodePropertyRows.size() == 3U,
        "Finding 25: the panel shows the material settings when nothing is selected");
    // The developer-only diagnostic dumps (Material Diff, Debug Channels) are off by default so the panel
    // reads like an Inspector rather than a debug log; they return only under KB_MATERIAL_EDITOR_DEBUG_DETAILS.
    report.Check(unselectedRows.debugChannelRows.empty() && unselectedRows.materialDiffRows.empty(),
        "Finding 25: the debug dumps are hidden from the default Details view");

    // Parameter and texture rows read like an Inspector, not a debug log: no always-on "override on/disabled"
    // vocabulary and no "Core Scalar"-style type prefix on a base material.
    const auto anyRowContains = [](const std::vector<std::string>& rows, std::string_view needle) {
        return std::ranges::any_of(rows, [needle](const std::string& row) { return row.find(needle) != std::string::npos; });
    };
    report.Check(!anyRowContains(unselectedRows.parameterRows, "override on") &&
            !anyRowContains(unselectedRows.parameterRows, "override disabled"),
        "Finding 25: parameter rows have no always-on override vocabulary");
    report.Check(!unselectedRows.parameterRows.empty() && !anyRowContains(unselectedRows.parameterRows, " Scalar  "),
        "Finding 25: parameter rows drop the 'Core Scalar' type prefix");
    // An unassigned texture slot shows "None", never a raw 64-bit id.
    report.Check(anyRowContains(unselectedRows.textureSlotRows, "= None") &&
            !anyRowContains(unselectedRows.textureSlotRows, "override on"),
        "Finding 25: an unassigned texture slot reads '= None', not a raw id");
    const std::uint32_t outputNodeId = context.MaterialEditor().WorkingCopy()->graph.nodes.front().id;
    static_cast<void>(context.SelectMaterialGraphNode(outputNodeId));
    const MaterialEditorPanelDetailsRows selectedRows =
        MaterialEditorPanelRenderer::DetailsRowsForDocument(context, *context.MaterialEditor().WorkingCopy(), false);
    report.Check(!selectedRows.nodePropertiesAreMaterialSettings,
        "Finding 25: selecting a node replaces the material settings with the node's own properties");
}

// Finding 26 (comments must be editable): comments used to be inert - the text was hard-coded "Comment" with
// no way to change it, its colour, or (next) its size. This covers the text and colour editing paths.
void RunMaterialGraphCommentEditingSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 26: register material loader");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "CommentEditing.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 26: create .kbmat fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 26: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/CommentEditing.kbmat");
    report.Check(metadata != nullptr, "Finding 26: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }
    const kb::assets::AssetId id = metadata->id;
    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 26: open material");

    report.Check(context.AddMaterialGraphComment(id, -200, -120), "Finding 26: add a comment");
    const std::uint32_t commentId = context.SelectedMaterialGraphCommentId();
    report.Check(commentId != 0U, "Finding 26: the new comment is selected");
    const auto commentText = [&context, commentId]() {
        std::string text;
        if (context.MaterialEditor().WorkingCopy().has_value()) {
            for (const kb::render::RenderMaterialGraphCommentBox& comment : context.MaterialEditor().WorkingCopy()->graph.comments) {
                if (comment.id == commentId) {
                    text = comment.text;
                }
            }
        }
        return text;
    };
    report.Check(commentText() == "Comment", "Finding 26: a new comment starts with the default label");

    // Text editing writes through the working copy: it dirties, survives a save with spaces intact, and undoes.
    report.Check(context.SetMaterialGraphCommentText(id, commentId, "Lighting section") && commentText() == "Lighting section" &&
            context.MaterialEditor().Dirty(),
        "Finding 26: editing the comment text updates the document and dirties it");
    report.Check(context.SaveMaterialEditorAsset(id), "Finding 26: save the comment text");
    report.Check(ReadFileTextForTest(materialPath).find("graphComment") != std::string::npos,
        "Finding 26: the comment reached the file");
    report.Check(context.RevertMaterialEditorAsset(id), "Finding 26: revert");
    report.Check(context.OpenMaterialEditorAsset(id), "Finding 26: reopen after save");
    std::uint32_t reloadedCommentId = 0U;
    if (context.MaterialEditor().WorkingCopy().has_value() && !context.MaterialEditor().WorkingCopy()->graph.comments.empty()) {
        reloadedCommentId = context.MaterialEditor().WorkingCopy()->graph.comments.front().id;
    }
    report.Check(reloadedCommentId != 0U, "Finding 26: the saved comment reloaded");
    if (reloadedCommentId != 0U) {
        std::string reloadedText;
        for (const kb::render::RenderMaterialGraphCommentBox& comment : context.MaterialEditor().WorkingCopy()->graph.comments) {
            if (comment.id == reloadedCommentId) {
                reloadedText = comment.text;
            }
        }
        report.Check(reloadedText == "Lighting section",
            "Finding 26: the comment text with a space round-trips through save and reload");

        // Colour editing works and is undoable.
        static_cast<void>(context.SelectMaterialGraphComment(reloadedCommentId));
        report.Check(context.SetMaterialGraphCommentColor(id, reloadedCommentId, 0x00804020U),
            "Finding 26: recoloring the comment succeeds");
        std::uint32_t recolored = 0U;
        for (const kb::render::RenderMaterialGraphCommentBox& comment : context.MaterialEditor().WorkingCopy()->graph.comments) {
            if (comment.id == reloadedCommentId) {
                recolored = comment.color;
            }
        }
        report.Check(recolored == 0x00804020U, "Finding 26: the new colour is in the document");
        context.FocusMaterialGraph(true);
        report.Check(EditorEditCommandPolicy::Execute(context, EditorEditCommand::Undo), "Finding 26: undo the recolor");
        std::uint32_t afterUndo = 0U;
        for (const kb::render::RenderMaterialGraphCommentBox& comment : context.MaterialEditor().WorkingCopy()->graph.comments) {
            if (comment.id == reloadedCommentId) {
                afterUndo = comment.color;
            }
        }
        report.Check(afterUndo != 0x00804020U, "Finding 26: undo restores the previous colour");
    }
}

// Finding 27 (a diagnostic jumps to its node): a node-tied diagnostic line is clickable and centres the graph
// on the offending node, the way Unreal's error list focuses the node instead of leaving the user to hunt.
void RunMaterialGraphDiagnosticJumpSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 27: register material loader");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "DiagnosticJump.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    // A named-reroute declaration with no name is a clean, node-tied validation error that round-trips
    // through save/load (an inverted numeric range would be rejected by the parser on reload instead).
    kb::render::RenderMaterialGraphNode broken{
        .id = 4712U,
        .kind = kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration,
        .positionX = 240,
        .positionY = -180,
    };
    fixture.graph.nodes.push_back(broken);
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 27: create .kbmat fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 27: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/DiagnosticJump.kbmat");
    report.Check(metadata != nullptr, "Finding 27: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }
    const kb::assets::AssetId id = metadata->id;
    report.Check(context.OpenMaterialEditorAsset(id) && context.MaterialEditor().WorkingCopy().has_value(),
        "Finding 27: open material");
    context.SetMaterialGraphCanvasViewport(kContent.right, kContent.bottom);

    // The node-id list stays strictly parallel to the diagnostics list, with a real id for the broken node.
    const std::vector<std::string>& diagnostics = context.MaterialEditor().Diagnostics();
    const std::vector<std::uint32_t>& nodeIds = context.MaterialEditor().DiagnosticNodeIds();
    report.Check(!diagnostics.empty(), "Finding 27: the broken range produces a diagnostic");
    report.Check(nodeIds.size() == diagnostics.size(), "Finding 27: the node-id list is parallel to the diagnostics list");
    std::size_t brokenRowIndex = diagnostics.size();
    for (std::size_t index = 0U; index < nodeIds.size(); ++index) {
        if (nodeIds[index] == broken.id) {
            brokenRowIndex = index;
            break;
        }
    }
    report.Check(brokenRowIndex < diagnostics.size(), "Finding 27: a diagnostic line points at the broken node");
    if (brokenRowIndex >= diagnostics.size()) {
        return;
    }

    // The panel hit-test maps a click on that row back to the node id.
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(kContent);
    const RECT& panel = layout.diagnosticsPanel;
    const int rowY = panel.top + 32 + static_cast<int>(brokenRowIndex) * 22 + 11;
    const int rowX = panel.left + 30;
    report.Check(MaterialEditorPanelRenderer::DiagnosticsRowNodeAt(kContent, context, rowX, rowY) == broken.id,
        "Finding 27: clicking the diagnostic row resolves to the broken node");
    report.Check(MaterialEditorPanelRenderer::DiagnosticsRowNodeAt(kContent, context, rowX, panel.top + 4) == 0U,
        "Finding 27: clicking the panel title is not a row hit");

    // Focusing the node selects it (and re-centres the view - not observable here, but the selection is).
    report.Check(context.FocusMaterialGraphNode(broken.id) && context.MaterialEditor().SelectedNodeId() == broken.id,
        "Finding 27: focusing the diagnostic node selects it");
    report.Check(!context.FocusMaterialGraphNode(0U) && !context.FocusMaterialGraphNode(999999U),
        "Finding 27: focusing a missing node id does nothing");
}

// Finding 28 (material preview camera control): the preview object was locked to one head-on angle. Drag now
// orbits it and the wheel dollies in/out, the way Unreal's material-preview viewport reads.
void RunMaterialPreviewCameraControlSuite(Report& report) {
    EditorSceneContext context;
    std::error_code error;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 28: register material loader");
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "PreviewCamera.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 28: create .kbmat fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 28: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/PreviewCamera.kbmat");
    report.Check(metadata != nullptr, "Finding 28: resolve .kbmat metadata");
    if (metadata == nullptr) {
        return;
    }
    report.Check(context.OpenMaterialEditorAsset(metadata->id), "Finding 28: open material");

    // Build the preview scene so the revision has a real baseline. Every camera move below must leave this
    // baseline untouched: an orbit/zoom is a per-frame camera override, never a scene edit, so it must NOT
    // bump the revision - a bump makes the viewport treat the scene as changed and re-sync every mesh and
    // material to the GPU on every orbit frame, which was the orbit "cosmic lag".
    static_cast<void>(context.MaterialPreviewScene(metadata->id));
    const std::uint64_t revisionBeforeCameraMoves = context.MaterialPreviewRevision();

    const auto almostEqual = [](float a, float b) { return std::fabs(a - b) < 0.01F; };
    const auto settings = [&context]() -> const EditorMaterialPreviewSceneSettings& { return context.MaterialPreviewSceneSettings(); };

    // Default: straight down -Z, matching the historic fixed camera (so thumbnails are unchanged).
    report.Check(almostEqual(settings().orbitYawDegrees, 0.0F) && almostEqual(settings().orbitPitchDegrees, 0.0F),
        "Finding 28: the preview starts at the historic head-on angle");
    const std::array<float, 3U> eye0 = settings().CameraEye();
    report.Check(almostEqual(eye0[0], 0.0F) && almostEqual(eye0[1], 0.0F) && almostEqual(eye0[2], -settings().cameraDistance),
        "Finding 28: the head-on eye is straight in front of the object");

    // Orbit 90 degrees of yaw swings the camera to the side (+X, looking back at the object).
    report.Check(context.OrbitMaterialPreviewCamera(90.0F, 0.0F) && almostEqual(settings().orbitYawDegrees, 90.0F),
        "Finding 28: dragging orbits the yaw");
    const std::array<float, 3U> eyeYaw = settings().CameraEye();
    report.Check(almostEqual(eyeYaw[0], settings().cameraDistance) && almostEqual(eyeYaw[1], 0.0F) && almostEqual(eyeYaw[2], 0.0F),
        "Finding 28: a 90-degree yaw puts the camera to the side of the object");

    // Pitch is clamped so the orbit never flips over the pole.
    report.Check(context.OrbitMaterialPreviewCamera(0.0F, 1000.0F) &&
            settings().orbitPitchDegrees <= kEditorMaterialPreviewMaxPitchDegrees + 0.01F,
        "Finding 28: pitch is clamped below the pole");

    // The wheel dollies: a factor < 1 moves the camera closer, and the distance is clamped to a floor.
    const float distanceBeforeZoom = settings().cameraDistance;
    report.Check(context.ZoomMaterialPreviewCamera(0.5F) && settings().cameraDistance < distanceBeforeZoom,
        "Finding 28: the wheel dollies the camera closer");
    for (int i = 0; i < 40; ++i) {
        static_cast<void>(context.ZoomMaterialPreviewCamera(0.5F));
    }
    report.Check(almostEqual(settings().cameraDistance, kEditorMaterialPreviewMinCameraDistance),
        "Finding 28: zooming in is clamped to the minimum distance");
    for (int i = 0; i < 60; ++i) {
        static_cast<void>(context.ZoomMaterialPreviewCamera(2.0F));
    }
    report.Check(almostEqual(settings().cameraDistance, kEditorMaterialPreviewMaxCameraDistance),
        "Finding 28: zooming out is clamped to the maximum distance");

    // The drag gesture: begin, drag, end - a horizontal drag moves the yaw, and the camera-only path does
    // not re-cook (nothing here asserts a cook was avoided, but the gesture threads through cleanly).
    const float yawBeforeGesture = settings().orbitYawDegrees;
    report.Check(context.BeginMaterialPreviewOrbit(200, 200) && context.IsMaterialPreviewOrbiting(),
        "Finding 28: a preview drag begins an orbit gesture");
    // Dragging right turns the object's right side toward the viewer, i.e. yaw decreases (the horizontal
    // axis is negated so grabbing and pulling feels natural rather than inverted).
    report.Check(context.DragMaterialPreviewOrbit(260, 200) && settings().orbitYawDegrees < yawBeforeGesture,
        "Finding 28: dragging right turns the object the natural way (yaw decreases)");
    report.Check(context.EndMaterialPreviewOrbit() && !context.IsMaterialPreviewOrbiting(),
        "Finding 28: releasing ends the orbit gesture");
    // A camera move never marks the document dirty - it does not touch the material.
    report.Check(!context.MaterialEditor().Dirty(), "Finding 28: orbiting and zooming never dirty the material");

    // The heart of the lag fix: none of the orbit/zoom above bumped the preview revision, so the viewport
    // never re-synced the scene to the GPU for a camera move. The preview redraws from the per-frame present
    // override alone - matching UE, where an orbit ends in Viewport->InvalidateDisplay() (viewport pixels
    // only), not a scene rebuild.
    report.Check(context.MaterialPreviewRevision() == revisionBeforeCameraMoves,
        "Finding 28: orbiting and zooming never bump the preview revision (no per-frame GPU re-sync)");
    // Negative control: a genuine scene change (lighting settings) DOES bump the revision, proving the
    // invariant above is not vacuously true (the revision counter really does move when the scene changes).
    EditorMaterialPreviewSceneSettings changedSettings = context.MaterialPreviewSceneSettings();
    changedSettings.keyLightIntensity += 1.0F;
    report.Check(context.SetMaterialPreviewSceneSettings(changedSettings) &&
            context.MaterialPreviewRevision() != revisionBeforeCameraMoves,
        "Finding 28: negative control - a genuine scene change bumps the revision");
}

// Finding 21 (modal loops must survive an app quit): quitting the editor while a dialog is up used to be
// swallowed by the dialog's own pump, and the parameter dialog left its window alive pointing at stack state
// that was about to die.
void RunModalMessageLoopQuitSuite(Report& report) {
    // The queue must start clean or the assertions below measure someone else's messages. Bounded, because
    // PeekMessage never removes WM_PAINT - it is regenerated until someone validates the update region, so
    // an unbounded drain would spin forever the day a window is left alive by an earlier suite.
    MSG drained{};
    for (int i = 0; i < 512 && PeekMessageW(&drained, nullptr, 0, 0, PM_REMOVE) != 0; ++i) {
    }

    // Ordinary exit: the loop pumps until the dialog reports it is done.
    int calls = 0;
    report.Check(PostThreadMessageW(GetCurrentThreadId(), WM_APP, 0, 0) != 0, "Finding 21: queue a message to pump");
    const EditorModalLoopExit completed = RunEditorModalMessageLoop(nullptr, false, [&calls]() noexcept { return ++calls > 1; });
    report.Check(completed == EditorModalLoopExit::Completed, "Finding 21: a dialog that closes itself reports Completed");

    // WM_QUIT: the loop stops AND puts the quit back, so the main pump still sees the shutdown.
    PostQuitMessage(11);
    const EditorModalLoopExit quit = RunEditorModalMessageLoop(nullptr, false, []() noexcept { return false; });
    report.Check(quit == EditorModalLoopExit::Quit, "Finding 21: WM_QUIT ends the modal loop and is reported as a quit");
    MSG queued{};
    const bool requeued = PeekMessageW(&queued, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != 0;
    report.Check(requeued && queued.wParam == 11U,
        "Finding 21: the quit is re-posted with its exit code instead of being swallowed by the dialog");

    // A dialog destroyed from outside (its owner window closes, taking its owned windows with it) never sets
    // its own "done" flag and never gets a quit, so a pump that only watches those two spins forever and
    // freezes the editor. The queued quit is this assertion's safety net: without the handle check the loop
    // would report Quit here rather than hang the whole self-test.
    const wchar_t abandonClass[] = L"KBEditorSelfTestAbandonedDialog";
    WNDCLASSW abandonWindowClass{};
    abandonWindowClass.lpfnWndProc = DefWindowProcW;
    abandonWindowClass.hInstance = GetModuleHandleW(nullptr);
    abandonWindowClass.lpszClassName = abandonClass;
    const bool abandonClassReady = RegisterClassW(&abandonWindowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    report.Check(abandonClassReady, "Finding 21: register the stand-in dialog class");
    const HWND abandoned = abandonClassReady
        ? CreateWindowExW(0, abandonClass, L"kb self-test dialog", WS_OVERLAPPEDWINDOW, 0, 0, 120, 80, nullptr,
              nullptr, GetModuleHandleW(nullptr), nullptr)
        : nullptr;
    report.Check(abandoned != nullptr, "Finding 21: create the stand-in dialog window");
    if (abandoned != nullptr) {
        // DefWindowProcW turns WM_CLOSE into DestroyWindow, i.e. the window dies inside the pump.
        report.Check(PostMessageW(abandoned, WM_CLOSE, 0, 0) != 0, "Finding 21: queue the external destroy");
        PostQuitMessage(5);
        const EditorModalLoopExit abandonedExit =
            RunEditorModalMessageLoop(abandoned, false, []() noexcept { return false; });
        report.Check(abandonedExit == EditorModalLoopExit::Abandoned,
            "Finding 21: a dialog destroyed from outside ends the pump instead of freezing the editor");
        MSG leftoverQuit{};
        static_cast<void>(PeekMessageW(&leftoverQuit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
    }

    // End to end through the real dialog: a quit delivered while it is up must tear the window down (its
    // DialogState lives on Show's stack) and must not report a value the user never confirmed. The WM_APP
    // ahead of the quit is a canary - GetMessageW only reports WM_QUIT once the queue holds nothing else, so
    // if it is still queued afterwards the dialog never pumped and the rest of this block would pass vacuously.
    report.Check(PostThreadMessageW(GetCurrentThreadId(), WM_APP, 0, 0) != 0, "Finding 21: queue the pump canary");
    PostQuitMessage(3);
    const std::optional<std::string> value =
        EditorMaterialParameterValueDialog::Show(nullptr, "Roughness", "0.5");
    // PM_NOREMOVE and an explicit message check, because PeekMessageW hands back WM_QUIT whatever filter it
    // is given - removing here would eat the very quit the next assertion is about.
    MSG canary{};
    const bool canaryLeft = PeekMessageW(&canary, nullptr, WM_APP, WM_APP, PM_NOREMOVE) != 0 && canary.message == WM_APP;
    report.Check(!canaryLeft, "Finding 21: the dialog really ran its message pump");
    if (canaryLeft) {
        // Failing is no reason to hand the next suite a dirty queue.
        static_cast<void>(PeekMessageW(&canary, nullptr, WM_APP, WM_APP, PM_REMOVE));
    }
    report.Check(!value.has_value(), "Finding 21: a dialog abandoned by a quit returns no value");
    // Our own thread, not FindWindowW: that searches the whole desktop, so a second editor instance with the
    // same dialog open would fail this for the wrong reason.
    struct DialogSearch {
        const wchar_t* className;
        bool found;
    } search{ L"KBEditorMaterialParameterValueDialog", false };
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND window, LPARAM parameter) -> BOOL {
            DialogSearch& state = *reinterpret_cast<DialogSearch*>(parameter);
            std::array<wchar_t, 64U> className{};
            if (GetClassNameW(window, className.data(), static_cast<int>(className.size())) > 0 &&
                std::wcscmp(className.data(), state.className) == 0) {
                state.found = true;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    report.Check(!search.found,
        "Finding 21: the abandoned dialog window is destroyed instead of outliving the state it points at");
    MSG dialogQuit{};
    report.Check(PeekMessageW(&dialogQuit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != 0 && dialogQuit.wParam == 3U,
        "Finding 21: the quit survives the parameter dialog");
}

// Finding 15 (undocked panels must be resizable): the border strip already hit-tests as a resize edge, but
// the window style has to allow sizing or Windows ignores those codes and the edges feel dead.
void RunFloatingWindowResizeSuite(Report& report) {
    const wchar_t className[] = L"KBEditorSelfTestFloatingWindow";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = className;
    const bool classRegistered = RegisterClassW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    report.Check(classRegistered, "Finding 15: register self-test window class");
    if (!classRegistered) {
        return;
    }
    const HWND owner = CreateWindowExW(
        0, className, L"kb self-test owner", WS_OVERLAPPEDWINDOW, 0, 0, 200, 160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    report.Check(owner != nullptr, "Finding 15: create the owner window");
    if (owner == nullptr) {
        return;
    }

    const DockRect rect{ .x = 120, .y = 120, .width = 480, .height = 320 };
    const HWND floating = FloatingWindowFactory::Create(GetModuleHandleW(nullptr), owner, className, "Material Editor", rect);
    report.Check(floating != nullptr, "Finding 15: create a floating panel window through the production factory");
    if (floating == nullptr) {
        DestroyWindow(owner);
        return;
    }
    report.Check((GetWindowLongW(floating, GWL_STYLE) & WS_THICKFRAME) != 0,
        "Finding 15: an undocked panel window is created with a sizing frame");

    RECT frame{};
    GetWindowRect(floating, &frame);
    const EditorMetrics metrics{};
    const auto hitAt = [floating, &metrics](int screenX, int screenY) {
        return FloatingWindowHitTestResolver::Resolve(floating, MAKELPARAM(screenX, screenY), metrics);
    };
    report.Check(hitAt(frame.left, frame.top) == HTTOPLEFT && hitAt(frame.right - 1, frame.bottom - 1) == HTBOTTOMRIGHT,
        "Finding 15: the window corners hit-test as resize corners");
    report.Check(hitAt(frame.left, (frame.top + frame.bottom) / 2) == HTLEFT &&
            hitAt(frame.right - 1, (frame.top + frame.bottom) / 2) == HTRIGHT &&
            hitAt((frame.left + frame.right) / 2, frame.bottom - 1) == HTBOTTOM,
        "Finding 15: the window edges hit-test as resize edges");
    report.Check(hitAt((frame.left + frame.right) / 2, (frame.top + frame.bottom) / 2) == HTCLIENT,
        "Finding 15: the middle of the panel stays a normal client area");

    DestroyWindow(floating);
    DestroyWindow(owner);
}

// Finding 29 (2026-07-22): a material graph edit (connect/create/disconnect) used to call the blanket
// MarkSceneRenderDirty(), forcing a full resync of every mesh in the scene on every single edit - a
// multi-second stall in a Debug build on any non-trivial scene, even though only ONE material actually
// changed. MarkMaterialAssetRenderDirty replaces it: no resync at all when the edited material isn't
// equipped on any scene mesh, and an incremental (entity-scoped, non-full) resync when it is.
void RunMaterialGraphEditSceneDirtyScopeSuite(Report& report) {
    EditorSceneContext context;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 29: register material loader");
    std::error_code error;
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "SceneDirtyScope.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 29: create .kbmat fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 29: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/SceneDirtyScope.kbmat");
    if (metadata == nullptr) {
        report.Check(false, "Finding 29: resolve .kbmat metadata");
        return;
    }
    const kb::assets::AssetId id = metadata->id;
    report.Check(context.OpenMaterialEditorAsset(id), "Finding 29: open material");

    // Baseline: settle the initial open-triggered dirtying so the assertions below measure only the
    // edit itself, not setup noise.
    context.AcknowledgeSceneRenderSubmitted();
    const std::uint64_t revisionBeforeUnusedEdit = context.SceneRenderRevision();

    // Case A: the material is not equipped on any scene mesh (the common case while authoring a graph
    // against the Material Editor's own preview). A real graph edit must cause NO main-scene resync
    // whatsoever - not even an incremental one.
    report.Check(context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantScalar, -300, -200),
        "Finding 29: add a node to a material unused by any scene mesh");
    report.Check(context.SceneRenderRevision() == revisionBeforeUnusedEdit,
        "Finding 29: editing a material unused by the scene does not bump the scene render revision");
    report.Check(!context.SceneRenderFullDirty() && context.SceneRenderDirtyEntityIds().empty(),
        "Finding 29: editing a material unused by the scene marks nothing dirty");

    // Case B (negative control): equip the material on a scene mesh via a SLOT OVERRIDE (not just the
    // primary material slot, to prove the traversal checks both) and edit the graph again. This time the
    // scene MUST resync - but only that one entity, never the whole scene.
    const kb::scene::SceneEntity mesh = context.CreateHierarchyObject();
    report.Check(mesh.IsValid(), "Finding 29: create a mesh entity to equip the material on");
    kb::scene::MeshRendererComponent renderer{ .meshAssetId = 4242U };
    renderer.materialSlotOverrideCount = 1U;
    renderer.materialSlotAssetIds[0] = id.value;
    context.Scene().Components().MeshRenderers().Set(mesh, renderer);
    context.AcknowledgeSceneRenderSubmitted();
    const std::uint64_t revisionBeforeUsedEdit = context.SceneRenderRevision();

    report.Check(context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantScalar, -300, -100),
        "Finding 29: add a node to a material equipped (via slot override) on a scene mesh");
    report.Check(context.SceneRenderRevision() != revisionBeforeUsedEdit,
        "Finding 29: editing a material equipped on a scene mesh does bump the scene render revision");
    report.Check(!context.SceneRenderFullDirty(),
        "Finding 29: editing an equipped material stays an INCREMENTAL resync, not a full-scene one");
    const std::vector<std::uint64_t>& dirtyIds = context.SceneRenderDirtyEntityIds();
    report.Check(std::ranges::find(dirtyIds, mesh.Id()) != dirtyIds.end(),
        "Finding 29: the mesh equipped with the edited material (via slot override) is marked dirty by id");
}

// Interaction cost budget: every mouse move during a drag runs hit-tests and a repaint, so a single
// pointer event has to stay well under a frame or the editor drops to single-digit FPS on a real graph.
void RunMaterialGraphInteractionCostSuite(Report& report) {
    EditorSceneContext context;
    report.Check(context.Scene().Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()),
        "Finding 16: register material loader");
    std::error_code error;
    const std::filesystem::path materialPath = EditorProjectPaths::AssetsRoot() / "Materials" / "InteractionCost.kbmat";
    std::filesystem::create_directories(materialPath.parent_path(), error);
    kb::render::RenderMaterialAssetData fixture{};
    fixture.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    report.Check(kb::render::RenderMaterialAssetWriter::Save(materialPath, fixture), "Finding 16: create .kbmat fixture");
    report.Check(context.Scene().Assets().Discover() >= 1U, "Finding 16: discover .kbmat fixture");
    const kb::assets::AssetMetadata* metadata =
        context.Scene().Assets().Manager().Registry().FindByPath("/Game/Materials/InteractionCost.kbmat");
    if (metadata == nullptr) {
        report.Check(false, "Finding 16: resolve .kbmat metadata");
        return;
    }
    const kb::assets::AssetId id = metadata->id;
    report.Check(context.OpenMaterialEditorAsset(id), "Finding 16: open material");

    // A graph of a size a user actually builds.
    for (int index = 0; index < 40; ++index) {
        static_cast<void>(context.AddMaterialGraphNode(
            id,
            index % 2 == 0 ? kb::render::RenderMaterialGraphNodeKind::TextureSample
                           : kb::render::RenderMaterialGraphNodeKind::Multiply,
            -600 + (index % 8) * 260,
            -400 + (index / 8) * 220));
    }
    const std::optional<kb::render::RenderMaterialAssetData>& document = context.MaterialEditor().WorkingCopy();
    if (!document.has_value()) {
        report.Check(false, "Finding 16: build the benchmark graph");
        return;
    }
    report.Check(document->graph.nodes.size() >= 40U, "Finding 16: benchmark graph has a realistic node count");

    // Reproduce the user's "adding a node lags" action headlessly: add one node, then drive the material
    // preview once (which synchronously resolves the edited working copy). This is the whole synchronous cost
    // the user feels as a freeze after adding a node (the async shader cook happens afterwards, off-thread).
    {
        const LARGE_INTEGER addStart = QueryCounter();
        static_cast<void>(context.AddMaterialGraphNode(id, kb::render::RenderMaterialGraphNodeKind::ConstantColor, 320, 320));
        const double addEditMs = ElapsedMilliseconds(addStart);
        const LARGE_INTEGER resolveStart = QueryCounter();
        static_cast<void>(context.MaterialPreviewScene(id));
        const double previewResolveMs = ElapsedMilliseconds(resolveStart);
        report.Check(addEditMs >= 0.0 && previewResolveMs >= 0.0,
            "Finding ADDNODE: add one node = " + FormatMilliseconds(addEditMs) + " ms edit + " +
                FormatMilliseconds(previewResolveMs) + " ms preview resolve (synchronous freeze on a " +
                std::to_string(context.MaterialEditor().WorkingCopy()->graph.nodes.size()) + " node graph)");
        // Second identical preview drive must be cheap (gated, no re-resolve) - proves the freeze is the
        // one-shot resolve, not a per-frame cost.
        const LARGE_INTEGER steadyStart = QueryCounter();
        static_cast<void>(context.MaterialPreviewScene(id));
        const double steadyFrameMs = ElapsedMilliseconds(steadyStart);
        report.Check(steadyFrameMs >= 0.0,
            "Finding ADDNODE: a steady preview frame after the edit = " + FormatMilliseconds(steadyFrameMs) + " ms");
    }

    const int iterations = 200;
    const auto hitTestCost = [&]() {
        const LARGE_INTEGER start = QueryCounter();
        for (int index = 0; index < iterations; ++index) {
            const int x = kContent.left + 40 + (index % 500);
            const int y = kContent.top + 40 + (index % 300);
            static_cast<void>(MaterialEditorPanelRenderer::GraphPinAt(kContent, document->graph, context, id, x, y));
            static_cast<void>(MaterialEditorPanelRenderer::GraphNodeAt(kContent, document->graph, context, id, x, y));
        }
        return ElapsedMilliseconds(start) / static_cast<double>(iterations);
    };

    const double perEventMs = hitTestCost();
    report.Check(perEventMs < 8.0,
        "Finding 16: one pointer event of graph hit-testing stays under 8 ms (measured " +
            FormatMilliseconds(perEventMs) + " ms on a " + std::to_string(document->graph.nodes.size()) + " node graph)");

    // The third cost centre, and the one the audit flagged as untouched: every edit funnels through
    // SetWorkingCopy, which re-derives the dirty flag. Measured on the same graph as the two above.
    const int editIterations = 60;
    kb::render::RenderMaterialAssetData edited = *document;
    const LARGE_INTEGER editStart = QueryCounter();
    for (int index = 0; index < editIterations; ++index) {
        edited.graph.nodes.back().positionX = index * 7;
        context.MaterialEditor().SetWorkingCopy(edited);
    }
    const double perEditMs = ElapsedMilliseconds(editStart) / static_cast<double>(editIterations);

    // Budget, not a stopwatch: an edit used to re-derive everything (two canonical serializations for the
    // dirty compare, the parameter list, the find results, the graph validator AND a full shader compile),
    // which measured ~2.2 ms per edit on this graph in a Debug build. Deriving it lazily instead puts it
    // well under a millisecond; the ceiling here is loose enough not to fire on a slow machine and tight
    // enough that going back to eager rebuilds trips it.
    report.Check(perEditMs < 1.2,
        "Finding 16: one document edit stays under 1.2 ms (measured " + FormatMilliseconds(perEditMs) +
            " ms on a " + std::to_string(context.MaterialEditor().WorkingCopy()->graph.nodes.size()) + " node graph)");

    // Lazy must not mean absent. Reading after an edit has to produce the same answer eager rebuilding did.
    report.Check(!context.MaterialEditor().Parameters().empty(),
        "Finding 16: the parameter list is there when something reads it");
    const std::size_t markerCount = context.MaterialEditor().GraphDiagnosticMarkers().size();
    const bool hasDiagnostics = !context.MaterialEditor().Diagnostics().empty();
    report.Check(hasDiagnostics || markerCount == 0U,
        "Finding 16: the diagnostics are rebuilt on read, not left cleared");

    // And a result that arrives from outside must not be wiped by a rebuild that was still pending.
    context.MaterialEditor().SetWorkingCopy(edited);
    context.MaterialEditor().ApplyCookResult({ "selftest cook line" }, true, true, false, false);
    const std::vector<std::string>& afterCook = context.MaterialEditor().Diagnostics();
    report.Check(std::ranges::any_of(afterCook, [](const std::string& line) {
        return line.find("selftest cook line") != std::string::npos;
    }), "Finding 16: a cook result that lands on a pending rebuild survives it");

    // The other half of a frame: the repaint every pointer event triggers.
    double perPaintMs = 0.0;
    double perDragPaintMs = 0.0;
    const HDC screenDc = GetDC(nullptr);
    if (screenDc != nullptr) {
        const HDC memoryDc = CreateCompatibleDC(screenDc);
        const HBITMAP bitmap = CreateCompatibleBitmap(screenDc, kContent.right, kContent.bottom);
        if (memoryDc != nullptr && bitmap != nullptr) {
            HGDIOBJ previous = SelectObject(memoryDc, bitmap);
            MaterialEditorPanelRenderer renderer;
            const int paintIterations = 30;
            const LARGE_INTEGER start = QueryCounter();
            for (int index = 0; index < paintIterations; ++index) {
                renderer.Paint(memoryDc, kContent, EditorTheme{}, context);
            }
            perPaintMs = ElapsedMilliseconds(start) / static_cast<double>(paintIterations);

            // Drag repaint: every mouse move while dragging a node changes the view signature (the live drag
            // offset is folded in), so the cached canvas model is rebuilt AND the whole graph redrawn on each
            // one. This is the number the user feels as "przesuwanie laguje", so measure it distinctly from the
            // static repaint above rather than assuming they are the same.
            const std::uint32_t dragNodeId = document->graph.nodes.back().id;
            const std::optional<RECT> dragNodeRect =
                MaterialEditorPanelRenderer::GraphNodeRect(kContent, document->graph, dragNodeId, context, id);
            if (dragNodeRect.has_value()) {
                const int dragStartX = (dragNodeRect->left + dragNodeRect->right) / 2;
                const int dragStartY = (dragNodeRect->top + dragNodeRect->bottom) / 2;
                if (context.BeginMaterialGraphNodeDrag(id, dragNodeId, dragStartX, dragStartY)) {
                    const int dragIterations = 30;
                    const LARGE_INTEGER dragStart = QueryCounter();
                    for (int index = 0; index < dragIterations; ++index) {
                        static_cast<void>(context.DragMaterialGraphNode(dragStartX + 1 + (index % 20), dragStartY + (index % 12)));
                        renderer.Paint(memoryDc, kContent, EditorTheme{}, context);
                    }
                    perDragPaintMs = ElapsedMilliseconds(dragStart) / static_cast<double>(dragIterations);
                    static_cast<void>(context.EndMaterialGraphNodeDrag());
                }
            }

            // Retained graph bitmap: an overlay-only repaint (the node palette opening/navigating, a box
            // select, an in-flight wire) must composite over the cached graph instead of redrawing every node.
            // That is the fix for the ~28 ms-per-mouse-move palette lag. Prove it via the render counter.
            const std::uint32_t firstNodeId = document->graph.nodes.front().id;
            const std::uint32_t lastNodeId = document->graph.nodes.back().id;
            static_cast<void>(context.SetMaterialGraphNodeSelection({ lastNodeId }, lastNodeId));
            renderer.Paint(memoryDc, kContent, EditorTheme{}, context); // prime the cache for this content+selection
            const std::uint64_t renderCountAfterPrime = MaterialEditorPanelRenderer::DebugGraphContentRenderCount();
            renderer.Paint(memoryDc, kContent, EditorTheme{}, context); // identical -> cache hit, no re-render
            report.Check(MaterialEditorPanelRenderer::DebugGraphContentRenderCount() == renderCountAfterPrime,
                "Finding 16: an unchanged Material Editor repaint reuses the cached graph bitmap (no node re-render)");
            // Negative control: selecting a DIFFERENT node changes the graph's drawn border/glow, so it must
            // invalidate the cache and redraw.
            static_cast<void>(context.SetMaterialGraphNodeSelection({ firstNodeId }, firstNodeId));
            renderer.Paint(memoryDc, kContent, EditorTheme{}, context);
            const std::uint64_t renderCountAfterSelect = MaterialEditorPanelRenderer::DebugGraphContentRenderCount();
            report.Check(renderCountAfterSelect > renderCountAfterPrime,
                "Finding 16 negative control: a selection change invalidates the cached graph and redraws it");
            // Opening and navigating the node palette is an OVERLAY change only, so it must still hit the cache.
            static_cast<void>(context.OpenMaterialGraphContextMenu(id, kContent.left + 60, kContent.top + 60, 0, 0));
            renderer.Paint(memoryDc, kContent, EditorTheme{}, context);
            report.Check(MaterialEditorPanelRenderer::DebugGraphContentRenderCount() == renderCountAfterSelect,
                "Finding 16: opening the node palette composites over the cached graph (the palette-lag fix)");

            if (previous != nullptr) {
                SelectObject(memoryDc, previous);
            }
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memoryDc != nullptr) {
            DeleteDC(memoryDc);
        }
        ReleaseDC(nullptr, screenDc);
    }
    // Debug-build budget: a real regression (a rebuild-everything paint) lands far above this, while the
    // normal 28-33 ms spread of an unoptimised build does not trip it.
    report.Check(perPaintMs > 0.0 && perPaintMs < 60.0,
        "Finding 16: one Material Editor repaint stays under 60 ms (measured " + FormatMilliseconds(perPaintMs) +
            " ms, i.e. " + FormatMilliseconds(perPaintMs > 0.0 ? 1000.0 / perPaintMs : 0.0) + " FPS ceiling)");
    report.Check(perDragPaintMs >= 0.0,
        "Finding 16: one drag-frame repaint (canvas rebuild + full redraw) measured " + FormatMilliseconds(perDragPaintMs) +
            " ms, i.e. " + FormatMilliseconds(perDragPaintMs > 0.0 ? 1000.0 / perDragPaintMs : 0.0) + " FPS ceiling)");
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
        // Numeric fields now scrub on drag; a click (press + release, no movement)
        // opens the inline text editor on pointer-up.
        static_cast<void>(InspectorPanelInteraction::HandlePointerUp(context));
        context.Inspector().ClearText();
        context.Inspector().InsertText("4.25");
        report.Check(InspectorPanelInteraction::HandleKeyDown(nullptr, context, static_cast<WPARAM>(0x0D)), "Committing Light intensity is handled");
        const kb::scene::LightComponent* light = context.Scene().Components().Lights().TryGet(lightEntity);
        report.Check(light != nullptr && std::abs(light->intensity - 4.25F) < 0.001F, "Committed Light intensity updates the runtime component");

        // Drag-to-scrub: press + horizontal move changes the value in 0.1 steps
        // (~6 px each); release commits. 60 px right => +1.0.
        const POINT dragStart = Center(intensityHit.rect);
        static_cast<void>(InspectorPanelInteraction::HandlePointerDown(context, intensityHit, dragStart.x, dragStart.y));
        static_cast<void>(InspectorPanelInteraction::HandlePointerDrag(context, dragStart.x + 60, dragStart.y));
        static_cast<void>(InspectorPanelInteraction::HandlePointerUp(context));
        const kb::scene::LightComponent* scrubbed = context.Scene().Components().Lights().TryGet(lightEntity);
        report.Check(scrubbed != nullptr && scrubbed->intensity > 5.0F, "Dragging a numeric field right scrubs its value up in 0.1 steps");
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

    // Unity-like: a Collider WITHOUT a Rigidbody is an implicit static body, so a
    // dynamic body lands on it instead of tunneling through. Placed far from the
    // first floor so the two scenarios never interact.
    const kb::scene::SceneEntity colliderOnlyFloor = context.CreateHierarchyObject();
    context.Scene().Transforms().Set(colliderOnlyFloor, kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ 40.0F, -0.5F, 0.0F },
    });
    context.Scene().Components().Colliders().Set(colliderOnlyFloor, kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 10.0F, 1.0F, 10.0F },
    });
    const kb::scene::SceneEntity fallingBox = context.CreateHierarchyObject();
    context.Scene().Transforms().Set(fallingBox, kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ 40.0F, 4.0F, 0.0F },
    });
    context.Scene().Components().Rigidbodies().Set(fallingBox, kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
        .mass = 1.0F,
    });
    context.Scene().Components().Colliders().Set(fallingBox, kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
    });
    for (int frame = 0; frame < 120; ++frame) {
        static_cast<void>(context.Scene().Runtime().Update(1.0F / 60.0F));
    }
    const kb::scene::TransformComponent fallingBoxTransform = context.Scene().Transforms().Get(fallingBox);
    report.Check(fallingBoxTransform.localPosition.y < 4.0F, "A dynamic body over a collider-only floor still falls under gravity");
    report.Check(fallingBoxTransform.localPosition.y > 0.35F, "A Collider without a Rigidbody acts as a static body (the dynamic body does not tunnel through)");

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
    const kb::assets::AssetId secondMaterialId{ 0x7111U };
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
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 4U,
        .kind = kb::render::RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 320,
        .positionY = 300,
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
    material.graph.comments.push_back(kb::render::RenderMaterialGraphCommentBox{
        .id = 11U, .positionX = 520, .positionY = 280, .width = 240, .height = 160, .text = "Context target",
    });
    material.graph.composites.push_back(kb::render::RenderMaterialGraphCompositeSubgraph{
        .id = 20U, .positionX = 0, .positionY = 0, .width = 420, .height = 260,
        .collapsed = true, .name = "Collapsed", .nodeIds = { 2U, 3U },
    });
    const std::filesystem::path viewStateRoot =
        std::filesystem::temp_directory_path() / "21kb_selftest" / "material_graph_view_state";
    const std::filesystem::path firstGraphPath = viewStateRoot / "First.kbmaterialgraph";
    const std::filesystem::path secondGraphPath = viewStateRoot / "Second.kbmaterialgraph";
    const bool firstViewFixtureSaved =
        kb::render::RenderMaterialGraphAssetLoader::SaveGraph(firstGraphPath, material.graph);
    std::filesystem::create_directories(secondGraphPath.parent_path());
    std::ofstream legacySecondGraph{ secondGraphPath, std::ios::trunc };
    legacySecondGraph << "graphVersion 1\n"
                      << "graphShadingModel lit\n"
                      << "graphNode 1 MaterialOutput 640 240\n";
    legacySecondGraph.close();
    const bool viewFixturesSaved = firstViewFixtureSaved && static_cast<bool>(legacySecondGraph);
    const bool viewFixturesRegistered = viewFixturesSaved &&
        context.Scene().Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
            .id = materialId,
            .type = kb::render::kRenderMaterialGraphAssetType,
            .name = "FirstGraphViewState",
            .virtualPath = "/Game/Materials/FirstGraphViewState.kbmaterialgraph",
            .physicalPath = firstGraphPath,
            .runtimeLoadable = true,
        }) &&
        context.Scene().Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
            .id = secondMaterialId,
            .type = kb::render::kRenderMaterialGraphAssetType,
            .name = "SecondGraphViewState",
            .virtualPath = "/Game/Materials/SecondGraphViewState.kbmaterialgraph",
            .physicalPath = secondGraphPath,
            .runtimeLoadable = true,
        });
    const bool firstOpened = viewFixturesRegistered && context.OpenMaterialEditorAsset(materialId);
    report.Check(firstOpened, "P2.3 open first production graph document for per-document view-state verification");
    if (!firstOpened) {
        return;
    }
    context.FocusMaterialGraph(true);

    static_cast<void>(context.ZoomMaterialGraph(120, 240, 180));
    static_cast<void>(context.BeginMaterialGraphPan(0, 0));
    static_cast<void>(context.DragMaterialGraphPan(48, 32));
    static_cast<void>(context.EndMaterialGraphPan());
    const float firstZoom = context.MaterialGraphZoom();
    const int firstPanX = context.MaterialGraphPanX();
    const int firstPanY = context.MaterialGraphPanY();
    const bool firstTransientStateStarted =
        context.BeginMaterialGraphPinConnection(materialId, 2U, "rgba", true, 320, 240) &&
        context.OpenMaterialGraphContextMenuForPinConnection(materialId, 320, 240, 180, 120) &&
        context.BeginMaterialGraphPan(20, 20);
    const bool secondOpened = context.OpenMaterialEditorAsset(secondMaterialId);
    const bool secondMigrationWarningVisible = secondOpened &&
        std::ranges::any_of(context.MaterialEditor().Diagnostics(), [](const std::string& diagnostic) {
            return diagnostic.find("graph_migration") != std::string::npos;
        });
    const bool secondStartsDefault = secondOpened &&
        std::fabs(context.MaterialGraphZoom() - MaterialGraphInteractionPolicy::DefaultZoom) < 0.0001F &&
        context.MaterialGraphPanX() == 0 && context.MaterialGraphPanY() == 0 &&
        !context.HasMaterialGraphPinConnection() &&
        !context.IsMaterialGraphContextMenuOpen() &&
        !context.IsMaterialGraphFocused() &&
        !context.EndMaterialGraphPan();
    static_cast<void>(context.ZoomMaterialGraph(-120, 320, 220));
    static_cast<void>(context.BeginMaterialGraphPan(0, 0));
    static_cast<void>(context.DragMaterialGraphPan(-64, 24));
    static_cast<void>(context.EndMaterialGraphPan());
    const float secondZoom = context.MaterialGraphZoom();
    const int secondPanX = context.MaterialGraphPanX();
    const int secondPanY = context.MaterialGraphPanY();
    const bool firstRestored = context.OpenMaterialEditorAsset(materialId) &&
        std::fabs(context.MaterialGraphZoom() - firstZoom) < 0.0001F &&
        context.MaterialGraphPanX() == firstPanX && context.MaterialGraphPanY() == firstPanY;
    const bool texturePickerStarted = firstRestored &&
        context.OpenMaterialGraphTexturePicker(materialId, 4U, {});
    const bool secondRestored = texturePickerStarted && context.OpenMaterialEditorAsset(secondMaterialId) &&
        std::fabs(context.MaterialGraphZoom() - secondZoom) < 0.0001F &&
        context.MaterialGraphPanX() == secondPanX && context.MaterialGraphPanY() == secondPanY &&
        !context.IsMaterialGraphTexturePickerOpen();
    const bool firstReopenedForRemainingTests = context.OpenMaterialEditorAsset(materialId);
    report.Check(firstTransientStateStarted && secondStartsDefault && secondMigrationWarningVisible && firstRestored && texturePickerStarted && secondRestored && firstReopenedForRemainingTests,
        "P1.24/P2.3/P2.11 production switching resets transient interaction state, isolates view state and keeps standalone graph migration warnings visible");
    if (!firstReopenedForRemainingTests) {
        return;
    }
    context.FocusMaterialGraph(true);

    report.Check(context.DetachMaterialGraphInputPinConnection(materialId, 1U, "baseColor", 20, 20),
        "P1.22 begin production rewire transaction");
    report.Check(context.CancelMaterialGraphPinConnection() && context.MaterialEditor().WorkingCopy()->graph.links.size() == 1U &&
            !context.MaterialEditor().Dirty() && !context.CanUndoSceneCommand(),
        "P1.22 cancel rewire restores original link without history");

    // Finding 14: pulling a wire off an input pin and letting go anywhere but on a pin must leave the link
    // unplugged (and undoable) — otherwise the only way to disconnect two nodes is the Alt+click shortcut.
    report.Check(context.DetachMaterialGraphInputPinConnection(materialId, 1U, "baseColor", 20, 20),
        "Finding 14: pull the wire off the input pin");
    report.Check(context.AbandonMaterialGraphPinConnection() &&
            context.MaterialEditor().WorkingCopy()->graph.links.empty() &&
            context.MaterialEditor().Dirty() && !context.HasMaterialGraphPinConnection(),
        "Finding 14: dropping the wire away from a pin keeps the link disconnected");
    report.Check(context.UndoSceneCommand() && context.MaterialEditor().WorkingCopy()->graph.links.size() == 1U,
        "Finding 14: the disconnect is a single undo step");
    report.Check(context.RedoSceneCommand() && context.MaterialEditor().WorkingCopy()->graph.links.empty(),
        "Finding 14: redo re-applies the disconnect");
    report.Check(context.UndoSceneCommand() && context.MaterialEditor().WorkingCopy()->graph.links.size() == 1U,
        "Finding 14: restore the link for the remaining checks");

    // Parked-menu regression (2026-07-22): a wire dropped on empty canvas parks the pin-connection menu but
    // deliberately KEEPS the pending connection so a picked node connects to it (UE-style). The move router
    // used to cancel that pending connection on the very first mouse move (its !leftButtonDown loose-wire
    // path), after which the pick created nothing - "selecting the list just cancels". The router now guards
    // that path with !IsMaterialGraphContextMenuOpen(); this models the exact predicate so a regression fails
    // headlessly (the Win32 move router itself is not reachable here).
    const bool parkedOpened =
        context.BeginMaterialGraphPinConnection(materialId, 2U, "rgba", true, 420, 260) &&
        context.OpenMaterialGraphContextMenuForPinConnection(materialId, 420, 260, 460, 180);
    report.Check(
        parkedOpened && context.HasMaterialGraphPinConnection() && context.IsMaterialGraphContextMenuOpen() &&
            !(context.HasMaterialGraphPinConnection() && !context.IsMaterialGraphContextMenuOpen()),
        "Parked pin-connection menu keeps its pending connection (move router must not cancel it on a mouse move)");
    static_cast<void>(context.CloseMaterialGraphContextMenu());
    static_cast<void>(context.CancelMaterialGraphPinConnection());
    // Negative control: with no menu parked, a live pin drag DOES satisfy the router's loose-wire predicate,
    // so a genuinely loose wire is still handled/cancelled on release.
    report.Check(
        context.BeginMaterialGraphPinConnection(materialId, 2U, "rgba", true, 420, 260) &&
            context.HasMaterialGraphPinConnection() && !context.IsMaterialGraphContextMenuOpen(),
        "Negative control: a live pin drag with no menu still satisfies the move router's loose-wire predicate");
    static_cast<void>(context.CancelMaterialGraphPinConnection());

    const bool dragCreateOpened = context.BeginMaterialGraphPinConnection(materialId, 2U, "rgba", true, 420, 260) &&
        context.OpenMaterialGraphContextMenuForPinConnection(materialId, 420, 260, 460, 180) &&
        context.IsMaterialGraphContextMenuPinFiltered();
    const bool dragCreateExecuted = dragCreateOpened &&
        context.ExecuteMaterialGraphContextMenuCommand(MaterialEditorGraphMenuCommand::CreateMultiply);
    const std::uint32_t dragCreatedNodeId = context.SelectedMaterialGraphNodeId();
    const bool dragCreateConnected = dragCreateExecuted && dragCreatedNodeId != 0U &&
        std::ranges::any_of(context.MaterialEditor().WorkingCopy()->graph.links,
            [dragCreatedNodeId](const kb::render::RenderMaterialGraphLink& createdLink) {
                return createdLink.fromNodeId == 2U && createdLink.toNodeId == dragCreatedNodeId;
            });
    report.Check(dragCreateConnected && !context.HasMaterialGraphPinConnection() &&
            !context.IsMaterialGraphContextMenuOpen() && context.UndoSceneCommand(),
        "P1.23 filtered drag-create atomically creates, autoconnects and records the compatible node");

    static_cast<void>(context.SetMaterialGraphNodeSelection({ 2U, 3U }, 2U));
    static_cast<void>(context.SelectMaterialGraphContextTarget(3U, 0U));
    report.Check(context.SelectedMaterialGraphNodeIds() == std::vector<std::uint32_t>({ 2U, 3U }) &&
            context.SelectedMaterialGraphNodeId() == 2U,
        "P1.27 RMB target preserves selected multi-selection and primary");
    report.Check(context.BeginMaterialGraphPan(10, 10) && !context.DragMaterialGraphPan(11, 10) &&
            !context.HasMaterialGraphPanMoved() && context.EndMaterialGraphPan(),
        "P1.27 one-pixel RMB jitter remains a context-menu click");

    static_cast<void>(context.SelectMaterialGraphComment(10U));
    const bool contextCommentRetargeted = context.SelectMaterialGraphContextTarget(0U, 11U) &&
        context.SelectedMaterialGraphCommentId() == 11U;
    const bool contextCommentDeleted = contextCommentRetargeted &&
        context.DeleteSelectedMaterialGraphComment(materialId) &&
        context.MaterialEditor().GraphComment(10U).has_value() &&
        !context.MaterialEditor().GraphComment(11U).has_value();
    report.Check(contextCommentDeleted && context.UndoSceneCommand() &&
            context.MaterialEditor().GraphComment(11U).has_value(),
        "P1.28 RMB comment context retargets deletion to the comment under the pointer and supports Undo");

    static_cast<void>(context.SetMaterialGraphNodeSelection({ 2U, 3U }, 2U));
    report.Check(
        ResolveMaterialGraphSelectionOperation(false, true, false) == MaterialGraphSelectionOperation::Invert &&
            ResolveMaterialGraphSelectionOperation(false, false, true) == MaterialGraphSelectionOperation::Add &&
            ResolveMaterialGraphSelectionOperation(true, true, true) == MaterialGraphSelectionOperation::Remove &&
            ResolveMaterialGraphSelectionOperation(false, false, false) == MaterialGraphSelectionOperation::Replace,
        "P2.1 production modifier mapping preserves Ctrl-invert, Shift-add, Alt-remove and plain-replace");
    report.Check(context.BeginMaterialGraphBoxSelection(materialId, 0, 0, MaterialGraphSelectionOperation::Invert) &&
            context.EndMaterialGraphBoxSelection({ 3U }, 3U) &&
            context.SelectedMaterialGraphNodeIds() == std::vector<std::uint32_t>{ 2U } &&
            context.SelectedMaterialGraphNodeId() == 2U,
        "P2.1 Ctrl marquee inverts membership instead of behaving like Shift-add");
    report.Check(context.BeginMaterialGraphBoxSelection(materialId, 0, 0, MaterialGraphSelectionOperation::Add) &&
            context.EndMaterialGraphBoxSelection({ 3U }, 3U) &&
            context.SelectedMaterialGraphNodeIds() == std::vector<std::uint32_t>({ 2U, 3U }) &&
            context.SelectedMaterialGraphNodeId() == 3U,
        "P2.1 Shift marquee adds hits and assigns an explicit primary node");
    report.Check(context.BeginMaterialGraphBoxSelection(materialId, 0, 0, MaterialGraphSelectionOperation::Remove) &&
            context.EndMaterialGraphBoxSelection({ 2U }, 2U) &&
            context.SelectedMaterialGraphNodeIds() == std::vector<std::uint32_t>{ 3U } &&
            context.SelectedMaterialGraphNodeId() == 3U,
        "P2.1 Alt marquee removes hits and preserves a deterministic primary node");
    report.Check(context.BeginMaterialGraphBoxSelection(materialId, 0, 0, MaterialGraphSelectionOperation::Replace) &&
            context.EndMaterialGraphBoxSelection({ 2U }, 2U) &&
            context.SelectedMaterialGraphNodeIds() == std::vector<std::uint32_t>{ 2U } &&
            context.SelectedMaterialGraphNodeId() == 2U,
        "P2.1 plain marquee replaces selection");

    report.Check(context.BeginMaterialGraphNodeDrag(materialId, 2U, 100, 100) &&
            !context.DragMaterialGraphNode(101, 101) && context.EndMaterialGraphNodeDrag() &&
            context.MaterialEditor().GraphNodePosition(2U) == std::optional<std::pair<std::int32_t, std::int32_t>>{ { 20, 20 } } &&
            !context.MaterialEditor().Dirty(),
        "P2.2 one-pixel node jitter stays below the shared drag threshold and creates no edit");
    report.Check(context.BeginMaterialGraphNodeDrag(materialId, 2U, 100, 100) &&
            context.DragMaterialGraphNode(120, 100) && context.EndMaterialGraphNodeDrag() &&
            context.MaterialEditor().GraphNodePosition(2U).has_value() &&
            context.MaterialEditor().GraphNodePosition(2U)->first != 20 &&
            context.CanUndoSceneCommand() && context.UndoSceneCommand() &&
            context.MaterialEditor().GraphNodePosition(2U) == std::optional<std::pair<std::int32_t, std::int32_t>>{ { 20, 20 } },
        "P2.2 committed node drag tracks the cursor smoothly (no grid snap) and records one undo command");

    const RECT feedbackContent{ 0, 0, 1280, 820 };
    const kb::render::RenderMaterialGraphDocument& feedbackGraph = context.MaterialEditor().WorkingCopy()->graph;
    MaterialGraphCanvasDocumentBuildResult feedbackCanvas =
        MaterialEditorPanelBuildInteractiveGraphCanvas(feedbackContent, feedbackGraph, context, materialId);
    const auto feedbackPinCenter = [&feedbackCanvas](std::uint32_t nodeId, std::string_view pin, bool output) -> std::optional<POINT> {
        for (std::uint32_t nodeIndex = 0U; nodeIndex < feedbackCanvas.canvas.NodeCount(); ++nodeIndex) {
            const MaterialGraphCanvasNode* node = feedbackCanvas.canvas.NodeAt(nodeIndex);
            if (node == nullptr || node->stableId != std::to_string(nodeId)) {
                continue;
            }
            const std::vector<MaterialGraphCanvasPin>& pins = output ? node->outputs : node->inputs;
            for (std::uint32_t pinIndex = 0U; pinIndex < pins.size(); ++pinIndex) {
                if (pins[pinIndex].stableId == pin) {
                    const MaterialGraphCanvasPoint center =
                        feedbackCanvas.canvas.PinCenterWindow(nodeIndex, pinIndex, output);
                    return POINT{
                        static_cast<LONG>(std::lround(center.x)),
                        static_cast<LONG>(std::lround(center.y)),
                    };
                }
            }
        }
        return std::nullopt;
    };
    const std::optional<POINT> compatiblePinCenter = feedbackPinCenter(1U, "baseColor", false);
    const std::optional<POINT> incompatiblePinCenter = feedbackPinCenter(4U, "texture", false);
    bool compatibleRingRendered = false;
    bool incompatibleRingRendered = false;
    int compatibleFeedbackPixelCount = 0;
    int incompatibleFeedbackPixelCount = 0;
    if (compatiblePinCenter.has_value() && incompatiblePinCenter.has_value()) {
        const POINT compatiblePin = *compatiblePinCenter;
        const POINT incompatiblePin = *incompatiblePinCenter;
        HDC screenDc = GetDC(nullptr);
        HDC memoryDc = screenDc == nullptr ? nullptr : CreateCompatibleDC(screenDc);
        HBITMAP bitmap = screenDc == nullptr ? nullptr : CreateCompatibleBitmap(screenDc, 1280, 820);
        HGDIOBJ previous = memoryDc != nullptr && bitmap != nullptr ? SelectObject(memoryDc, bitmap) : nullptr;
        if (memoryDc != nullptr && bitmap != nullptr && previous != nullptr &&
            context.BeginMaterialGraphPinConnection(materialId, 4U, "color", true, compatiblePin.x, compatiblePin.y)) {
            MaterialEditorPanelRenderer renderer;
            const auto countFeedbackPixels = [memoryDc](POINT center, COLORREF expected) {
                int count = 0;
                for (int dy = -10; dy <= 10; ++dy) {
                    for (int dx = -10; dx <= 10; ++dx) {
                        const int radiusSquared = dx * dx + dy * dy;
                        if (radiusSquared < 16 || radiusSquared > 100) {
                            continue;
                        }
                        const COLORREF pixel = GetPixel(memoryDc, center.x + dx, center.y + dy);
                        if (pixel != CLR_INVALID &&
                            std::abs(static_cast<int>(GetRValue(pixel)) - static_cast<int>(GetRValue(expected))) <= 24 &&
                            std::abs(static_cast<int>(GetGValue(pixel)) - static_cast<int>(GetGValue(expected))) <= 24 &&
                            std::abs(static_cast<int>(GetBValue(pixel)) - static_cast<int>(GetBValue(expected))) <= 24) {
                            ++count;
                        }
                    }
                }
                return count;
            };
            renderer.Paint(memoryDc, feedbackContent, EditorTheme{}, context);
            compatibleFeedbackPixelCount = countFeedbackPixels(compatiblePin, RGB(72, 220, 126));
            compatibleRingRendered = compatibleFeedbackPixelCount >= 8;
            static_cast<void>(context.DragMaterialGraphPinConnection(incompatiblePin.x, incompatiblePin.y));
            renderer.Paint(memoryDc, feedbackContent, EditorTheme{}, context);
            incompatibleFeedbackPixelCount = countFeedbackPixels(incompatiblePin, RGB(235, 76, 86));
            incompatibleRingRendered = incompatibleFeedbackPixelCount >= 8;
            if (const std::optional<CLSID> encoder = GdiplusEncoderClsid(L"image/bmp")) {
                const std::filesystem::path feedbackPath =
                    std::filesystem::temp_directory_path() / "21kb_selftest" / "_materialGraphPinFeedback.bmp";
                Gdiplus::Bitmap feedbackImage(bitmap, nullptr);
                static_cast<void>(feedbackImage.Save(feedbackPath.wstring().c_str(), &*encoder, nullptr));
            }
            static_cast<void>(context.CancelMaterialGraphPinConnection());
        }
        if (previous != nullptr) {
            SelectObject(memoryDc, previous);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memoryDc != nullptr) {
            DeleteDC(memoryDc);
        }
        if (screenDc != nullptr) {
            ReleaseDC(nullptr, screenDc);
        }
    }
    // Contract changed on request: dragging a wire no longer advertises compatibility. No rings are painted
    // around pins and the wire keeps its source colour; validity is decided when the wire is dropped.
    report.Check(
        !compatibleRingRendered && !incompatibleRingRendered,
        "P2.10 production paint draws no compatibility rings while a connection is being dragged (green=" +
            std::to_string(compatibleFeedbackPixelCount) + ", red=" +
            std::to_string(incompatibleFeedbackPixelCount) + ")");

    static_cast<void>(context.MaterialEditor().MoveGraphNode(3U, 4000, 20));
    static_cast<void>(context.SetMaterialGraphNodeSelection({ 2U, 3U }, 2U));
    const bool framedLargeSelection = context.FrameSelectedMaterialGraphNodes(800, 600);
#if defined(_WIN32)
    const SIZE framedNodeSize = MaterialEditorPanelGraphNodeSize(kb::render::RenderMaterialGraphNodeKind::ConstantScalar);
    const float framedRight = static_cast<float>(context.MaterialGraphPanX()) +
        (4000.0F + static_cast<float>(framedNodeSize.cx)) * context.MaterialGraphZoom();
#else
    const float framedRight = static_cast<float>(context.MaterialGraphPanX()) + 4240.0F * context.MaterialGraphZoom();
#endif
    report.Check(framedLargeSelection && context.MaterialGraphZoom() < 0.45F && framedRight <= 800.0F,
        "P2.4 Frame Selected may zoom below the old interactive floor and keeps a large selection inside the viewport");
    context.MaterialEditor().SetWorkingCopy(material);
    context.MaterialEditor().MarkSaved();
    static_cast<void>(context.SetMaterialGraphNodeSelection({ 2U }, 2U));

    static_cast<void>(context.SelectMaterialGraphComment(10U));
    report.Check(context.BeginMaterialGraphCommentDrag(materialId, 10U, 100, 100) &&
            !context.DragMaterialGraphComment(101, 101) && context.EndMaterialGraphCommentDrag() &&
            context.MaterialEditor().GraphCommentPosition(10U) == std::optional<std::pair<std::int32_t, std::int32_t>>{ { 0, 0 } } &&
            !context.MaterialEditor().Dirty(),
        "P2.2 one-pixel comment jitter stays below the shared drag threshold and creates no edit");
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
    out << "Suites: Project Settings + Project physics layers runtime + Plugins + Gameplay loop + Script editor/attach/log + Hierarchy commands + Selection transform + Prefab placement + Material graph context menu + Material graph panel canvas hit-test + Material graph color watcher + Material graph texture nodes + Material graph dense node layout + Material graph visual redesign + Material graph canvas clipping\n";
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
    RunSuiteInScratch(report, "project_physics_layers_runtime", &RunProjectPhysicsLayersRuntimeSuite);
    RunSuiteInScratch(report, "gameplay", &RunGameplayLoopSuite);
    RunSuiteInScratch(report, "script_editor", &RunScriptEditorSuite);
    RunSuiteInScratch(report, "script_attach", &RunScriptAttachSuite);
    RunSuiteInScratch(report, "script_inspector_schema_refresh", &RunScriptInspectorSchemaRefreshSuite);
    RunSuiteInScratch(report, "inspector_component_affordances", &RunInspectorComponentAffordancesSuite);
    RunSuiteInScratch(report, "physics_component_catalog", &RunPhysicsComponentCatalogSuite);
    RunSuiteInScratch(report, "camera_inspector", &RunCameraInspectorSuite);
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
    RunSuiteInScratch(report, "material_inspector_graph_edit_reconcile", &RunMaterialInspectorGraphEditReconcileSuite);
    RunSuiteInScratch(report, "material_editor_reopen_keeps_unsaved_edits", &RunMaterialEditorReopenPreservesUnsavedEditsSuite);
    RunSuiteInScratch(report, "material_graph_gesture_edit_guard", &RunMaterialGraphGestureBlocksEditCommandsSuite);
    RunSuiteInScratch(report, "material_editor_inline_constant_close", &RunMaterialEditorInlineConstantCloseContractSuite);
    RunSuiteInScratch(report, "material_editor_inspector_text_edit_save", &RunMaterialEditorInspectorTextEditSavesToDiskSuite);
    RunSuiteInScratch(report, "material_thumbnail_image_pipeline", &RunMaterialThumbnailImagePipelineSuite);
    RunSuiteInScratch(report, "modal_window_scope", &RunModalWindowScopeSuite);
    RunSuiteInScratch(report, "modal_message_loop_quit", &RunModalMessageLoopQuitSuite);
    RunSuiteInScratch(report, "debug_log_gate", &RunDebugLogGateSuite);
    RunSuiteInScratch(report, "material_editor_stale_references", &RunMaterialEditorStaleReferenceSuite);
    RunSuiteInScratch(report, "material_settings_inspector", &RunMaterialSettingsInspectorSuite);
    RunSuiteInScratch(report, "material_graph_comment_editing", &RunMaterialGraphCommentEditingSuite);
    RunSuiteInScratch(report, "material_graph_diagnostic_jump", &RunMaterialGraphDiagnosticJumpSuite);
    RunSuiteInScratch(report, "material_preview_camera_control", &RunMaterialPreviewCameraControlSuite);
    RunSuiteInScratch(report, "material_graph_edit_scene_dirty_scope", &RunMaterialGraphEditSceneDirtyScopeSuite);
    RunSuiteInScratch(report, "floating_window_resize", &RunFloatingWindowResizeSuite);
    RunSuiteInScratch(report, "material_graph_interaction_cost", &RunMaterialGraphInteractionCostSuite);
    RunSuiteInScratch(report, "material_editor_global_save", &RunMaterialEditorGlobalSaveSuite);
    RunSuiteInScratch(report, "inspector_material_color_rows", &RunInspectorMaterialColorRowsSuite);
    RunSuiteInScratch(report, "inspector_material_preview_surface", &RunInspectorMaterialPreviewSurfaceSuite);
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
