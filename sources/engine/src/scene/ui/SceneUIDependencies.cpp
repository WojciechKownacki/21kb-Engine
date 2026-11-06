#include "engine/scene/SceneUIDependencies.hpp"

#include "engine/assets/AssetImportService.hpp"
#include "engine/platform/FileSystemPath.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/scene/SceneUIComponents.hpp"
#include "engine/ui/UIComponentCatalog.hpp"

#include <array>

namespace kb::scene {
namespace {

[[nodiscard]] UIDependencyResult Failure(std::string error) { return UIDependencyResult{ .succeeded = false, .error = std::move(error) }; }

[[nodiscard]] UIDependencyResult CompleteFont(Scene& scene, SceneEntity entity) {
    auto ui = scene.Components().UI();
    const UIText* text = ui.TryGet<UIText>(entity);
    if (text == nullptr || text->fontAssetId != 0U) return {};
    const std::filesystem::path font = BundledUIFontPath();
    std::error_code missing;
    if (font.empty() || !std::filesystem::is_regular_file(font, missing)) {
        return Failure("Cannot give the Text a font: the bundled UI font was not found at " + font.generic_string() +
            ". Fix: reinstall the engine so Content/EditorShell/Fonts/DejaVuSans.ttf sits beside the executable, or set the Text's fontAssetId to an imported font.");
    }
    const std::array fontFiles{ font };
    const auto imported = kb::assets::AssetImportService::ImportFiles(scene.Assets().Manager(), fontFiles, "/Game/UI/Fonts");
    if (imported.items.empty() || !imported.items.front().Succeeded()) {
        return Failure("Cannot provision the bundled UI font: " +
            (imported.items.empty() ? std::string{ "import returned no result" } : imported.items.front().error));
    }
    UIText authoredText = *text;
    authoredText.fontAssetId = imported.items.front().id.value;
    ui.Set(entity, authoredText);
    return {};
}

[[nodiscard]] UIDependencyResult CompleteCanvas(Scene& scene, SceneEntity entity) {
    auto ui = scene.Components().UI();
    if (CaptureSceneUIComponents(ui, entity).Empty()) return {};
    if (!ui.Has<UIRectTransform>(entity)) ui.Set(entity, UIRectTransform{});
    SceneEntity root = entity;
    for (SceneEntity ancestor = entity; ancestor.IsValid(); ancestor = scene.Hierarchy().Parent(ancestor)) {
        if (ui.Has<UICanvas>(ancestor)) {
            // Between the widget and its Canvas every object needs a Rect Transform for the layout to pass through.
            for (SceneEntity bridge = entity;; bridge = scene.Hierarchy().Parent(bridge)) {
                if (!ui.Has<UIRectTransform>(bridge)) {
                    UIRectTransform rect;
                    rect.anchorMax = { 1.0F, 1.0F };
                    rect.offsetMax = {};
                    ui.Set(bridge, rect);
                }
                if (bridge == ancestor) break;
            }
            return {};
        }
        if (ui.Has<UIRectTransform>(ancestor)) root = ancestor;
    }
    const SceneEntity parent = scene.Hierarchy().Parent(root);
    if (!parent.IsValid()) {
        for (const SceneEntity existing : scene.Hierarchy().RootEntities()) {
            if (existing != root && ui.Has<UICanvas>(existing) && ui.Has<UIRectTransform>(existing)) {
                return scene.Hierarchy().SetParent(root, existing) ? UIDependencyResult{} : Failure("The widget could not move under the scene's Canvas.");
            }
        }
    }
    const SceneEntity canvas = scene.Entities().CreateEntity(SceneObjectDesc{ .name = "Canvas" });
    if (!canvas.IsValid()) return Failure("A Canvas for the widget could not be created.");
    ApplySceneUIComponents(ui, canvas, BuildUIComponentPreset(UIComponentPreset::Canvas));
    if ((parent.IsValid() && !scene.Hierarchy().SetParent(canvas, parent)) || !scene.Hierarchy().SetParent(root, canvas)) {
        return Failure("The widget could not move under its new Canvas.");
    }
    return {};
}

} // namespace

std::filesystem::path BundledUIFontPath() {
    const std::filesystem::path executable = kb::platform::RunningExecutablePath();
    return executable.empty() ? std::filesystem::path{} : executable.parent_path() / "Content" / "EditorShell" / "Fonts" / "DejaVuSans.ttf";
}

UIDependencyResult CompleteUIDependencies(Scene& scene, SceneEntity entity) {
    if (UIDependencyResult font = CompleteFont(scene, entity); !font.succeeded) return font;
    return CompleteCanvas(scene, entity);
}

} // namespace kb::scene
