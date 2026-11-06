#include "TestSupport.hpp"

#include "engine/ui/UITheme.hpp"
#include "engine/ui/UIThemeIO.hpp"
#include "engine/ui/UIThemeAssetLoader.hpp"
#include "engine/assets/AssetRegistry.hpp"

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace kb::tests {

void RunUIThemeTests() {
    using namespace kb::scene;
    const UIThemePropertyKey color{UIComponentType::Text, "color.r"};
    const UIThemePropertyKey font{UIComponentType::Text, "fontAssetId"};
    UITheme theme{{{color, 0.25F}, {font, std::uint64_t{42U}}},
                  {{"accent", {{color, 0.75F}}}}};
    UIComponentSet first, second;
    first.text.emplace();
    second.text.emplace();
    first.text->color.r = 0.1F;
    second.text->color.r = 0.2F;
    UIComponentSet resolvedA, resolvedB;
    Require(ResolveUITheme(theme, "accent", {}, first, resolvedA) == UIThemeResolveResult::Succeeded &&
            ResolveUITheme(theme, "accent", std::span{&color, 1U}, second, resolvedB) == UIThemeResolveResult::Succeeded &&
            NearlyEqual(resolvedA.text->color.r, 0.75F) && NearlyEqual(resolvedB.text->color.r, 0.2F) &&
            resolvedA.text->fontAssetId == 42U && resolvedB.text->fontAssetId == 42U,
            "Theme variant and local override must resolve independently");
    theme.variants[0].properties[0].value = 0.5F;
    Require(ResolveUITheme(theme, "accent", {}, first, resolvedA) == UIThemeResolveResult::Succeeded &&
            NearlyEqual(resolvedA.text->color.r, 0.5F), "Shared theme edits must affect subsequent resolutions");
    const auto before = resolvedA.text->color.r;
    Require(ResolveUITheme(theme, "missing", {}, first, resolvedA) == UIThemeResolveResult::UnknownVariant &&
            resolvedA.text->color.r == before, "Failed resolution must not alter output");
    Require(WriteUIComponentProperty(first, UIComponentType::Text, "fontRenderMode", std::int32_t{256}) ==
                UIComponentPropertyWriteResult::TypeMismatch && first.text->fontRenderMode == UITextFontRenderMode::Bitmap,
            "Out-of-range enum values must not wrap into valid settings");
    UITheme invalid = theme;
    invalid.properties.push_back(invalid.properties.front());
    Require(!ValidateUITheme(invalid), "Duplicate theme keys must be rejected");
    invalid = theme;
    invalid.properties[0].value = std::numeric_limits<float>::infinity();
    Require(!ValidateUITheme(invalid), "Invalid theme values must be rejected");
    invalid = theme;
    invalid.properties[0].key.property = "content";
    Require(!ValidateUITheme(invalid), "Theme cannot replace authored text content");

    const auto path = std::filesystem::temp_directory_path() / "kb_ui_theme_test.kbtheme";
    Require(UIThemeIO::Save(path, theme), "Theme save failed");
    const auto loaded = UIThemeIO::Load(path);
    kb::assets::AssetRegistry registry;
    kb::assets::AssetMetadata fontMeta;
    fontMeta.id = kb::assets::AssetId{42U};
    fontMeta.type = "ImportedAsset";
    fontMeta.importCategory = "Font";
    fontMeta.virtualPath = "Fonts/ThemeFont";
    Require(registry.Upsert(fontMeta), "Theme test font registration failed");
    kb::assets::AssetMetadata themeMeta;
    themeMeta.physicalPath = path;
    UIThemeAssetLoader loader;
    const auto dependencies = loader.DiscoverDependencies(themeMeta, registry);
    Require(dependencies.size() == 1U && dependencies[0].value == 42U &&
            !loader.ValidateDependencies(themeMeta, registry), "Theme font dependency must be retained and validated");
    Require(registry.Remove(kb::assets::AssetId{42U}) && loader.ValidateDependencies(themeMeta, registry).has_value(),
            "Missing theme asset dependencies must fail validation");
    std::error_code error;
    std::filesystem::remove(path, error);
    Require(loaded && ResolveUITheme(*loaded, "accent", {}, first, resolvedA) == UIThemeResolveResult::Succeeded &&
            NearlyEqual(resolvedA.text->color.r, 0.5F) && resolvedA.text->fontAssetId == 42U,
            "Saved theme must retain variant values and asset references");
    const std::string malformed = "KB_UI_THEME 1\n1\n\"kb21.ui.text\" \"content\" 4 \"bad\"\n0\n";
    const auto bytes = std::span{reinterpret_cast<const std::uint8_t*>(malformed.data()), malformed.size()};
    Require(!UIThemeIO::Load(bytes), "Theme loader must reject non-style fields");
}

} // namespace kb::tests
