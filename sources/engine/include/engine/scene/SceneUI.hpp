#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/input/InputTouchPoint.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/ui/UIComponentSet.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

// A committed native editor snapshot. The caret counts Unicode scalar values.
// Composition stays in the platform editor until it is committed.
struct SceneUITextInputState {
    SceneEntity entity;
    std::u32string text;
    std::size_t caret = 0U;
    std::optional<std::size_t> selectionAnchor;
};

// Geometry from the last rendered text layout. Byte offsets remain logical even
// when the visual order differs. Hosts may publish the same contract without a GPU.
struct SceneUITextCaretStop {
    std::uint32_t byteOffset = 0U;
    float x = 0.0F;
    float top = 0.0F;
    float bottom = 0.0F;
    bool leading = false;
};

struct SceneUITextLinkRegion {
    std::array<kb::math::Vec2, 4U> corners{};
    std::string target;
    std::uint32_t instance = 0U;
};

struct SceneUITextLayout {
    SceneEntity entity;
    std::string source;
    std::vector<SceneUITextCaretStop> carets;
    std::vector<SceneUITextLinkRegion> links;
};

enum class UIInteractionState : std::uint8_t {
    Normal,
    Hovered,
    Pressed,
    Focused,
    Disabled,
};

// Menu navigation from one controller. Players 1-3 drive only canvases that name them, each with its own
// focus, so split-screen menus do not steal each other's selection.
struct SceneUIPlayerNavigation {
    bool navigateUp = false;
    bool navigateDown = false;
    bool navigateLeft = false;
    bool navigateRight = false;
    bool submitDown = false;
    bool cancelDown = false;

    [[nodiscard]] bool operator==(const SceneUIPlayerNavigation&) const noexcept = default;
};

struct SceneUIInput {
    kb::math::Vec2 pointerPosition{};
    bool pointerAvailable = false;
    bool primaryDown = false;
    bool navigateUp = false;
    bool navigateDown = false;
    bool navigateLeft = false;
    bool navigateRight = false;
    bool submitDown = false;
    bool cancelDown = false;
    bool backspaceDown = false;
    bool deleteDown = false;
    bool extendTextSelection = false;
    bool selectAllTextDown = false;
    bool textUpDown = false;
    bool textDownDown = false;
    bool textHomeDown = false;
    bool textEndDown = false;
    float scrollDelta = 0.0F;
    std::span<const char32_t> textInput;
    // Independent device contacts. A begin over UI owns its pointer until release;
    // contacts that start outside UI remain available to gameplay.
    std::span<const kb::input::InputTouchPoint> touchPoints;
    // Controllers of players 1, 2 and 3. Mouse, keyboard and the first controller are player 0.
    std::array<SceneUIPlayerNavigation, 3U> otherPlayers{};
};

enum class SceneUIEventType : std::uint8_t {
    HoverEntered,
    HoverExited,
    Pressed,
    Released,
    Clicked,
    Focused,
    Blurred,
    Submitted,
    Canceled,
    Changed,
    DragBegan,
    Dragged,
    DragEnded,
    Dropped,
    LinkActivated,
};

struct SceneUIEventDescriptor {
    SceneUIEventType type = SceneUIEventType::Clicked;
    std::string_view callbackName;
};

[[nodiscard]] std::span<const SceneUIEventDescriptor> SceneUIEventCatalog() noexcept;
[[nodiscard]] const SceneUIEventDescriptor* FindSceneUIEventDescriptor(SceneUIEventType type) noexcept;

struct SceneUIEvent {
    SceneUIEventType type = SceneUIEventType::Clicked;
    SceneEntity entity{};
    // The other widget of a drag: the one dropped onto this widget.
    SceneEntity other{};
    // Where the click's action event named by `name` goes: the selectable's event target, or the widget.
    SceneEntity actionTarget{};
    std::array<char, UISelectable::MaxEventNameBytes> name{};
    kb::math::Vec2 pointerPosition{};
    float value = 0.0F;
    float value2 = 0.0F;
    bool pointerAvailable = false;
    std::array<char, UIText::MaxUtf8Bytes> text{};
};

[[nodiscard]] std::string_view SceneUIEventName(const SceneUIEvent& event) noexcept;
[[nodiscard]] std::string_view SceneUIEventText(const SceneUIEvent& event) noexcept;

// An image's opacity, one byte per pixel in rows from the top. The renderer, which decodes images, hands
// it over for images whose presses only land where they are opaque.
struct SceneUIImageAlpha {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint8_t> alpha;
};

struct SceneUIFrameElement {
    SceneEntity entity{};
    SceneEntity canvas{};
    kb::math::Rect rect{};
    std::array<kb::math::Vec2, 4U> corners{};
    // Where a press lands: the corners grown by the selectable's raycast padding.
    std::array<kb::math::Vec2, 4U> hitCorners{};
    kb::math::Rect clipRect{};
    // Screen pixels over which the nearest mask fades this element out towards the mask's edge.
    float clipSoftness = 0.0F;
    std::vector<std::array<kb::math::Vec2, 4U>> clipQuads;
    float canvasScale = 1.0F;
    float effectiveOpacity = 1.0F;
    std::int32_t canvasSortingOrder = 0;
    // The player whose controller drives this element's canvas; -1 is everyone.
    std::int32_t player = -1;
    std::int32_t zOrder = 0;
    std::uint32_t traversalOrder = 0U;
    UIInteractionState interactionState = UIInteractionState::Normal;
    kb::math::Color interactionTint{};
    bool interactionEnabled = false;
    bool hitTestable = false;
    std::optional<UISelectable> selectable;
    std::optional<UISprite> sprite;
    std::optional<UIImage> image;
    std::optional<UIRawImage> rawImage;
    std::optional<UIText> text;
    std::optional<UIBorder> border;
    std::optional<UIMask> mask;
    std::optional<UIShadow> shadow;
    std::optional<UIOutline> outline;
    std::optional<UIBackgroundBlur> backgroundBlur;
    std::optional<UIToggle> toggle;
    std::optional<UISlider> slider;
    std::optional<UIScrollbar> scrollbar;
    std::optional<UIScrollView> scrollView;
    std::optional<UIDropdown> dropdown;
    std::optional<UIProgressBar> progressBar;
    std::optional<UIInputField> inputField;
    // Set on the element of a tooltip bubble and its label, which the frame adds over everything else.
    bool tooltip = false;
    // The image's opacity when its alpha hit threshold is above zero and the renderer has published it.
    std::shared_ptr<const SceneUIImageAlpha> hitAlpha;
    // Where the text caret sits, and whether the blink is currently showing it. Only set on the
    // focused input field; editing worked before this but drew no insertion point at all, so a
    // shipped text field looked inert.
    std::uint32_t textCaretByteOffset = 0U;
    bool textCaretVisible = false;
    std::uint32_t textSelectionBegin = 0U;
    std::uint32_t textSelectionEnd = 0U;
    // Language of the resolved text, which may differ from the selected locale.
    std::string textLanguage;
    // The selectable's own transition state, retained even when tint is applied to another graphic.
    kb::math::Color selectableTransitionTint{};
};

// Why a frame build refused, and which entity caused it. The frame builder is fail-closed:
// one unbuildable widget refuses the whole frame, because a half-laid-out canvas is a worse
// answer than no canvas. That refusal is only useful if it says what to fix, so every
// refusal site records the entity and a static reason string here.
// `reason` is null exactly when the frame is complete.
struct SceneUIFrameRefusal {
    SceneEntity entity{};
    const char* reason = nullptr;

    [[nodiscard]] bool HasValue() const noexcept {
        return reason != nullptr;
    }
};

struct SceneUIFrame {
    kb::math::Vec2 viewportSize{};
    std::vector<SceneUIFrameElement> elements;
    SceneUIFrameRefusal refusal{};
    // True while a modal canvas is shown; every element on a canvas sorted below modalSortingOrder is then neither
    // hit-testable nor interactive.
    bool modalActive = false;
    std::int32_t modalSortingOrder = 0;
    std::string language;

    [[nodiscard]] SceneEntity HitTest(kb::math::Vec2 point) const noexcept;
    // The topmost hit that is not `excluded` or inside it - where a dragged widget is being dropped.
    [[nodiscard]] SceneEntity HitTestExcluding(kb::math::Vec2 point, SceneEntity excluded, const Scene& scene) const noexcept;
};

class SceneUIQueries {
  public:
    explicit SceneUIQueries(const Scene& scene) noexcept;
    [[nodiscard]] const SceneUIFrame& Frame() const noexcept;
    [[nodiscard]] UIEdges SafeAreaInsets() const noexcept;
    // Player 0 is the shared focus; 1-3 the focus of those players' canvases.
    [[nodiscard]] SceneEntity PlayerFocused(std::uint32_t player) const noexcept;
    // The widget being dragged, if any.
    [[nodiscard]] SceneEntity Dragged() const noexcept;
    // Whether a modal canvas is shown, so the canvases below it take no input.
    [[nodiscard]] bool ModalCanvasActive() const noexcept;
    [[nodiscard]] bool HasImageAlpha(std::uint64_t imageAssetId) const noexcept;
    // Image assets the current frame hit-tests by opacity whose opacity has not been published yet.
    [[nodiscard]] std::vector<std::uint64_t> PendingImageAlpha() const;
    [[nodiscard]] bool BuildFrame(float viewportWidth, float viewportHeight, SceneUIFrame& output) const;
    [[nodiscard]] SceneEntity HitTest(kb::math::Vec2 point) const noexcept;
    [[nodiscard]] SceneEntity Hovered() const noexcept;
    [[nodiscard]] SceneEntity Pressed() const noexcept;
    [[nodiscard]] SceneEntity Focused() const noexcept;
    [[nodiscard]] bool HasFocusedTextInput() const noexcept;
    [[nodiscard]] std::span<const SceneUIEvent> Events() const noexcept;

  private:
    const Scene& scene_;
};

class SceneUIAccess {
  public:
    explicit SceneUIAccess(Scene& scene) noexcept;
    [[nodiscard]] bool SetViewport(float width, float height) noexcept;
    // Pixels at each screen edge that are unsafe for content - notch, rounded corners, system bars. The
    // host sets them from the platform, and a preview sets them to the device it shows. Canvases with
    // respectSafeArea lay out inside what is left. They must be finite and non-negative.
    [[nodiscard]] bool SetSafeAreaInsets(UIEdges insets) noexcept;
    [[nodiscard]] UIEdges SafeAreaInsets() const noexcept;
    // Stores an image's opacity for hit tests against images with an alpha hit threshold.
    void PublishImageAlpha(std::uint64_t imageAssetId, SceneUIImageAlpha alpha);
    [[nodiscard]] bool UpdateFromInput(float deltaSeconds);
    [[nodiscard]] bool Update(float viewportWidth, float viewportHeight, const SceneUIInput& input, float deltaSeconds);
    [[nodiscard]] const SceneUIFrame& Frame() const noexcept;
    [[nodiscard]] SceneEntity HitTest(kb::math::Vec2 point) const noexcept;
    [[nodiscard]] SceneEntity Hovered() const noexcept;
    [[nodiscard]] SceneEntity Pressed() const noexcept;
    [[nodiscard]] SceneEntity Focused() const noexcept;
    [[nodiscard]] bool ModalCanvasActive() const noexcept;
    [[nodiscard]] bool HasFocusedTextInput() const noexcept;
    [[nodiscard]] std::span<const SceneUIEvent> Events() const noexcept;
    [[nodiscard]] std::vector<SceneUIEvent> DrainEvents();
    [[nodiscard]] bool SetFocus(SceneEntity entity);
    void ClearFocus() noexcept;
    [[nodiscard]] std::optional<SceneUITextInputState> TextInputState() const;
    // Applied during the next UI update using the field's normal validation and events.
    [[nodiscard]] bool QueueTextInputState(SceneUITextInputState input);
    // Clipboard transfers use the same selection, limits and validation as typing.
    // Password and PIN fields never expose their selected contents.
    [[nodiscard]] std::optional<std::u32string> SelectedText() const;
    [[nodiscard]] bool QueueTextReplacement(std::u32string text);
    // Replaces previous render feedback; stale source snapshots are ignored by editing.
    void PublishTextLayouts(std::vector<SceneUITextLayout> layouts);

  private:
    Scene& scene_;
};

} // namespace kb::scene
