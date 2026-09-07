#pragma once

#include "engine/ui/effects/UIEffects.hpp"
#include "engine/ui/interaction/UIControlKind.hpp"
#include "engine/ui/interaction/UIInteraction.hpp"
#include "engine/ui/layout/UICanvas.hpp"
#include "engine/ui/layout/UIContainerLayout.hpp"
#include "engine/ui/layout/UIRectTransform.hpp"
#include "engine/ui/visual/UIImage.hpp"
#include "engine/ui/visual/UIPaint.hpp"
#include "engine/ui/visual/UIText.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

using UIElementId = std::uint64_t;
inline constexpr std::size_t kMaxUIListItems = 4096U;
inline constexpr std::uint32_t kMaxUIVirtualListViewportItems = 512U;
inline constexpr std::uint32_t kMaxUIVirtualListOverscanItems = 128U;
inline constexpr std::size_t kMaxUITextBytes = 4096U;
inline constexpr std::size_t kMaxUIEventTextBytes = kMaxUITextBytes;
inline constexpr std::size_t kMaxUIActionNameBytes = 256U;

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

// This is only the authored boundary. The runtime supplies the data source and
// feedback-loop policy; a document never owns game data.
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
// event dispatch and data binding remain separate runtime responsibilities, so
// controls do not own callbacks or game data.
struct UIControlState {
    UIControlKind kind = UIControlKind::Container;
    std::string text;
    bool toggleValue = false;
    float sliderValue = 0.0F;
    float sliderMinimum = 0.0F;
    float sliderMaximum = 1.0F;
    std::vector<std::string> listItems;
    std::uint32_t selectedIndex = 0U;
    float scrollOffset = 0.0F;
    bool modalOpen = false;
};

// A slot is a lightweight view into a retained List control's item data, not
// a UI tree element. Its text view remains valid until the next UI queue
// boundary affecting that list.
struct UIVirtualListItem {
    std::uint32_t index = 0U;
    std::string_view text;
};

struct UIVirtualListView {
    std::uint32_t totalItemCount = 0U;
    std::uint32_t firstVisibleIndex = 0U;
    std::span<const UIVirtualListItem> pooledItems;
};

// Input routing produces these data-only records. The scene owns their FIFO
// queue; ScriptRuntimeSceneSystem is the sole consumer that translates them
// into ScriptEventBus events, so controls never retain callbacks or script state.
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
    // Optional authored action name. The standard typed UI event is always
    // emitted; this additionally targets a project-defined script event.
    std::string eventName;
};

struct UIDocumentElement {
    UIElementId id = 0U;
    UIElementId parentId = 0U;
    std::uint32_t siblingOrder = 0U;
    std::string name;
    std::string styleClass;
    bool visible = true;
    UIRectTransform rect;
    std::optional<UICanvas> canvas;
    std::optional<UIContainerLayout> layout;
    std::optional<UIPaint> paint;
    std::optional<UIImage> image;
    std::optional<UIText> textStyle;
    std::optional<UIInteraction> interaction;
    std::optional<UIEffects> effects;
    UIControlState control;
};

// Copyable command/query view of the authorable component set on one runtime
// element. It has no identity or hierarchy fields and is never stored beside
// UIDocumentElement, so the retained runtime tree remains the sole owner.
struct UIElementComponents {
    UIRectTransform rect;
    std::optional<UICanvas> canvas;
    std::optional<UIContainerLayout> layout;
    std::optional<UIPaint> paint;
    std::optional<UIImage> image;
    std::optional<UIText> textStyle;
    std::optional<UIInteraction> interaction;
    std::optional<UIEffects> effects;
};

// Mutable runtime-only element description.  It is copied into the sole
// scene-owned UI tree when its queued creation command reaches the boundary.
struct UIRuntimeElementDesc {
    UIElementId parentId = 0U;
    std::string name;
    std::string styleClass;
    bool visible = true;
    UIElementComponents components;
    UIControlState control;
};

struct UIDocument {
    static constexpr std::uint32_t kSchemaVersion = 2U;

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
