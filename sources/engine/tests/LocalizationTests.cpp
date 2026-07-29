#include "TestSupport.hpp"

#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/localization/LocalizationCatalogIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneLocalization.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <filesystem>

namespace kb::tests {

void RunLocalizationTests() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-localization-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Assets" / "Localization");
    const kb::localization::LocalizationCatalog catalog{
        .fallbackLanguage = "en",
        .languages = {
            { "en", {
                { "menu.play", { .text = "Play" } },
                { "menu.quit", { .text = "Quit" } },
                { "hud.coins", { .plurals = { { "one", "{count} coin" }, { "other", "{count} coins" } } } },
            } },
            { "pl", {
                { "menu.play", { .text = "Graj" } },
                { "hud.coins", { .plurals = { { "one", "{count} moneta" }, { "few", "{count} monety" }, { "many", "{count} monet" }, { "other", "{count} monety" } } } },
            } },
        },
    };
    const std::filesystem::path catalogPath = root / "Assets" / "Localization" / "Game.kbloc";
    Require(kb::localization::LocalizationCatalogIO::Save(catalogPath, catalog), "Localization catalog writer rejected valid authored messages");

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root) && scene.Assets().Discover() == 1U, "Localization asset was not discovered through the production registry");
    const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Localization/Game.kbloc");
    Require(metadata != nullptr && metadata->type == kb::localization::kLocalizationCatalogAssetType, "Localization asset type was not registered");
    Require(scene.Localization().SetCatalog(metadata->id.value), "Scene localization rejected a valid catalog asset");
    Require(scene.Localization().Translate("menu.play") == "Play", "Fallback language did not resolve the default translation");
    Require(scene.Localization().SetLanguage("pl"), "Scene localization rejected an authored language");
    Require(scene.Localization().Translate("menu.play") == "Graj", "Selected localization language did not override fallback text");
    Require(scene.Localization().Translate("menu.quit") == "Quit", "Missing selected-language text did not fall back to the catalog language");
    Require(scene.Localization().FormatPlural("hud.coins", 1) == "1 moneta" &&
            scene.Localization().FormatPlural("hud.coins", 2) == "2 monety" &&
            scene.Localization().FormatPlural("hud.coins", 5) == "5 monet",
        "Polish plural selection or count formatting was incorrect");
    Require(scene.Localization().Translate("missing.key") == "missing.key", "Missing localization keys must remain visible to content authors");

    kb::script::ScriptRuntimeHost host{ scene };
    Require(host.Succeeded() && host.Functions().FindSignature("Localization.Translate") != nullptr &&
            host.Functions().FindSignature("Localization.FormatPlural") != nullptr,
        "Localization script functions were not registered through the production library module");
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
