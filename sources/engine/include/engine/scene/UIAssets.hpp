#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

using UIElementId = std::uint64_t;

// UI documents are retained assets.  The scene component only names a document;
// the derived runtime tree is owned by SceneState and is never serialized beside it.
struct UIStyleAsset {
    std::string name;
    std::vector<std::string> classes;
};

enum class UIDataValueType : std::uint8_t {
    Boolean,
    Number,
    String,
};

enum class UIBindingDirection : std::uint8_t {
    OneWay,
    TwoWay,
};

// This is only the authored boundary.  LIB-178 supplies the runtime data-source
// implementation and feedback-loop policy; a document never owns game data.
struct UIBindingDeclaration {
    UIElementId elementId = 0U;
    std::string property;
    std::string sourcePath;
    UIDataValueType valueType = UIDataValueType::String;
    UIBindingDirection direction = UIBindingDirection::OneWay;
};

struct UIDocumentElement {
    UIElementId id = 0U;
    UIElementId parentId = 0U;
    std::string name;
    std::string styleClass;
    bool visible = true;
};

struct UIDocument {
    static constexpr std::uint32_t kSchemaVersion = 1U;

    std::uint32_t schemaVersion = kSchemaVersion;
    std::uint64_t styleAssetId = 0U;
    std::vector<UIDocumentElement> elements;
    std::vector<UIBindingDeclaration> bindings;
};

// Persistent scene authoring state.  It has exactly one asset reference; all
// element state is derived from that document by the UI scene system.
struct UIDocumentComponent {
    std::uint64_t documentAssetId = 0U;
    bool enabled = true;
};

} // namespace kb::scene
