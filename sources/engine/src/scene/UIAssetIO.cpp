#include "engine/scene/UIAssetIO.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <filesystem>
#include <iomanip>
#include <locale>
#include <span>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace kb::scene {
namespace {

std::optional<std::string> ReadText(const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes = SceneAssetBinaryIO::ReadAllBytes(path);
    if (bytes.empty()) return std::nullopt;
    return std::string{ reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

bool WriteText(const std::filesystem::path& path, const std::string& text) {
    return SceneAssetBinaryIO::WriteBytesAtomically(path,
        std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

bool EndOfRecord(std::istringstream& input) {
    input >> std::ws;
    return input.eof() || input.peek() == '#';
}

bool ParseBool(std::string_view text, bool& value) {
    if (text == "1" || text == "true") { value = true; return true; }
    if (text == "0" || text == "false") { value = false; return true; }
    return false;
}

const char* ValueTypeName(UIDataValueType value) {
    switch (value) {
    case UIDataValueType::Boolean: return "Boolean";
    case UIDataValueType::Number: return "Number";
    case UIDataValueType::String: return "String";
    }
    return "";
}

bool ParseValueType(std::string_view text, UIDataValueType& value) {
    if (text == "Boolean") value = UIDataValueType::Boolean;
    else if (text == "Number") value = UIDataValueType::Number;
    else if (text == "String") value = UIDataValueType::String;
    else return false;
    return true;
}

const char* DirectionName(UIBindingDirection direction) {
    switch (direction) {
    case UIBindingDirection::OneWay: return "OneWay";
    case UIBindingDirection::TwoWay: return "TwoWay";
    }
    return "";
}

bool ParseDirection(std::string_view text, UIBindingDirection& direction) {
    if (text == "OneWay") direction = UIBindingDirection::OneWay;
    else if (text == "TwoWay") direction = UIBindingDirection::TwoWay;
    else return false;
    return true;
}

bool ValidateDocument(const UIDocument& document) {
    if (document.schemaVersion != UIDocument::kSchemaVersion || document.elements.empty()) return false;
    std::unordered_map<UIElementId, UIElementId> parents;
    std::unordered_set<UIElementId> ids;
    std::size_t roots = 0U;
    for (const UIDocumentElement& element : document.elements) {
        if (element.id == 0U || element.name.empty() || !ids.insert(element.id).second) return false;
        parents.emplace(element.id, element.parentId);
        roots += element.parentId == 0U ? 1U : 0U;
    }
    if (roots != 1U) return false;
    for (const UIDocumentElement& element : document.elements) {
        if (element.parentId != 0U && !ids.contains(element.parentId)) return false;
        std::unordered_set<UIElementId> path;
        UIElementId current = element.id;
        while (current != 0U) {
            if (!path.insert(current).second) return false;
            current = parents.at(current);
        }
    }
    std::unordered_set<std::string> bindingTargets;
    for (const UIBindingDeclaration& binding : document.bindings) {
        if (!ids.contains(binding.elementId) || binding.property.empty() || binding.sourcePath.empty() ||
            !bindingTargets.insert(std::to_string(binding.elementId) + '\x1f' + binding.property).second) return false;
    }
    return true;
}

bool ValidateStyle(const UIStyleAsset& style) {
    if (style.name.empty()) return false;
    std::unordered_set<std::string> classes;
    for (const std::string& value : style.classes) {
        if (value.empty() || !classes.insert(value).second) return false;
    }
    return true;
}

} // namespace

std::optional<UIDocument> UIAssetIO::LoadDocument(const std::filesystem::path& path) {
    const auto text = ReadText(path);
    if (!text) return std::nullopt;
    UIDocument document{};
    std::istringstream file{ *text };
    file.imbue(std::locale::classic());
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream input{ line };
        input.imbue(std::locale::classic());
        std::string command;
        if (!(input >> command) || command.starts_with('#')) continue;
        if (command == "schema") {
            if (!(input >> document.schemaVersion) || !EndOfRecord(input)) return std::nullopt;
        } else if (command == "style") {
            if (!(input >> document.styleAssetId) || !EndOfRecord(input)) return std::nullopt;
        } else if (command == "element") {
            UIDocumentElement element{};
            std::string visible;
            if (!(input >> element.id >> element.parentId >> std::quoted(element.name) >>
                  std::quoted(element.styleClass) >> visible) || !ParseBool(visible, element.visible) || !EndOfRecord(input)) return std::nullopt;
            document.elements.push_back(std::move(element));
        } else if (command == "binding") {
            UIBindingDeclaration binding{};
            std::string valueType;
            std::string direction;
            if (!(input >> binding.elementId >> std::quoted(binding.property) >> std::quoted(binding.sourcePath) >>
                  valueType >> direction) || !ParseValueType(valueType, binding.valueType) ||
                  !ParseDirection(direction, binding.direction) || !EndOfRecord(input)) return std::nullopt;
            document.bindings.push_back(std::move(binding));
        } else {
            return std::nullopt;
        }
    }
    return ValidateDocument(document) ? std::optional<UIDocument>{ std::move(document) } : std::nullopt;
}

std::optional<UIStyleAsset> UIAssetIO::LoadStyle(const std::filesystem::path& path) {
    const auto text = ReadText(path);
    if (!text) return std::nullopt;
    UIStyleAsset style{};
    std::istringstream file{ *text };
    file.imbue(std::locale::classic());
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream input{ line };
        input.imbue(std::locale::classic());
        std::string command;
        if (!(input >> command) || command.starts_with('#')) continue;
        if (command == "name") {
            if (!(input >> std::quoted(style.name)) || !EndOfRecord(input)) return std::nullopt;
        } else if (command == "class") {
            std::string value;
            if (!(input >> std::quoted(value)) || !EndOfRecord(input)) return std::nullopt;
            style.classes.push_back(std::move(value));
        } else {
            return std::nullopt;
        }
    }
    return ValidateStyle(style) ? std::optional<UIStyleAsset>{ std::move(style) } : std::nullopt;
}

bool UIAssetIO::SaveDocument(const std::filesystem::path& path, const UIDocument& document) {
    if (!ValidateDocument(document)) return false;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "schema " << document.schemaVersion << '\n';
    output << "style " << document.styleAssetId << '\n';
    for (const UIDocumentElement& element : document.elements) {
        output << "element " << element.id << ' ' << element.parentId << ' ' << std::quoted(element.name) << ' '
               << std::quoted(element.styleClass) << ' ' << (element.visible ? "true" : "false") << '\n';
    }
    for (const UIBindingDeclaration& binding : document.bindings) {
        output << "binding " << binding.elementId << ' ' << std::quoted(binding.property) << ' '
               << std::quoted(binding.sourcePath) << ' ' << ValueTypeName(binding.valueType) << ' '
               << DirectionName(binding.direction) << '\n';
    }
    return WriteText(path, output.str());
}

bool UIAssetIO::SaveStyle(const std::filesystem::path& path, const UIStyleAsset& style) {
    if (!ValidateStyle(style)) return false;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "name " << std::quoted(style.name) << '\n';
    for (const std::string& value : style.classes) output << "class " << std::quoted(value) << '\n';
    return WriteText(path, output.str());
}

} // namespace kb::scene
