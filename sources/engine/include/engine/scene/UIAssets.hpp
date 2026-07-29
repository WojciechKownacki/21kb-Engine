#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

using UIElementId = std::uint64_t;
inline constexpr std::size_t kMaxUIListItems = 4096U;
inline constexpr std::size_t kMaxUIEventTextBytes = 4096U;

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

// Typed scalar exchanged across the UI data-binding boundary. The document
// only names a source path; the host supplies the actual data source, so UI
// assets never retain or serialize game/script state.
struct UIBindingValue {
    UIDataValueType type = UIDataValueType::String;
    bool boolean = false;
    double number = 0.0;
    std::string string;

    [[nodiscard]] bool operator==(const UIBindingValue& other) const noexcept {
        return type == other.type && boolean == other.boolean && number == other.number && string == other.string;
    }
};

// A runtime supplies this narrow boundary when it synchronizes retained UI
// binding declarations. Read/Write are deliberately typed: a bad source type
// is rejected instead of being implicitly converted or silently clamped.
class UIBindingDataSource {
public:
    virtual ~UIBindingDataSource() = default;
    [[nodiscard]] virtual std::optional<UIBindingValue> Read(std::string_view sourcePath, UIDataValueType type) const = 0;
    [[nodiscard]] virtual bool Write(std::string_view sourcePath, const UIBindingValue& value) = 0;
};

// The control category and its value state are deliberately data-only. Input,
// event dispatch and data binding remain separate runtime responsibilities
// (LIB-176 and LIB-178), so controls do not own callbacks or game data.
enum class UIControlKind : std::uint8_t {
    Container,
    Text,
    Image,
    Button,
    Toggle,
    Slider,
    List,
    InputField,
    ScrollView,
    ModalDialog,
};

struct UIControlState {
    UIControlKind kind = UIControlKind::Container;
    std::string text;
    std::uint64_t imageAssetId = 0U;
    bool toggleValue = false;
    float sliderValue = 0.0F;
    float sliderMinimum = 0.0F;
    float sliderMaximum = 1.0F;
    std::vector<std::string> listItems;
    float scrollOffset = 0.0F;
    bool modalOpen = false;
};

// LIB-176: input routing (LIB-180) produces these data-only records.  The
// scene owns their FIFO queue; ScriptRuntimeSceneSystem is the sole consumer
// that translates them into ScriptEventBus events, so controls never retain
// callbacks or script state.
enum class UIRuntimeEventKind : std::uint8_t {
    Click,
    Pointer,
    Submit,
    Changed,
    Focus,
    Navigation,
};

enum class UINavigationDirection : std::uint8_t {
    None,
    Next,
    Previous,
    Up,
    Down,
    Left,
    Right,
};

struct UIRuntimeEvent {
    UIRuntimeEventKind kind = UIRuntimeEventKind::Click;
    UIElementId elementId = 0U;
    float pointerX = 0.0F;
    float pointerY = 0.0F;
    float value = 0.0F;
    std::string text;
    bool focused = false;
    UINavigationDirection navigation = UINavigationDirection::None;
};

struct UIDocumentElement {
    UIElementId id = 0U;
    UIElementId parentId = 0U;
    std::string name;
    std::string styleClass;
    bool visible = true;
    UIControlState control;
};

// Mutable runtime-only element description.  It is copied into the sole
// scene-owned UI tree when its queued creation command reaches the boundary.
struct UIRuntimeElementDesc {
    UIElementId parentId = 0U;
    std::string name;
    std::string styleClass;
    bool visible = true;
    UIControlState control;
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
