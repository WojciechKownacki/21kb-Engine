#include "CliAssetTarget.hpp"
#include "CliCommands.hpp"
#include "CliWorkshop.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"
#include "engine/ui/UITheme.hpp"
#include "engine/ui/UIThemeIO.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// ui-theme: the look of the user interface as a shared UI Theme asset (.kbtheme), the same one the editor's Inspector
// edits. A theme holds a base style (panels, texts) and a "Button" variant; `apply` gives it to every control of a scene.
// The tokens are the few a person asks for: colours, text size, rounded corners.

namespace kb::cli {
namespace {

using kb::scene::UIComponentType;
using kb::scene::UITheme;
using kb::scene::UIThemeProperty;
using kb::scene::UIThemeVariant;

constexpr std::string_view kUsage =
    "usage: kb_cli ui-theme <new|set|show|apply> --project <dir> ...\n"
    "  new   --name <Name> [--preset dark|light] [tokens] [--folder Assets/UI] [--replace]\n"
    "  set   --theme <path> [tokens]\n"
    "  show  --theme <path>\n"
    "  apply --theme <path> [--scene <path>] [--entity <Object>]\n"
    "  tokens: --background #RRGGBB  --text #RRGGBB  --border #RRGGBB  --border-width <px>  --corner-radius <px>  --font-size <px>\n"
    "          --button #RRGGBB  --button-text #RRGGBB  (the buttons' own colours: the variant Button)\n";

constexpr std::string_view kButtonVariant = "Button";

// The value is written as the property's own type, so a theme the editor reads has no surprises.
[[nodiscard]] bool SetProperty(std::vector<UIThemeProperty>& properties, UIComponentType component, const std::string& name, double value) {
    const auto* descriptor = kb::scene::FindUIComponentProperty(component, name);
    if (descriptor == nullptr) return false;
    kb::scene::UIComponentPropertyValue stored;
    switch (descriptor->type) {
    case kb::scene::UIComponentPropertyType::Float: stored = static_cast<float>(value); break;
    case kb::scene::UIComponentPropertyType::Int: stored = static_cast<std::int32_t>(std::lround(value)); break;
    case kb::scene::UIComponentPropertyType::UInt32: stored = static_cast<std::uint32_t>(std::lround(value)); break;
    default: return false;
    }
    const kb::scene::UIThemePropertyKey key{ component, name };
    const auto found = std::ranges::find(properties, key, &UIThemeProperty::key);
    if (found != properties.end()) found->value = stored;
    else properties.push_back({ key, stored });
    return true;
}

[[nodiscard]] bool SetColor(std::vector<UIThemeProperty>& properties, UIComponentType component, const std::string& name, const workshop::Rgb& color) {
    return SetProperty(properties, component, name + ".r", color.r) && SetProperty(properties, component, name + ".g", color.g) &&
        SetProperty(properties, component, name + ".b", color.b) && SetProperty(properties, component, name + ".a", 1.0);
}

[[nodiscard]] std::string Hex(float r, float g, float b) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string text = "#";
    for (const float channel : { r, g, b }) {
        const int value = static_cast<int>(std::lround(std::clamp(channel, 0.0F, 1.0F) * 255.0F));
        text += kDigits[value / 16];
        text += kDigits[value % 16];
    }
    return text;
}

[[nodiscard]] bool IsDark(const workshop::Rgb& color) { return 0.2126F * color.r + 0.7152F * color.g + 0.0722F * color.b < 0.55F; }

struct Colors {
    std::optional<workshop::Rgb> background;
    std::optional<workshop::Rgb> text;
    std::optional<workshop::Rgb> border;
    std::optional<workshop::Rgb> button;
    std::optional<workshop::Rgb> buttonText;
};

// Reads the tokens of the command line into a theme (base and the Button variant). False with an error printed.
[[nodiscard]] bool ApplyTokens(UITheme& theme, const ArgumentList& arguments, CommandIo io) {
    Colors colors;
    const std::vector<std::pair<std::string_view, std::optional<workshop::Rgb>*>> colorOptions{ { "--background", &colors.background }, { "--text", &colors.text },
        { "--border", &colors.border }, { "--button", &colors.button }, { "--button-text", &colors.buttonText } };
    for (const auto& [option, slot] : colorOptions) {
        const std::optional<std::string> text = arguments.Option(option);
        if (!text.has_value()) continue;
        workshop::Rgb color;
        if (!workshop::ParseHexColor(*text, color)) {
            io.err << "error: " << option << " takes a colour as #RRGGBB (for example #E8730C), got '" << *text << "'\n";
            return false;
        }
        *slot = color;
    }
    const auto number = [&](std::string_view option, double minimum, double maximum) -> std::optional<std::optional<double>> {
        const std::optional<std::string> text = arguments.Option(option);
        if (!text.has_value()) return std::optional<double>{};
        char* end = nullptr;
        const double value = std::strtod(text->c_str(), &end);
        if (text->empty() || end != text->c_str() + text->size() || !std::isfinite(value) || value < minimum || value > maximum) {
            io.err << "error: " << option << " takes a number from " << minimum << " to " << maximum << ", got '" << *text << "'\n";
            return std::nullopt;
        }
        return std::optional<double>{ value };
    };
    const auto width = number("--border-width", 0.0, 32.0);
    const auto radius = number("--corner-radius", 0.0, 256.0);
    const auto fontSize = number("--font-size", 4.0, 200.0);
    if (!width.has_value() || !radius.has_value() || !fontSize.has_value()) return false;

    std::vector<UIThemeProperty>& base = theme.properties;
    if (colors.background) static_cast<void>(SetColor(base, UIComponentType::Border, "backgroundColor", *colors.background));
    if (colors.border) static_cast<void>(SetColor(base, UIComponentType::Border, "borderColor", *colors.border));
    if (colors.text) static_cast<void>(SetColor(base, UIComponentType::Text, "color", *colors.text));
    if (*width) {
        for (const char* side : { "left", "top", "right", "bottom" }) static_cast<void>(SetProperty(base, UIComponentType::Border, std::string{ "borderWidth." } + side, **width));
    }
    if (*radius) {
        for (const char* corner : { "x", "y", "z", "w" }) static_cast<void>(SetProperty(base, UIComponentType::Border, std::string{ "cornerRadius." } + corner, **radius));
    }
    if (*fontSize) static_cast<void>(SetProperty(base, UIComponentType::Text, "fontSize", **fontSize));
    if (colors.button || colors.buttonText) {
        auto variant = std::ranges::find(theme.variants, kButtonVariant, &UIThemeVariant::name);
        if (variant == theme.variants.end()) variant = theme.variants.insert(theme.variants.end(), UIThemeVariant{ std::string{ kButtonVariant }, {} });
        if (colors.button) static_cast<void>(SetColor(variant->properties, UIComponentType::Border, "backgroundColor", *colors.button));
        // Text on a button is readable on its colour unless the person chose one.
        const std::optional<workshop::Rgb> labels = colors.buttonText ? colors.buttonText
            : (colors.button ? std::optional<workshop::Rgb>(IsDark(*colors.button) ? workshop::Rgb{ 0.96F, 0.96F, 0.96F } : workshop::Rgb{ 0.1F, 0.1F, 0.1F }) : std::nullopt);
        if (labels) static_cast<void>(SetColor(variant->properties, UIComponentType::Text, "color", *labels));
    }
    return true;
}

[[nodiscard]] std::string Value(const kb::scene::UIComponentPropertyValue& value) {
    if (const auto* number = std::get_if<float>(&value)) return FormatCliNumber(*number);
    if (const auto* integer = std::get_if<std::int32_t>(&value)) return std::to_string(*integer);
    if (const auto* unsignedInteger = std::get_if<std::uint32_t>(&value)) return std::to_string(*unsignedInteger);
    if (const auto* flag = std::get_if<bool>(&value)) return *flag ? "true" : "false";
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    return "asset";
}

[[nodiscard]] std::string ShowProperties(const std::vector<UIThemeProperty>& properties, const std::string& indent) {
    std::string text;
    for (std::size_t index = 0U; index < properties.size(); ++index) {
        const UIThemeProperty& property = properties[index];
        const std::string component = std::string{ kb::scene::FindUIComponentDescriptor(property.key.component)->displayName };
        // r, g, b, a of a colour are written one after another: shown as one colour.
        if (property.key.property.ends_with(".r") && index + 3U < properties.size() && properties[index + 3U].key.property.ends_with(".a")) {
            const auto channel = [&](std::size_t at) { return std::get<float>(properties[at].value); };
            text += indent + component + "." + property.key.property.substr(0U, property.key.property.size() - 2U) + " = " +
                Hex(channel(index), channel(index + 1U), channel(index + 2U)) + "\n";
            index += 3U;
            continue;
        }
        text += indent + component + "." + property.key.property + " = " + Value(property.value) + "\n";
    }
    return text;
}

[[nodiscard]] std::string ShowTheme(const std::string& path, const UITheme& theme) {
    std::string text = "theme " + path + "\n  base style (every control):\n" + ShowProperties(theme.properties, "    ");
    for (const UIThemeVariant& variant : theme.variants) text += "  variant " + variant.name + " (buttons):\n" + ShowProperties(variant.properties, "    ");
    return text;
}

struct OpenTheme {
    std::filesystem::path file;
    std::string virtualPath;
    UITheme theme;
};

[[nodiscard]] std::optional<OpenTheme> LoadTheme(const ArgumentList& arguments, kb::scene::Scene& scene, CommandIo io) {
    const std::optional<std::string> path = arguments.Option("--theme");
    const kb::assets::AssetMetadata* metadata = path.has_value()
        ? FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), kb::assets::Utf8Path(*path)) : nullptr;
    if (metadata == nullptr || metadata->type != kb::scene::kUIThemeAssetType) {
        io.err << "error: --theme <path> must name a UI theme of the project, like /Game/UI/Dark.kbtheme (make one with kb_cli ui-theme new)\n";
        return std::nullopt;
    }
    std::optional<UITheme> loaded = kb::scene::UIThemeIO::Load(metadata->physicalPath);
    if (!loaded.has_value()) {
        io.err << "error: " << metadata->virtualPath.generic_string() << " could not be read\n";
        return std::nullopt;
    }
    return OpenTheme{ metadata->physicalPath, metadata->virtualPath.generic_string(), std::move(*loaded) };
}

[[nodiscard]] bool Mount(const ArgumentList& arguments, kb::scene::Scene& scene, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: ui-theme requires --project <dir>\n" << kUsage;
        return false;
    }
    std::string error;
    if (!MountProjectAssets(scene, kb::assets::Utf8Path(*project), error)) {
        io.err << "error: " << error << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool SaveTheme(const OpenTheme& theme, CommandIo io) {
    if (!kb::scene::UIThemeIO::Save(theme.file, theme.theme)) {
        io.err << "error: " << theme.virtualPath << " could not be saved\n";
        return false;
    }
    return true;
}

[[nodiscard]] int New(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!Mount(arguments, scene, io)) return 1;
    const std::optional<AssetTarget> target = ResolveAssetTarget(arguments, "ui-theme new", "--project <dir> --name <Name>", "Assets/UI",
        std::string_view{ kb::scene::kUIThemeAssetExtension }, false, io);
    if (!target.has_value()) return 1;
    const std::string preset = arguments.Option("--preset").value_or("dark");
    if (preset != "dark" && preset != "light") {
        io.err << "error: --preset is dark or light, got '" << preset << "'\n";
        return 1;
    }
    // The preset's colours come first; the tokens of the command line then change them.
    OpenTheme theme{ target->file, target->virtualPath, {} };
    const bool dark = preset == "dark";
    static_cast<void>(SetColor(theme.theme.properties, UIComponentType::Border, "backgroundColor", dark ? workshop::Rgb{ 0.118F, 0.129F, 0.188F } : workshop::Rgb{ 0.957F, 0.957F, 0.965F }));
    static_cast<void>(SetColor(theme.theme.properties, UIComponentType::Border, "borderColor", dark ? workshop::Rgb{ 0.235F, 0.259F, 0.345F } : workshop::Rgb{ 0.769F, 0.784F, 0.831F }));
    static_cast<void>(SetColor(theme.theme.properties, UIComponentType::Text, "color", dark ? workshop::Rgb{ 0.94F, 0.94F, 0.94F } : workshop::Rgb{ 0.118F, 0.118F, 0.141F }));
    if (!ApplyTokens(theme.theme, arguments, io)) return 1;
    std::error_code error;
    std::filesystem::create_directories(target->file.parent_path(), error);
    if (!SaveTheme(theme, io)) return 1;
    io.out << "created " << theme.virtualPath << '\n' << ShowTheme(theme.virtualPath, theme.theme)
           << "next: kb_cli ui-theme apply --theme " << theme.virtualPath << " (gives it to every control of the scene)\n";
    return 0;
}

[[nodiscard]] int Set(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!Mount(arguments, scene, io)) return 1;
    std::optional<OpenTheme> theme = LoadTheme(arguments, scene, io);
    if (!theme.has_value()) return 1;
    if (!ApplyTokens(theme->theme, arguments, io)) return 1;
    if (!SaveTheme(*theme, io)) return 1;
    io.out << "updated " << theme->virtualPath << '\n' << ShowTheme(theme->virtualPath, theme->theme);
    return 0;
}

[[nodiscard]] int Show(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!Mount(arguments, scene, io)) return 1;
    const std::optional<OpenTheme> theme = LoadTheme(arguments, scene, io);
    if (!theme.has_value()) return 1;
    io.out << ShowTheme(theme->virtualPath, theme->theme);
    return 0;
}

// Whether the node is the object `only` (a name or a path) or lies below it.
[[nodiscard]] bool InScope(std::span<const kb::scene::ScenePrefabNodeDesc> nodes, std::size_t index, const std::string& only) {
    for (std::size_t depth = 0U; index < nodes.size() && depth < nodes.size(); ++depth) {
        if (nodes[index].name == only || NodePath(nodes, index) == only) return true;
        index = nodes[index].parentNode;
    }
    return false;
}

// Every control of the scene (or of one object and what is below it) takes the theme; buttons take its Button variant.
[[nodiscard]] int Apply(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!Mount(arguments, scene, io)) return 1;
    const std::optional<OpenTheme> theme = LoadTheme(arguments, scene, io);
    if (!theme.has_value()) return 1;
    std::string error;
    const std::optional<ProjectSceneDocument> loaded = LoadProjectScene(arguments, error);
    if (!loaded.has_value()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const bool hasButtonVariant = std::ranges::any_of(theme->theme.variants, [](const UIThemeVariant& variant) { return variant.name == kButtonVariant; });
    const std::string only = arguments.Option("--entity").value_or(std::string{});
    const std::span<const kb::scene::ScenePrefabNodeDesc> nodes = loaded->document.worldPrefab.Nodes();
    std::size_t themed = 0U;
    std::size_t buttons = 0U;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (!kb::scene::HasUIComponent(nodes[index].components.ui, UIComponentType::RectTransform)) continue;
        const std::string path = NodePath(nodes, index);
        if (!only.empty() && !InScope(nodes, index, only)) continue;
        const bool button = hasButtonVariant && kb::scene::HasUIComponent(nodes[index].components.ui, UIComponentType::Button);
        std::vector<std::string> words{ "set", "--project", *arguments.Option("--project"), "--scene", loaded->virtualPath, "--entity", path, "--type", "Rect Transform",
            "--set", "themeAssetId=" + theme->virtualPath, "--set", std::string{ "themeVariant=" } + (button ? std::string{ kButtonVariant } : std::string{}) };
        std::string message;
        if (!RunSiblingCommand(&RunComponentCommand, words, message)) {
            io.err << "error: " << path << ": " << message;
            return 1;
        }
        ++themed;
        buttons += button ? 1U : 0U;
    }
    if (themed == 0U) {
        io.err << "error: the scene has no user interface" << (only.empty() ? "" : " under '" + only + "'") << " (add a Canvas, Text or Button first)\n";
        return 1;
    }
    io.out << "applied " << theme->virtualPath << " to " << themed << " controls (" << buttons << " buttons with the Button variant) of " << loaded->virtualPath << '\n';
    return 0;
}

} // namespace

int RunUiThemeCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().size() == 1U ? arguments.Positionals().front() : std::string{};
    if (action == "new") return New(arguments, io);
    if (action == "set") return Set(arguments, io);
    if (action == "show") return Show(arguments, io);
    if (action == "apply") return Apply(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
