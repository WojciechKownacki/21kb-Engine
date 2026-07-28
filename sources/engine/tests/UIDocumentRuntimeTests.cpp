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
#include "engine/script/ScriptRuntimeHost.hpp"

#include <filesystem>
#include <fstream>

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

    const auto transientPanel = scene.UIDocuments().QueueCreate(owner.Entity(), kb::scene::UIRuntimeElementDesc{
        .parentId = 2U,
        .name = "TransientPanel",
        .styleClass = "hud",
        .visible = true,
    });
    Require(transientPanel.has_value() && !scene.UIDocuments().HasElement(owner.Entity(), *transientPanel) &&
            scene.UIDocuments().QueueHide(owner.Entity(), 2U),
        "UI mutations were not accepted through the deferred runtime queue");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().HasElement(owner.Entity(), *transientPanel) &&
            scene.UIDocuments().ElementCount(owner.Entity()) == 3U &&
            !scene.UIDocuments().Visible(owner.Entity(), 2U),
        "UI command FIFO did not apply create/hide at its frame boundary");
    Require(scene.UIDocuments().QueueDestroy(owner.Entity(), *transientPanel) &&
            scene.UIDocuments().QueueShow(owner.Entity(), 2U),
        "UI destroy/show commands were not accepted for a live runtime element");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(!scene.UIDocuments().HasElement(owner.Entity(), *transientPanel) &&
            scene.UIDocuments().ElementCount(owner.Entity()) == 2U &&
            scene.UIDocuments().Visible(owner.Entity(), 2U),
        "UI command FIFO did not apply destroy/show at its frame boundary");

    const std::filesystem::path luaPath = root / "Assets" / "Logic" / "UIQueue.lua";
    std::filesystem::create_directories(luaPath.parent_path());
    std::ofstream luaFile{ luaPath, std::ios::binary | std::ios::trunc };
    luaFile << R"(
local phase = 0
local element = 0

function Tick(self, dt)
    if phase == 0 then
        element = UI.Create(1, "LuaPanel", { styleClass = "hud", visible = true })
        UI.Hide(2)
        phase = 1
    elseif phase == 1 then
        UI.Destroy(element)
        UI.Show(2)
        phase = 2
    end
end
)";
    luaFile.close();
    Require(luaFile.good() && scene.Assets().Discover() == 3U, "UI queue Lua behaviour was not discovered as a production script asset");
    const auto* luaMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/UIQueue.lua");
    Require(luaMetadata != nullptr, "UI queue Lua behaviour was not registered by the project asset registry");
    scene.Components().Behaviours().Set(owner.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = luaMetadata->id.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    kb::script::ScriptRuntimeHost scriptHost{ scene };
    Require(scriptHost.Succeeded() && scriptHost.InstallSceneSystem(), "UI script runtime host could not install the UI library module");
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().ElementCount(owner.Entity()) == 3U && !scene.UIDocuments().Visible(owner.Entity(), 2U),
        "Lua UI.Create/UI.Hide did not reach the queued scene runtime tree");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.UIDocuments().ElementCount(owner.Entity()) == 2U && scene.UIDocuments().Visible(owner.Entity(), 2U),
        "Lua UI.Destroy/UI.Show did not reach the queued scene runtime tree");

    const std::filesystem::path scenePath = root / "UIDocument.21kbscene";
    Require(kb::scene::SceneDocumentService::Save(scene, scenePath, "UIDocument"),
        "Scene document with UIDocument component could not be saved");
    kb::scene::Scene loaded;
    Require(loaded.Assets().MountProject(root) && loaded.Assets().Discover() == 3U &&
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
