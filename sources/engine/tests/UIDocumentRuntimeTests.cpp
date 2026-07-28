#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneUIDocuments.hpp"
#include "engine/scene/UIAssetIO.hpp"

#include <filesystem>

namespace kb::tests {

void RunUIDocumentRuntimeTests() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-ui-document-runtime-tests";
    std::filesystem::remove_all(root);
    const std::filesystem::path stylePath = root / "Assets" / "UI" / "Default.kbuistyle";
    const std::filesystem::path documentPath = root / "Assets" / "UI" / "Hud.kbui";
    std::filesystem::create_directories(stylePath.parent_path());
    Require(kb::scene::UIAssetIO::SaveStyle(stylePath, kb::scene::UIStyleAsset{
        .name = "Default",
        .classes = { "hud", "label" },
    }), "UIStyle production asset could not be saved");

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root), "UI document project mount failed");
    Require(scene.Assets().Discover() == 1U, "UI style discovery failed");
    const auto* styleMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/UI/Default.kbuistyle");
    Require(styleMetadata != nullptr && styleMetadata->type == kb::scene::kUIStyleAssetType,
        "UI style asset was not classified by the production registry");
    const kb::assets::AssetId styleAssetId = styleMetadata->id;
    const kb::scene::UIDocument document{
        .styleAssetId = styleAssetId.value,
        .elements = {
            { .id = 1U, .parentId = 0U, .name = "HUD", .styleClass = "hud", .visible = true },
            { .id = 2U, .parentId = 1U, .name = "Score", .styleClass = "label", .visible = true },
        },
        .bindings = {
            { .elementId = 2U, .property = "text", .sourcePath = "player.score",
              .valueType = kb::scene::UIDataValueType::Number,
              .direction = kb::scene::UIBindingDirection::OneWay },
        },
    };
    Require(kb::scene::UIAssetIO::SaveDocument(documentPath, document), "UIDocument production asset could not be saved");
    Require(scene.Assets().Discover() == 2U, "UIDocument discovery failed");
    const auto* documentMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/UI/Hud.kbui");
    Require(documentMetadata != nullptr && documentMetadata->type == kb::scene::kUIDocumentAssetType &&
            documentMetadata->dependencies.size() == 1U && documentMetadata->dependencies.front() == styleAssetId,
        "UIDocument did not register its UIStyle dependency");

    const kb::scene::SceneObject owner = scene.Entities().CreateObject({ .name = "HUD Owner" });
    scene.Components().UIDocuments().Set(owner.Entity(), kb::scene::UIDocumentComponent{
        .documentAssetId = documentMetadata->id.value,
        .enabled = true,
    });
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().Exists(owner.Entity()) &&
            scene.UIDocuments().Asset(owner.Entity()) == documentMetadata->id.value &&
            scene.UIDocuments().Root(owner.Entity()) == 1U &&
            scene.UIDocuments().ElementCount(owner.Entity()) == 2U &&
            scene.UIDocuments().HasElement(owner.Entity(), 2U) &&
            scene.UIDocuments().StyleIsResolved(owner.Entity()),
        "Scene runtime did not derive the UI tree from UIDocument and UIStyle assets");

    const std::filesystem::path scenePath = root / "UIDocument.21kbscene";
    Require(kb::scene::SceneDocumentService::Save(scene, scenePath, "UIDocument"),
        "Scene document with UIDocument component could not be saved");
    kb::scene::Scene loaded;
    Require(loaded.Assets().MountProject(root) && loaded.Assets().Discover() == 2U &&
            kb::scene::SceneDocumentService::LoadFileIntoScene(loaded, scenePath),
        "Scene document with UIDocument component could not be reloaded");
    static_cast<void>(loaded.Runtime().Update(0.0F));
    const auto roots = loaded.Hierarchy().RootEntities();
    Require(roots.size() == 1U && loaded.Components().UIDocuments().TryGet(roots.front()) != nullptr &&
            loaded.UIDocuments().Exists(roots.front()) && loaded.UIDocuments().Root(roots.front()) == 1U &&
            loaded.UIDocuments().StyleIsResolved(roots.front()),
        "Project -> scene reload -> UIDocument component -> UI runtime lost its canonical document tree");
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
