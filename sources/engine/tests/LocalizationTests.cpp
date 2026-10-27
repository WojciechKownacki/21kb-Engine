#include "TestSupport.hpp"

#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/localization/LocalizationCatalogIO.hpp"
#include "engine/localization/PluralRules.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneLocalization.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include "engine/localization/LocalizationCatalogAssetLoader.hpp"
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::tests {
namespace {

struct PluralExpectation {
    std::string_view number;
    std::string_view category;
};

void RequirePlurals(std::string_view language, std::initializer_list<PluralExpectation> expectations) {
    for (const PluralExpectation& expectation : expectations) {
        const std::optional<kb::localization::PluralOperands> operands = kb::localization::ParsePluralOperands(expectation.number);
        Require(operands.has_value(), ("Plural test number did not parse: " + std::string{ expectation.number }).c_str());
        const std::string_view selected =
            kb::localization::PluralCategoryName(kb::localization::SelectPluralCategory(language, *operands));
        const std::string message = "CLDR plural category of " + std::string{ expectation.number } + " in " +
            std::string{ language } + " must be " + std::string{ expectation.category } + ", was " + std::string{ selected };
        Require(selected == expectation.category, message.c_str());
    }
}

void TestPluralOperands() {
    using kb::localization::PluralOperands;
    using kb::localization::ParsePluralOperands;
    Require(ParsePluralOperands("1.50") == PluralOperands{ .i = 1U, .f = 50U, .t = 5U, .v = 2U, .w = 1U },
        "Plural operands of 1.50 must keep the visible trailing zero");
    Require(ParsePluralOperands("-12") == PluralOperands{ .i = 12U }, "Plural operands use the absolute value");
    Require(ParsePluralOperands("1.2c6") == PluralOperands{ .i = 1200000U, .e = 6U },
        "A compact exponent must expand the integer digits and set the exponent operand");
    Require(ParsePluralOperands("1.25c1") == PluralOperands{ .i = 12U, .f = 5U, .t = 5U, .v = 1U, .w = 1U, .e = 1U },
        "A compact exponent moves fraction digits into the integer part");
    Require(ParsePluralOperands("0.00") == PluralOperands{ .v = 2U }, "Zero fraction digits stay visible");
    Require(!ParsePluralOperands("").has_value() && !ParsePluralOperands("1.").has_value() &&
            !ParsePluralOperands(".5").has_value() && !ParsePluralOperands("1,5").has_value() &&
            !ParsePluralOperands("abc").has_value() && !ParsePluralOperands("1e").has_value() &&
            !ParsePluralOperands("1234567890123456789").has_value(),
        "Malformed or oversized plural numbers must be rejected");
    Require(kb::localization::PluralOperandsFromInteger(std::numeric_limits<std::int64_t>::min()).i == 9223372036854775808ULL,
        "The most negative integer must keep its magnitude");
}

void TestCldrPluralRules() {
    for (const std::string_view language : { "en", "de", "nl", "sv", "fi" }) {
        RequirePlurals(language, { { "1", "one" }, { "0", "other" }, { "2", "other" }, { "1.0", "other" }, { "1.5", "other" }, { "21", "other" } });
    }
    RequirePlurals("fr", { { "0", "one" }, { "1", "one" }, { "1.5", "one" }, { "2", "other" }, { "1000000", "many" },
        { "2000000", "many" }, { "1000001", "other" }, { "1c6", "many" }, { "1.5c6", "many" }, { "1c3", "other" } });
    RequirePlurals("es", { { "1", "one" }, { "1.0", "one" }, { "0", "other" }, { "2", "other" }, { "1000000", "many" }, { "1c6", "many" } });
    RequirePlurals("it", { { "1", "one" }, { "1.0", "other" }, { "0", "other" }, { "5", "other" }, { "1000000", "many" } });
    RequirePlurals("pt", { { "0", "one" }, { "1", "one" }, { "1.5", "one" }, { "2", "other" }, { "1000000", "many" } });
    RequirePlurals("pt-PT", { { "0", "other" }, { "1", "one" }, { "1.5", "other" }, { "1000000", "many" } });
    RequirePlurals("pl", { { "1", "one" }, { "2", "few" }, { "4", "few" }, { "5", "many" }, { "12", "many" }, { "14", "many" },
        { "22", "few" }, { "0", "many" }, { "101", "many" }, { "112", "many" }, { "1.5", "other" }, { "1.0", "other" } });
    for (const std::string_view language : { "cs", "sk" }) {
        RequirePlurals(language, { { "1", "one" }, { "2", "few" }, { "4", "few" }, { "5", "other" }, { "0", "other" }, { "1.5", "many" }, { "22", "other" } });
    }
    for (const std::string_view language : { "ru", "uk" }) {
        RequirePlurals(language, { { "1", "one" }, { "21", "one" }, { "11", "many" }, { "2", "few" }, { "22", "few" }, { "12", "many" },
            { "5", "many" }, { "0", "many" }, { "111", "many" }, { "101", "one" }, { "1.5", "other" } });
    }
    RequirePlurals("ar", { { "0", "zero" }, { "1", "one" }, { "2", "two" }, { "3", "few" }, { "10", "few" }, { "103", "few" },
        { "11", "many" }, { "99", "many" }, { "111", "many" }, { "100", "other" }, { "102", "other" }, { "0.5", "other" }, { "1.0", "one" } });
    RequirePlurals("he", { { "1", "one" }, { "2", "two" }, { "3", "other" }, { "10", "other" }, { "20", "other" }, { "0.5", "one" },
        { "1.0", "other" }, { "2.0", "other" } });
    for (const std::string_view language : { "ja", "ko", "zh", "zh-Hant-TW" }) {
        RequirePlurals(language, { { "0", "other" }, { "1", "other" }, { "2", "other" }, { "1.5", "other" } });
    }
    for (const std::string_view language : { "tr", "hu" }) {
        RequirePlurals(language, { { "1", "one" }, { "1.0", "one" }, { "0", "other" }, { "2", "other" }, { "1.5", "other" } });
    }
    RequirePlurals("ro", { { "1", "one" }, { "0", "few" }, { "2", "few" }, { "19", "few" }, { "20", "other" }, { "101", "few" },
        { "119", "few" }, { "120", "other" }, { "1.5", "few" } });
    RequirePlurals("lt", { { "1", "one" }, { "21", "one" }, { "1.0", "one" }, { "11", "other" }, { "2", "few" }, { "9", "few" },
        { "10", "other" }, { "12", "other" }, { "1.5", "many" } });
    RequirePlurals("lv", { { "0", "zero" }, { "10", "zero" }, { "11", "zero" }, { "19", "zero" }, { "20", "zero" }, { "1", "one" },
        { "21", "one" }, { "2", "other" }, { "22", "other" }, { "0.1", "one" }, { "0.11", "zero" }, { "0.2", "other" } });

    Require(kb::localization::SelectPluralCategory("PL_pl", std::int64_t{ 3 }) == kb::localization::PluralCategory::Few &&
            kb::localization::SelectPluralCategory("en-GB", std::int64_t{ -1 }) == kb::localization::PluralCategory::One,
        "Plural language matching must ignore case, region subtags and the sign of the number");
    for (const std::string_view language : { "en", "de", "fr", "es", "it", "pt", "pl", "cs", "sk", "ru", "uk", "ar", "he", "ja",
                                             "ko", "zh", "tr", "hu", "fi", "sv", "nl", "ro", "lt", "lv" }) {
        Require(kb::localization::HasPluralRules(language), ("Plural rules are missing for " + std::string{ language }).c_str());
    }
    Require(!kb::localization::HasPluralRules("xx") &&
            kb::localization::SelectPluralCategory("xx", std::int64_t{ 1 }) == kb::localization::PluralCategory::Other,
        "A language without rules must use the other category");
    const auto polish = kb::localization::PluralCategoriesOf("pl");
    Require(std::vector<kb::localization::PluralCategory>(polish.begin(), polish.end()) ==
                std::vector<kb::localization::PluralCategory>{ kb::localization::PluralCategory::One, kb::localization::PluralCategory::Few,
                    kb::localization::PluralCategory::Many, kb::localization::PluralCategory::Other },
        "Polish must offer the one, few, many and other categories");
    const auto arabic = kb::localization::PluralCategoriesOf("ar");
    Require(arabic.size() == 6U && kb::localization::PluralCategoriesOf("ja").size() == 1U,
        "Arabic must use all six categories and Japanese only other");
}

} // namespace

void RunLocalizationTests() {
    TestPluralOperands();
    TestCldrPluralRules();

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
            { "fr", {
                { "menu.play", { .text = "Jouer" } },
                { "hud.lives", { .plurals = { { "one", "{count} vie" }, { "many", "{count} de vies" }, { "other", "{count} vies" } } } },
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
    Require(scene.Localization().FormatPlural("hud.coins", 22) == "22 monety" &&
            scene.Localization().FormatPlural("hud.coins", 12) == "12 monet" &&
            scene.Localization().FormatPluralNumber("hud.coins", "1.5") == "1.5 monety",
        "Polish plural selection must follow the CLDR rules for teens and decimals");
    Require(scene.Localization().FormatPluralNumber("hud.coins", "not-a-number") == "hud.coins",
        "A malformed plural number must leave the key visible");
    for (const auto locale : {"pl-PL", "pl_PL", "pl-Latn-PL", "PL_pl", "pL_lAtN_pL"}) {
        Require(scene.Localization().SetLanguage(locale) && scene.Localization().Language() == locale &&
                scene.Localization().Translate("menu.play") == "Graj" &&
                scene.Localization().Translate("menu.quit") == "Quit" &&
                scene.Localization().FormatPlural("hud.coins", 22) == "22 monety",
            "Regional fallback must use the supplying language's text and plural rules");
    }
    Require(scene.Localization().SetLanguage("en"), "Scene localization rejected the fallback language");
    Require(scene.Localization().FormatPluralNumber("hud.coins", "1") == "1 coin" &&
            scene.Localization().FormatPluralNumber("hud.coins", "1.0") == "1.0 coins",
        "English decimal plurals must treat visible fraction digits as other");
    Require(scene.Localization().SetLanguage("fr"), "Scene localization rejected an authored language");
    Require(scene.Localization().FormatPlural("hud.coins", 0) == "0 coins",
        "A plural message taken from the fallback language must use the fallback language's rules");
    Require(scene.Localization().FormatPlural("hud.lives", 0) == "0 vie" && scene.Localization().FormatPlural("hud.lives", 2) == "2 vies",
        "French plural selection must treat zero as one");
    Require(scene.Localization().Translate("missing.key") == "missing.key", "Missing localization keys must remain visible to content authors");

    kb::script::ScriptRuntimeHost host{ scene };
    Require(host.Succeeded() && host.Functions().FindSignature("Localization.Translate") != nullptr &&
            host.Functions().FindSignature("Localization.FormatPlural") != nullptr,
        "Localization script functions were not registered through the production library module");
    const std::array arguments{
        kb::script::ScriptFunctionArgument{ .name = "key", .value = kb::script::ScriptValue{ std::string{ "hud.lives" } } },
        kb::script::ScriptFunctionArgument{ .name = "number", .value = kb::script::ScriptValue{ std::string{ "1c6" } } },
    };
    const kb::script::ScriptFunctionCallResult formatted =
        host.Functions().Call("Localization.FormatPluralNumber", arguments, kb::script::ScriptFunctionCallContext{ .scene = &scene });
    Require(formatted.Succeeded() && formatted.Output("text").has_value() && formatted.Output("text")->AsString() == "1c6 de vies",
        "Localization.FormatPluralNumber must select the CLDR category of a compact decimal");
    auto aliased = catalog;
    aliased.fallbackLanguage = "EN";
    const auto aliasPath = root / "Aliases.kbloc";
    Require(kb::localization::LocalizationCatalogIO::Save(aliasPath, aliased) &&
        kb::localization::LocalizationCatalogIO::Load(aliasPath).has_value(), "Fallback locale spelling must match case-insensitively");
    aliased.languages["EN"] = aliased.languages.at("en");
    Require(!kb::localization::LocalizationCatalogIO::Save(aliasPath, aliased), "Ambiguous locale aliases must not be saved");
    const auto loadText = [](std::string_view text) {
        return kb::localization::LocalizationCatalogIO::Load(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    };
    for (const std::string_view malformed : {
             "schema 1 fallback en fallback pl language en text key value",
             "schema 1 fallback en language en text key value language EN text key other",
             "schema 1 fallback en-US language en-US text key value language EN_us text key other",
             "schema 1 fallback en--US language en--US text key value",
             "schema 1 fallback -en language -en text key value",
             "schema 1 fallback en language en text key \"\xFF\"",
             "schema 1 fallback en language en plural key other \"\xED\xA0\x80\""}) {
        Require(!loadText(malformed), "Malformed or ambiguous catalog must fail import");
    }
    const std::string nulMessage = std::string{"schema 1 fallback en language en text key \"a"} + '\0' + "b\"";
    Require(!loadText(nulMessage), "Catalog messages cannot hide text after embedded nulls");
    Require(kb::localization::LocalizationCatalogIO::Load(aliasPath)->languages.size() == catalog.languages.size(),
        "Rejected catalog save must preserve the previous valid file");
    std::filesystem::create_directories(root / "Assets" / "Scripts");
    for (const auto name : {"Base", "English", "Polish"}) {
        std::ofstream file{root / "Assets" / "Scripts" / (std::string{name} + ".lua")};
        file << "function Ready(self, dt) end\n";
    }
    static_cast<void>(scene.Assets().Discover());
    auto& registry = scene.Assets().Manager().Registry();
    const auto source = registry.FindByPath("/Game/Scripts/Base.lua")->id;
    const auto english = registry.FindByPath("/Game/Scripts/English.lua")->id;
    const auto polish = registry.FindByPath("/Game/Scripts/Polish.lua")->id;
    auto resources = catalog;
    resources.resources = {{"en", {{source.value, english.value}}}, {"pl", {{source.value, polish.value}}}};
    const auto resourcesPath = root / "Assets" / "Localization" / "Resources.kbloc";
    Require(kb::localization::LocalizationCatalogIO::Save(resourcesPath, resources), "Resource mappings must save");
    static_cast<void>(scene.Assets().Discover());
    const auto resourceId = registry.FindByPath("/Game/Localization/Resources.kbloc")->id;
    Require(scene.Localization().SetCatalog(resourceId.value) && scene.Localization().ResolveResource(source.value) == english.value,
        "Resource fallback must select the catalog fallback language");
    Require(scene.Localization().SetLanguage("PL_pl") && scene.Localization().ResolveResource(source.value) == polish.value &&
        scene.Localization().ResolveResource(0U) == 0U && scene.Localization().ResolveResource(123U) == 123U &&
        scene.Localization().ResolveResource(polish.value) == polish.value,
        "Resource remapping must use locale parents, preserve absent mappings and apply only once");
    Require(scene.Localization().SetLanguage("fr") && scene.Localization().ResolveResource(source.value) == english.value,
        "A missing locale mapping must use the fallback resource");
    const std::array resourceArguments{kb::script::ScriptFunctionArgument{
        "source", kb::script::ScriptValue{source.value, kb::script::ScriptValueType::Hash}}};
    const auto remapped = host.Functions().Call("Localization.ResolveResource", resourceArguments,
        kb::script::ScriptFunctionCallContext{.scene = &scene});
    Require(remapped.Succeeded() && remapped.Output("asset")->AsUInt64() == english.value,
        "Script resource lookup must preserve the full-width asset identifier");
    const auto* resourceMetadata = registry.Find(resourceId);
    Require(resourceMetadata->dependencies.size() == 3U, "All source and translated resources must enter dependency discovery");
    auto invalid = resources;
    invalid.resources["pl"][source.value] = resourceId.value;
    const auto invalidPath = root / "InvalidResources.kbloc";
    Require(kb::localization::LocalizationCatalogIO::Save(invalidPath, invalid), "Invalid dependency fixture must serialize");
    kb::localization::LocalizationCatalogAssetLoader loader;
    auto invalidMetadata = *resourceMetadata;
    invalidMetadata.physicalPath = invalidPath;
    Require(loader.ValidateDependencies(invalidMetadata, registry).has_value(), "Mismatched resource types must fail validation");
    invalid.resources["pl"][source.value] = 999999U;
    Require(kb::localization::LocalizationCatalogIO::Save(invalidPath, invalid) &&
        loader.ValidateDependencies(invalidMetadata, registry).has_value(), "Missing localized assets must fail validation");
    kb::assets::AssetRegistry incompatibleImports;
    for (const auto id : {source, english, polish}) {
        auto entry = *registry.Find(id);
        entry.type = "ImportedAsset";
        entry.importCategory = id == polish ? "Font" : "Texture";
        Require(incompatibleImports.Upsert(entry), "Imported resource fixture must register");
    }
    invalidMetadata.physicalPath = resourcesPath;
    Require(loader.ValidateDependencies(invalidMetadata, incompatibleImports).has_value(),
        "Imported assets of different categories must not replace each other");
    for (const auto malformed : {
        "schema 1 fallback en language en resource 1 2",
        "schema 2 fallback en language en resource 0 2",
        "schema 2 fallback en language en resource 1 2 resource 1 3"})
        Require(!loadText(malformed), "Malformed or duplicate resource mappings must be rejected");
    Require(loadText("schema 2 fallback en language en resource 1 2").has_value(),
        "Resource-only locales must be supported");
    const std::array importSources{resourcesPath};
    const auto imported = kb::assets::AssetImportService::ImportFiles(scene.Assets().Manager(), importSources, "/Game/Imported");
    Require(imported.items.size() == 1U && imported.items.front().Succeeded() &&
        imported.items.front().assetPhysicalPath.extension() == ".kbloc",
        "Catalog import must retain the native format and runtime loader");
    const auto reused = kb::assets::AssetImportService::ImportFiles(scene.Assets().Manager(), importSources, "/Game/Imported");
    Require(reused.items.front().status == kb::assets::AssetImportItemStatus::Reused &&
        reused.items.front().id == imported.items.front().id,
        "Unchanged catalog imports must reuse their stable asset ID");
    const auto brokenPath = root / "Broken.kbloc";
    { std::ofstream broken{brokenPath}; broken << "schema 2 invalid"; }
    const std::array brokenSources{brokenPath};
    const auto rejected = kb::assets::AssetImportService::ImportFiles(scene.Assets().Manager(), brokenSources, "/Game/Imported");
    Require(!rejected.items.front().Succeeded() && !std::filesystem::exists(root / "Assets" / "Imported" / "Broken.kbloc"),
        "Malformed catalog import must not publish an asset");
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
