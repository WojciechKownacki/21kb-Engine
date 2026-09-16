#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/ui/UIComponentSet.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

enum class UIInteractionState : std::uint8_t {
    Normal,
    Hovered,
    Pressed,
    Focused,
    Disabled,
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
    float scrollDelta = 0.0F;
    std::span<const char32_t> textInput;
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
    std::array<char, UISelectable::MaxEventNameBytes> name{};
    kb::math::Vec2 pointerPosition{};
    float value = 0.0F;
    float value2 = 0.0F;
    bool pointerAvailable = false;
    std::array<char, UIText::MaxUtf8Bytes> text{};
};

[[nodiscard]] std::string_view SceneUIEventName(const SceneUIEvent& event) noexcept;
[[nodiscard]] std::string_view SceneUIEventText(const SceneUIEvent& event) noexcept;

struct SceneUIFrameElement {
    SceneEntity entity{};
    SceneEntity canvas{};
    kb::math::Rect rect{};
    std::array<kb::math::Vec2, 4U> corners{};
    kb::math::Rect clipRect{};
    std::vector<std::array<kb::math::Vec2, 4U>> clipQuads;
    float canvasScale = 1.0F;
    float effectiveOpacity = 1.0F;
    std::int32_t canvasSortingOrder = 0;
    std::int32_t zOrder = 0;
    std::uint32_t traversalOrder = 0U;
    UIInteractionState interactionState = UIInteractionState::Normal;
    kb::math::Color interactionTint{};
    bool interactionEnabled = false;
    bool hitTestable = false;
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
    // Where the text caret sits, and whether the blink is currently showing it. Only set on the
    // focused input field; editing worked before this but drew no insertion point at all, so a
    // shipped text field looked inert.
    std::uint32_t textCaretByteOffset = 0U;
    bool textCaretVisible = false;
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

    [[nodiscard]] SceneEntity HitTest(kb::math::Vec2 point) const noexcept;
};

class SceneUIQueries {
  public:
    explicit SceneUIQueries(const Scene& scene) noexcept;
    [[nodiscard]] const SceneUIFrame& Frame() const noexcept;
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
    [[nodiscard]] bool UpdateFromInput(float deltaSeconds);
    [[nodiscard]] bool Update(float viewportWidth, float viewportHeight, const SceneUIInput& input, float deltaSeconds);
    [[nodiscard]] const SceneUIFrame& Frame() const noexcept;
    [[nodiscard]] SceneEntity HitTest(kb::math::Vec2 point) const noexcept;
    [[nodiscard]] SceneEntity Hovered() const noexcept;
    [[nodiscard]] SceneEntity Pressed() const noexcept;
    [[nodiscard]] SceneEntity Focused() const noexcept;
    [[nodiscard]] bool HasFocusedTextInput() const noexcept;
    [[nodiscard]] std::span<const SceneUIEvent> Events() const noexcept;
    [[nodiscard]] std::vector<SceneUIEvent> DrainEvents();
    [[nodiscard]] bool SetFocus(SceneEntity entity);
    void ClearFocus() noexcept;

  private:
    Scene& scene_;
};

} // namespace kb::scene
