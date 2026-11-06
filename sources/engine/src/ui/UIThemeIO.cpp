#include "engine/ui/UIThemeIO.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <type_traits>

namespace kb::scene {
namespace {
constexpr std::size_t kMaxBytes = 1024U * 1024U;

bool ReadProperties(std::istream& input, std::vector<UIThemeProperty>& result) {
    std::size_t count = 0U;
    if (!(input >> count) || count > 1024U) return false;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::string id, property;
        unsigned type = 0U;
        if (!(input >> std::quoted(id) >> std::quoted(property) >> type) || id.size() > 128U || property.size() > 128U)
            return false;
        const auto* component = FindUIComponentDescriptor(id);
        if (!component || component->stableId != id) return false;
        UIComponentPropertyValue value;
        switch (type) {
        case 0: { int v = 0; if (!(input >> v) || (v != 0 && v != 1)) return false; value = v != 0; break; }
        case 1: { std::int32_t v = 0; if (!(input >> v)) return false; value = v; break; }
        case 2: { std::uint32_t v = 0; if (!(input >> v)) return false; value = v; break; }
        case 3: { float v = 0; if (!(input >> v)) return false; value = v; break; }
        case 4: { std::string v; if (!(input >> std::quoted(v)) || v.size() > 1024U) return false; value = std::move(v); break; }
        case 5: { std::uint64_t v = 0; if (!(input >> v)) return false; value = v; break; }
        default: return false;
        }
        result.push_back({{component->type, std::move(property)}, std::move(value)});
    }
    return true;
}

void WriteProperties(std::ostream& output, std::span<const UIThemeProperty> properties) {
    output << properties.size() << '\n';
    for (const auto& property : properties) {
        output << std::quoted(std::string{FindUIComponentDescriptor(property.key.component)->stableId}) << ' '
               << std::quoted(property.key.property) << ' ' << property.value.index() << ' ';
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::string>) output << std::quoted(value);
            else output << value;
        }, property.value);
        output << '\n';
    }
}
} // namespace

std::optional<UITheme> UIThemeIO::Load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return std::nullopt;
    const auto size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > kMaxBytes) return std::nullopt;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return std::nullopt;
    return Load(bytes);
}

std::optional<UITheme> UIThemeIO::Load(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxBytes || std::ranges::find(bytes, std::uint8_t{0}) != bytes.end()) return std::nullopt;
    std::istringstream input{std::string(bytes.begin(), bytes.end())};
    input.imbue(std::locale::classic());
    std::string magic;
    unsigned version = 0U;
    if (!(input >> magic >> version) || magic != "KB_UI_THEME" || version != 1U) return std::nullopt;
    UITheme theme;
    if (!ReadProperties(input, theme.properties)) return std::nullopt;
    std::size_t count = 0U;
    if (!(input >> count) || count > 128U) return std::nullopt;
    for (std::size_t i = 0; i < count; ++i) {
        UIThemeVariant variant;
        if (!(input >> std::quoted(variant.name)) || !ReadProperties(input, variant.properties)) return std::nullopt;
        theme.variants.push_back(std::move(variant));
    }
    input >> std::ws;
    if (!input.eof() || !ValidateUITheme(theme)) return std::nullopt;
    return theme;
}

bool UIThemeIO::Save(const std::filesystem::path& path, const UITheme& theme) {
    if (!ValidateUITheme(theme)) return false;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<float>::max_digits10) << "KB_UI_THEME 1\n";
    WriteProperties(output, theme.properties);
    output << theme.variants.size() << '\n';
    for (const auto& variant : theme.variants) {
        output << std::quoted(variant.name) << '\n';
        WriteProperties(output, variant.properties);
    }
    const std::string bytes = output.str();
    if (!output || bytes.size() > kMaxBytes) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return false;
    file.close();
    return static_cast<bool>(file);
}

} // namespace kb::scene
