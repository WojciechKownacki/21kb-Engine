#include "engine/scene/UIAssetIO.hpp"

#include "engine/ui/UIAssetValidation.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <span>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace kb::scene {
namespace {

constexpr std::uint32_t kPreviousUIDocumentSchemaVersion = 1U;

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
    if (text == "1" || text == "true") {
        value = true;
        return true;
    }
    if (text == "0" || text == "false") {
        value = false;
        return true;
    }
    return false;
}

template <typename Enum>
bool ParseEnum(std::istringstream& input, Enum& value, std::uint32_t count) {
    std::uint32_t encoded = 0U;
    if (!(input >> encoded) || encoded >= count) return false;
    value = static_cast<Enum>(encoded);
    return true;
}

bool IsTextKind(UIControlKind kind) {
    return kind == UIControlKind::Text || kind == UIControlKind::Button ||
        kind == UIControlKind::InputField || kind == UIControlKind::Dropdown;
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

bool ValidBindingTarget(const UIDocumentElement& element, const UIBindingDeclaration& binding) {
    if (binding.property == "text") return IsTextKind(element.control.kind);
    if (binding.property == "toggle") {
        return binding.valueType == UIDataValueType::Boolean && element.control.kind == UIControlKind::Toggle;
    }
    if (binding.property == "value") {
        return binding.valueType == UIDataValueType::Number &&
            (element.control.kind == UIControlKind::Slider || element.control.kind == UIControlKind::ProgressBar ||
                element.control.kind == UIControlKind::Scrollbar);
    }
    if (binding.property == "scroll") {
        return binding.valueType == UIDataValueType::Number && element.control.kind == UIControlKind::ScrollView;
    }
    if (binding.property == "modal") {
        return binding.valueType == UIDataValueType::Boolean && element.control.kind == UIControlKind::ModalDialog;
    }
    return false;
}

bool ValidateDocument(const UIDocument& document) {
    if (document.schemaVersion != UIDocument::kSchemaVersion || document.elements.empty()) return false;
    std::unordered_map<UIElementId, UIElementId> parents;
    std::unordered_map<UIElementId, std::size_t> directChildCounts;
    std::unordered_map<UIElementId, std::vector<std::uint32_t>> siblingOrders;
    std::unordered_set<UIElementId> ids;
    const UIDocumentElement* root = nullptr;
    for (const UIDocumentElement& element : document.elements) {
        if (element.id == 0U || element.name.empty() || !ids.insert(element.id).second) return false;
        parents.emplace(element.id, element.parentId);
        ++directChildCounts[element.parentId];
        siblingOrders[element.parentId].push_back(element.siblingOrder);
        if (element.parentId == 0U) {
            if (root != nullptr) return false;
            root = &element;
        }
    }
    if (root == nullptr) return false;
    for (const UIDocumentElement& element : document.elements) {
        if (element.parentId != 0U && !ids.contains(element.parentId)) return false;
        if (!UIAssetValidation::ElementComposition(element, &element == root, directChildCounts[element.id])) return false;
        std::unordered_set<UIElementId> path;
        UIElementId current = element.id;
        while (current != 0U) {
            if (!path.insert(current).second) return false;
            current = parents.at(current);
        }
        if (element.interaction && element.interaction->navigationMode == UINavigationMode::Explicit) {
            for (const UIElementId target : { element.interaction->navigationUp, element.interaction->navigationDown,
                     element.interaction->navigationLeft, element.interaction->navigationRight }) {
                if (target != 0U && (target == element.id || !ids.contains(target))) return false;
            }
        }
    }
    for (auto& [parentId, orders] : siblingOrders) {
        static_cast<void>(parentId);
        std::sort(orders.begin(), orders.end());
        for (std::uint32_t index = 0U; index < orders.size(); ++index) {
            if (orders[index] != index) return false;
        }
    }
    std::unordered_set<std::string> bindingTargets;
    for (const UIBindingDeclaration& binding : document.bindings) {
        const auto element = std::find_if(document.elements.begin(), document.elements.end(), [&binding](const UIDocumentElement& value) {
            return value.id == binding.elementId;
        });
        if (element == document.elements.end() || binding.property.empty() || binding.sourcePath.empty() ||
            !ValidBindingTarget(*element, binding) ||
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

UIDocumentElement* FindElement(UIDocument& document, UIElementId id) {
    const auto found = std::find_if(document.elements.begin(), document.elements.end(), [id](const UIDocumentElement& element) {
        return element.id == id;
    });
    return found == document.elements.end() ? nullptr : &*found;
}

void AddMigratedComponents(UIDocumentElement& element, std::uint64_t legacyImageAssetId) {
    if (element.control.kind == UIControlKind::Image) {
        element.image = UIImage{ .imageAssetId = legacyImageAssetId };
    }
    if (IsTextKind(element.control.kind)) element.textStyle = UIText{};
    switch (element.control.kind) {
    case UIControlKind::Button:
    case UIControlKind::Toggle:
    case UIControlKind::Slider:
    case UIControlKind::List:
    case UIControlKind::InputField:
    case UIControlKind::ScrollView:
    case UIControlKind::ModalDialog:
        element.interaction = UIInteraction{};
        break;
    default:
        break;
    }
    if (element.control.kind == UIControlKind::ScrollView) {
        element.effects = UIEffects{ .clipChildren = true };
    }
}

bool MigrateV1(
    UIDocument& document,
    const std::unordered_map<UIElementId, std::uint64_t>& legacyImageAssets) {
    std::unordered_map<UIElementId, std::uint32_t> nextSiblingOrder;
    UIDocumentElement* root = nullptr;
    for (UIDocumentElement& element : document.elements) {
        element.siblingOrder = nextSiblingOrder[element.parentId]++;
        if (element.parentId == 0U) {
            if (root != nullptr) return false;
            root = &element;
        }
        const auto legacyImage = legacyImageAssets.find(element.id);
        AddMigratedComponents(element,
            legacyImage == legacyImageAssets.end() ? 0U : legacyImage->second);
    }
    if (root == nullptr || root->control.kind != UIControlKind::Container) return false;
    root->control.kind = UIControlKind::Canvas;
    root->canvas = UICanvas{};
    root->rect.anchorMax = { 1.0F, 1.0F };
    root->rect.offsetMax = {};
    document.schemaVersion = UIDocument::kSchemaVersion;
    return true;
}

struct DocumentParseState {
    std::optional<std::uint32_t> sourceSchema;
    bool styleSeen = false;
    std::unordered_set<UIElementId> controlElements;
    std::unordered_set<UIElementId> rectElements;
    std::unordered_set<UIElementId> canvasElements;
    std::unordered_set<UIElementId> layoutElements;
    std::unordered_set<UIElementId> paintElements;
    std::unordered_set<UIElementId> imageElements;
    std::unordered_set<UIElementId> textElements;
    std::unordered_set<UIElementId> interactionElements;
    std::unordered_set<UIElementId> effectElements;
    std::unordered_map<UIElementId, std::uint64_t> legacyImageAssets;
};

bool IsSupportedSchema(std::uint32_t version) {
    return version == kPreviousUIDocumentSchemaVersion || version == UIDocument::kSchemaVersion;
}

} // namespace

std::optional<UIDocument> UIAssetIO::LoadDocument(const std::filesystem::path& path) {
    const auto text = ReadText(path);
    if (!text) return std::nullopt;
    return LoadDocument(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(text->data()), text->size() });
}

std::optional<UIDocument> UIAssetIO::LoadDocument(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return std::nullopt;
    const std::string text{ reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    UIDocument document{};
    DocumentParseState state{};
    std::istringstream file{ text };
    file.imbue(std::locale::classic());
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream input{ line };
        input.imbue(std::locale::classic());
        std::string command;
        if (!(input >> command) || command.starts_with('#')) continue;
        if (command == "schema") {
            std::uint32_t version = 0U;
            if (state.sourceSchema || !(input >> version) || !EndOfRecord(input) || !IsSupportedSchema(version) ||
                state.styleSeen || !document.elements.empty() || !document.bindings.empty()) return std::nullopt;
            state.sourceSchema = version;
            document.schemaVersion = version;
            continue;
        }
        if (!state.sourceSchema) return std::nullopt;
        if (command == "style") {
            if (state.styleSeen || !(input >> document.styleAssetId) || !EndOfRecord(input)) return std::nullopt;
            state.styleSeen = true;
        } else if (command == "element") {
            UIDocumentElement element{};
            std::string visible;
            if (!(input >> element.id >> element.parentId)) return std::nullopt;
            if (*state.sourceSchema == UIDocument::kSchemaVersion && !(input >> element.siblingOrder)) return std::nullopt;
            if (!(input >> std::quoted(element.name) >> std::quoted(element.styleClass) >> visible) ||
                !ParseBool(visible, element.visible) || !EndOfRecord(input)) return std::nullopt;
            document.elements.push_back(std::move(element));
        } else if (command == "control") {
            UIElementId elementId = 0U;
            std::string kind;
            UIControlState control{};
            std::string toggle;
            std::string modal;
            std::size_t itemCount = 0U;
            std::uint64_t legacyImageAssetId = 0U;
            if (!(input >> elementId >> kind >> std::quoted(control.text))) return std::nullopt;
            if (*state.sourceSchema == kPreviousUIDocumentSchemaVersion &&
                !(input >> legacyImageAssetId)) return std::nullopt;
            if (!(input >> toggle >> control.sliderValue >> control.sliderMinimum >>
                  control.sliderMaximum >> control.scrollOffset >> modal) ||
                !TryParseUIControlKind(kind, control.kind) ||
                !ParseBool(toggle, control.toggleValue) || !ParseBool(modal, control.modalOpen)) return std::nullopt;
            if (*state.sourceSchema == UIDocument::kSchemaVersion) {
                if (!(input >> control.selectedIndex >> itemCount)) return std::nullopt;
            } else if (!(input >> itemCount)) {
                return std::nullopt;
            }
            if (!state.controlElements.insert(elementId).second) return std::nullopt;
            if (*state.sourceSchema == kPreviousUIDocumentSchemaVersion && control.kind > UIControlKind::ModalDialog) return std::nullopt;
            if (itemCount > kMaxUIListItems) return std::nullopt;
            control.listItems.reserve(itemCount);
            for (std::size_t index = 0U; index < itemCount; ++index) {
                std::string item;
                if (!(input >> std::quoted(item))) return std::nullopt;
                control.listItems.push_back(std::move(item));
            }
            UIDocumentElement* const element = FindElement(document, elementId);
            if (!EndOfRecord(input) || !UIAssetValidation::Control(control) || element == nullptr) return std::nullopt;
            element->control = std::move(control);
            if (*state.sourceSchema == kPreviousUIDocumentSchemaVersion &&
                !state.legacyImageAssets.emplace(elementId, legacyImageAssetId).second) return std::nullopt;
        } else if (command == "rect" && *state.sourceSchema == UIDocument::kSchemaVersion) {
            UIElementId elementId = 0U;
            UIRectTransform value{};
            if (!(input >> elementId >> value.anchorMin.x >> value.anchorMin.y >> value.anchorMax.x >> value.anchorMax.y >>
                  value.offsetMin.x >> value.offsetMin.y >> value.offsetMax.x >> value.offsetMax.y >>
                  value.pivot.x >> value.pivot.y >> value.scale.x >> value.scale.y >> value.rotationDegrees >> value.zOrder) ||
                !EndOfRecord(input) || !state.rectElements.insert(elementId).second) return std::nullopt;
            UIDocumentElement* const element = FindElement(document, elementId);
            if (element == nullptr) return std::nullopt;
            element->rect = value;
        } else if (command == "canvas" && *state.sourceSchema == UIDocument::kSchemaVersion) {
            UIElementId elementId = 0U;
            UICanvas value{};
            if (!(input >> elementId) || !ParseEnum(input, value.scaleMode, 2U) ||
                !(input >> value.referenceResolution.x >> value.referenceResolution.y >> value.scaleFactor >> value.matchWidthOrHeight) ||
                !EndOfRecord(input) || !state.canvasElements.insert(elementId).second) return std::nullopt;
            UIDocumentElement* const element = FindElement(document, elementId);
            if (element == nullptr) return std::nullopt;
            element->canvas = value;
        } else if (command == "layout" && *state.sourceSchema == UIDocument::kSchemaVersion) {
            UIElementId elementId = 0U;
            UIContainerLayout value{};
            if (!(input >> elementId) || !ParseEnum(input, value.mode, 6U) ||
                !(input >> value.padding.left >> value.padding.top >> value.padding.right >> value.padding.bottom >>
                  value.spacing.x >> value.spacing.y) || !ParseEnum(input, value.horizontalAlignment, 4U) ||
                !ParseEnum(input, value.verticalAlignment, 4U) ||
                !(input >> value.cellSize.x >> value.cellSize.y >> value.columns) || !EndOfRecord(input) ||
                !state.layoutElements.insert(elementId).second) return std::nullopt;
            UIDocumentElement* const element = FindElement(document, elementId);
            if (element == nullptr) return std::nullopt;
            element->layout = value;
        } else if (command == "paint" && *state.sourceSchema == UIDocument::kSchemaVersion) {
            UIElementId elementId = 0U;
            UIPaint value{};
            if (!(input >> elementId >> value.backgroundColor.r >> value.backgroundColor.g >> value.backgroundColor.b >> value.backgroundColor.a >>
                  value.borderColor.r >> value.borderColor.g >> value.borderColor.b >> value.borderColor.a >>
                  value.borderWidth.left >> value.borderWidth.top >> value.borderWidth.right >> value.borderWidth.bottom >>
                  value.cornerRadius.x >> value.cornerRadius.y >> value.cornerRadius.z >> value.cornerRadius.w >> value.opacity) ||
                !EndOfRecord(input) || !state.paintElements.insert(elementId).second) return std::nullopt;
            UIDocumentElement* const element = FindElement(document, elementId);
            if (element == nullptr) return std::nullopt;
            element->paint = value;
        } else if (command == "image" && *state.sourceSchema == UIDocument::kSchemaVersion) {
            UIElementId elementId = 0U;
            UIImage value{};
            std::string preserveAspect;
            if (!(input >> elementId >> value.imageAssetId >> value.uvRect.x >> value.uvRect.y >> value.uvRect.width >> value.uvRect.height) ||
                !ParseEnum(input, value.scaleMode, 3U) || !(input >> preserveAspect) ||
                !ParseBool(preserveAspect, value.preserveAspect) ||
                !(input >> value.nineSlice.left >> value.nineSlice.top >> value.nineSlice.right >> value.nineSlice.bottom) ||
                !EndOfRecord(input) || !state.imageElements.insert(elementId).second) return std::nullopt;
            UIDocumentElement* const element = FindElement(document, elementId);
            if (element == nullptr) return std::nullopt;
            element->image = value;
        } else if (command == "text_style" && *state.sourceSchema == UIDocument::kSchemaVersion) {
            UIElementId elementId = 0U;
            UIText value{};
            if (!(input >> elementId >> value.fontAssetId >> value.fontSize >> value.color.r >> value.color.g >> value.color.b >> value.color.a) ||
                !ParseEnum(input, value.horizontalAlignment, 3U) || !ParseEnum(input, value.verticalAlignment, 3U) ||
                !ParseEnum(input, value.wrapMode, 3U) || !EndOfRecord(input) ||
                !state.textElements.insert(elementId).second) return std::nullopt;
            UIDocumentElement* const element = FindElement(document, elementId);
            if (element == nullptr) return std::nullopt;
            element->textStyle = value;
        } else if (command == "interaction" && *state.sourceSchema == UIDocument::kSchemaVersion) {
            UIElementId elementId = 0U;
            UIInteraction value{};
            std::string raycastTarget;
            std::string interactable;
            if (!(input >> elementId >> raycastTarget >> interactable) ||
                !ParseBool(raycastTarget, value.raycastTarget) || !ParseBool(interactable, value.interactable) ||
                !ParseEnum(input, value.navigationMode, 3U) ||
                !(input >> value.navigationUp >> value.navigationDown >> value.navigationLeft >> value.navigationRight >>
                    std::quoted(value.eventName)) || !EndOfRecord(input) ||
                !state.interactionElements.insert(elementId).second) return std::nullopt;
            UIDocumentElement* const element = FindElement(document, elementId);
            if (element == nullptr) return std::nullopt;
            element->interaction = std::move(value);
        } else if (command == "effects" && *state.sourceSchema == UIDocument::kSchemaVersion) {
            UIElementId elementId = 0U;
            UIEffects value{};
            std::string clipChildren;
            std::string mask;
            std::string shadowEnabled;
            std::string outlineEnabled;
            if (!(input >> elementId >> clipChildren >> mask >> shadowEnabled >> value.shadowOffset.x >> value.shadowOffset.y >>
                  value.shadowColor.r >> value.shadowColor.g >> value.shadowColor.b >> value.shadowColor.a >> value.shadowBlur >>
                  outlineEnabled >> value.outlineColor.r >> value.outlineColor.g >> value.outlineColor.b >> value.outlineColor.a >>
                  value.outlineWidth >> value.backgroundBlur) ||
                !ParseBool(clipChildren, value.clipChildren) || !ParseBool(mask, value.mask) ||
                !ParseBool(shadowEnabled, value.shadowEnabled) || !ParseBool(outlineEnabled, value.outlineEnabled) ||
                !EndOfRecord(input) || !state.effectElements.insert(elementId).second) return std::nullopt;
            UIDocumentElement* const element = FindElement(document, elementId);
            if (element == nullptr) return std::nullopt;
            element->effects = value;
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
    if (!state.sourceSchema || state.controlElements.size() != document.elements.size()) return std::nullopt;
    if (*state.sourceSchema == UIDocument::kSchemaVersion && state.rectElements.size() != document.elements.size()) return std::nullopt;
    if (*state.sourceSchema == kPreviousUIDocumentSchemaVersion &&
        !MigrateV1(document, state.legacyImageAssets)) return std::nullopt;
    return ValidateDocument(document) ? std::optional<UIDocument>{ std::move(document) } : std::nullopt;
}

std::optional<UIStyleAsset> UIAssetIO::LoadStyle(const std::filesystem::path& path) {
    const auto text = ReadText(path);
    if (!text) return std::nullopt;
    return LoadStyle(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(text->data()), text->size() });
}

std::optional<UIStyleAsset> UIAssetIO::LoadStyle(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return std::nullopt;
    const std::string text{ reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    UIStyleAsset style{};
    std::istringstream file{ text };
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
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << "schema " << UIDocument::kSchemaVersion << '\n';
    output << "style " << document.styleAssetId << '\n';
    for (const UIDocumentElement& element : document.elements) {
        output << "element " << element.id << ' ' << element.parentId << ' ' << element.siblingOrder << ' '
               << std::quoted(element.name) << ' ' << std::quoted(element.styleClass) << ' '
               << (element.visible ? "true" : "false") << '\n';
        const UIRectTransform& rect = element.rect;
        output << "rect " << element.id << ' ' << rect.anchorMin.x << ' ' << rect.anchorMin.y << ' '
               << rect.anchorMax.x << ' ' << rect.anchorMax.y << ' ' << rect.offsetMin.x << ' ' << rect.offsetMin.y << ' '
               << rect.offsetMax.x << ' ' << rect.offsetMax.y << ' ' << rect.pivot.x << ' ' << rect.pivot.y << ' '
               << rect.scale.x << ' ' << rect.scale.y << ' ' << rect.rotationDegrees << ' ' << rect.zOrder << '\n';
        if (element.canvas) {
            const UICanvas& canvas = *element.canvas;
            output << "canvas " << element.id << ' ' << static_cast<std::uint32_t>(canvas.scaleMode) << ' '
                   << canvas.referenceResolution.x << ' ' << canvas.referenceResolution.y << ' '
                   << canvas.scaleFactor << ' ' << canvas.matchWidthOrHeight << '\n';
        }
        if (element.layout) {
            const UIContainerLayout& layout = *element.layout;
            output << "layout " << element.id << ' ' << static_cast<std::uint32_t>(layout.mode) << ' '
                   << layout.padding.left << ' ' << layout.padding.top << ' ' << layout.padding.right << ' ' << layout.padding.bottom << ' '
                   << layout.spacing.x << ' ' << layout.spacing.y << ' '
                   << static_cast<std::uint32_t>(layout.horizontalAlignment) << ' '
                   << static_cast<std::uint32_t>(layout.verticalAlignment) << ' '
                   << layout.cellSize.x << ' ' << layout.cellSize.y << ' ' << layout.columns << '\n';
        }
        if (element.paint) {
            const UIPaint& paint = *element.paint;
            output << "paint " << element.id << ' ' << paint.backgroundColor.r << ' ' << paint.backgroundColor.g << ' '
                   << paint.backgroundColor.b << ' ' << paint.backgroundColor.a << ' ' << paint.borderColor.r << ' '
                   << paint.borderColor.g << ' ' << paint.borderColor.b << ' ' << paint.borderColor.a << ' '
                   << paint.borderWidth.left << ' ' << paint.borderWidth.top << ' ' << paint.borderWidth.right << ' '
                   << paint.borderWidth.bottom << ' ' << paint.cornerRadius.x << ' ' << paint.cornerRadius.y << ' '
                   << paint.cornerRadius.z << ' ' << paint.cornerRadius.w << ' ' << paint.opacity << '\n';
        }
        if (element.image) {
            const UIImage& image = *element.image;
            output << "image " << element.id << ' ' << image.imageAssetId << ' ' << image.uvRect.x << ' '
                   << image.uvRect.y << ' ' << image.uvRect.width << ' ' << image.uvRect.height << ' '
                   << static_cast<std::uint32_t>(image.scaleMode) << ' ' << (image.preserveAspect ? "true" : "false") << ' '
                   << image.nineSlice.left << ' ' << image.nineSlice.top << ' ' << image.nineSlice.right << ' '
                   << image.nineSlice.bottom << '\n';
        }
        if (element.textStyle) {
            const UIText& textStyle = *element.textStyle;
            output << "text_style " << element.id << ' ' << textStyle.fontAssetId << ' ' << textStyle.fontSize << ' '
                   << textStyle.color.r << ' ' << textStyle.color.g << ' ' << textStyle.color.b << ' ' << textStyle.color.a << ' '
                   << static_cast<std::uint32_t>(textStyle.horizontalAlignment) << ' '
                   << static_cast<std::uint32_t>(textStyle.verticalAlignment) << ' '
                   << static_cast<std::uint32_t>(textStyle.wrapMode) << '\n';
        }
        if (element.interaction) {
            const UIInteraction& interaction = *element.interaction;
            output << "interaction " << element.id << ' ' << (interaction.raycastTarget ? "true" : "false") << ' '
                   << (interaction.interactable ? "true" : "false") << ' '
                   << static_cast<std::uint32_t>(interaction.navigationMode) << ' '
                   << interaction.navigationUp << ' ' << interaction.navigationDown << ' '
                   << interaction.navigationLeft << ' ' << interaction.navigationRight << ' '
                   << std::quoted(interaction.eventName) << '\n';
        }
        if (element.effects) {
            const UIEffects& effects = *element.effects;
            output << "effects " << element.id << ' ' << (effects.clipChildren ? "true" : "false") << ' '
                   << (effects.mask ? "true" : "false") << ' ' << (effects.shadowEnabled ? "true" : "false") << ' '
                   << effects.shadowOffset.x << ' ' << effects.shadowOffset.y << ' ' << effects.shadowColor.r << ' '
                   << effects.shadowColor.g << ' ' << effects.shadowColor.b << ' ' << effects.shadowColor.a << ' '
                   << effects.shadowBlur << ' ' << (effects.outlineEnabled ? "true" : "false") << ' '
                   << effects.outlineColor.r << ' ' << effects.outlineColor.g << ' ' << effects.outlineColor.b << ' '
                   << effects.outlineColor.a << ' ' << effects.outlineWidth << ' ' << effects.backgroundBlur << '\n';
        }
        const UIControlState& control = element.control;
        output << "control " << element.id << ' ' << UIControlKindName(control.kind) << ' ' << std::quoted(control.text) << ' '
               << (control.toggleValue ? "true" : "false") << ' '
               << control.sliderValue << ' ' << control.sliderMinimum << ' ' << control.sliderMaximum << ' '
               << control.scrollOffset << ' ' << (control.modalOpen ? "true" : "false") << ' '
               << control.selectedIndex << ' ' << control.listItems.size();
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
