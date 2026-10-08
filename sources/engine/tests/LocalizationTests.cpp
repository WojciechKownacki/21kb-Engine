#include "TestSupport.hpp"

#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/localization/LocalizationCatalogIO.hpp"
#include "engine/localization/PluralRules.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneLocalization.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <array>
#include <filesystem>
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
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
