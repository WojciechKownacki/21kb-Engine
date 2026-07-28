#include "engine/scene/UIAssetIO.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <filesystem>
#include <cmath>
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

const char* ControlKindName(UIControlKind kind) {
    switch (kind) {
    case UIControlKind::Container: return "Container";
    case UIControlKind::Text: return "Text";
    case UIControlKind::Image: return "Image";
    case UIControlKind::Button: return "Button";
    case UIControlKind::Toggle: return "Toggle";
    case UIControlKind::Slider: return "Slider";
    case UIControlKind::List: return "List";
    case UIControlKind::InputField: return "InputField";
    case UIControlKind::ScrollView: return "ScrollView";
    case UIControlKind::ModalDialog: return "ModalDialog";
    }
    return "";
}

bool ParseControlKind(std::string_view text, UIControlKind& kind) {
    if (text == "Container") kind = UIControlKind::Container;
    else if (text == "Text") kind = UIControlKind::Text;
    else if (text == "Image") kind = UIControlKind::Image;
    else if (text == "Button") kind = UIControlKind::Button;
    else if (text == "Toggle") kind = UIControlKind::Toggle;
    else if (text == "Slider") kind = UIControlKind::Slider;
    else if (text == "List") kind = UIControlKind::List;
    else if (text == "InputField") kind = UIControlKind::InputField;
    else if (text == "ScrollView") kind = UIControlKind::ScrollView;
    else if (text == "ModalDialog") kind = UIControlKind::ModalDialog;
    else return false;
    return true;
}

bool ValidateControl(const UIControlState& control) {
    if (!std::isfinite(control.sliderValue) || !std::isfinite(control.sliderMinimum) ||
        !std::isfinite(control.sliderMaximum) || !std::isfinite(control.scrollOffset) ||
        control.sliderMinimum > control.sliderMaximum ||
        control.sliderValue < control.sliderMinimum || control.sliderValue > control.sliderMaximum ||
        control.scrollOffset < 0.0F) return false;
    if (control.listItems.size() > kMaxUIListItems) return false;
    for (const std::string& item : control.listItems) if (item.empty()) return false;
    return ControlKindName(control.kind)[0] != '\0';
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
        if (element.id == 0U || element.name.empty() || !ValidateControl(element.control) || !ids.insert(element.id).second) return false;
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
        } else if (command == "control") {
            UIElementId elementId = 0U;
            std::string kind;
            UIControlState control{};
            std::string toggle;
            std::string modal;
            std::size_t itemCount = 0U;
            if (!(input >> elementId >> kind >> std::quoted(control.text) >> control.imageAssetId >> toggle >>
                  control.sliderValue >> control.sliderMinimum >> control.sliderMaximum >> control.scrollOffset >>
                  modal >> itemCount) || !ParseControlKind(kind, control.kind) ||
                !ParseBool(toggle, control.toggleValue) || !ParseBool(modal, control.modalOpen)) return std::nullopt;
            if (itemCount > kMaxUIListItems) return std::nullopt;
            control.listItems.reserve(itemCount);
            for (std::size_t index = 0U; index < itemCount; ++index) {
                std::string item;
                if (!(input >> std::quoted(item))) return std::nullopt;
                control.listItems.push_back(std::move(item));
            }
            if (!EndOfRecord(input) || !ValidateControl(control)) return std::nullopt;
            const auto found = std::find_if(document.elements.begin(), document.elements.end(), [elementId](const UIDocumentElement& element) {
                return element.id == elementId;
            });
            if (found == document.elements.end() || found->control.kind != UIControlKind::Container) return std::nullopt;
            found->control = std::move(control);
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
        const UIControlState& control = element.control;
        output << "control " << element.id << ' ' << ControlKindName(control.kind) << ' ' << std::quoted(control.text) << ' '
               << control.imageAssetId << ' ' << (control.toggleValue ? "true" : "false") << ' '
               << control.sliderValue << ' ' << control.sliderMinimum << ' ' << control.sliderMaximum << ' '
               << control.scrollOffset << ' ' << (control.modalOpen ? "true" : "false") << ' ' << control.listItems.size();
        for (const std::string& item : control.listItems) output << ' ' << std::quoted(item);
        output << '\n';
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
