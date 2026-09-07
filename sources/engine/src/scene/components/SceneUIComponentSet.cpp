#include "engine/scene/SceneUIComponentSet.hpp"

#include "scene/asset/io/components/SceneAssetUIComponentCodec.hpp"

#include <vector>

namespace kb::scene {
namespace {

#define KB_COMPONENTS(X) \
 X(UIRectTransform,rectTransform) X(UICanvas,canvas) X(UICanvasScaler,canvasScaler) X(UICanvasGroup,canvasGroup) \
 X(UIHorizontalLayout,horizontalLayout) X(UIVerticalLayout,verticalLayout) X(UIGridLayout,gridLayout) X(UIWrapLayout,wrapLayout) X(UIOverlayLayout,overlayLayout) \
 X(UILayoutElement,layoutElement) X(UIContentSizeFitter,contentSizeFitter) X(UIAspectRatioFitter,aspectRatioFitter) \
 X(UISprite,sprite) X(UIImage,image) X(UIRawImage,rawImage) X(UIText,text) X(UIBorder,border) X(UIMask,mask) X(UIShadow,shadow) X(UIOutline,outline) X(UIBackgroundBlur,backgroundBlur) \
 X(UISelectable,selectable) X(UIButton,button) X(UIToggle,toggle) X(UISlider,slider) X(UIScrollbar,scrollbar) X(UIScrollView,scrollView) X(UIInputField,inputField) X(UIDropdown,dropdown) X(UIProgressBar,progressBar) X(UIWidgetSwitcher,widgetSwitcher)

template <typename Access>
[[nodiscard]] UIComponentSet Capture(const Access& components, SceneEntity entity) {
    UIComponentSet output;
#define KB_CAPTURE(Component, Field) if (const Component* value = components.template TryGet<Component>(entity)) output.Field = *value;
    KB_COMPONENTS(KB_CAPTURE)
#undef KB_CAPTURE
    return output;
}

template <typename T>
void ApplyOptional(SceneUIComponents components, SceneEntity entity, const std::optional<T>& value, bool removeMissing) {
    if (value) components.Set<T>(entity, *value);
    else if (removeMissing) components.Remove<T>(entity);
}

void Apply(SceneUIComponents components, SceneEntity entity, const UIComponentSet& values, bool removeMissing) {
#define KB_APPLY(Component, Field) ApplyOptional(components, entity, values.Field, removeMissing);
    KB_COMPONENTS(KB_APPLY)
#undef KB_APPLY
}

[[nodiscard]] std::vector<std::uint8_t> Encode(const UIComponentSet& components) {
    std::vector<std::uint8_t> bytes;
    SceneAssetUIComponentCodec::Write(bytes, components);
    return bytes;
}

#undef KB_COMPONENTS

} // namespace

UIComponentSet CaptureSceneUIComponents(const SceneUIComponentQueries& components, SceneEntity entity) { return Capture(components, entity); }
UIComponentSet CaptureSceneUIComponents(const SceneUIComponents& components, SceneEntity entity) { return Capture(components, entity); }
void ApplySceneUIComponents(SceneUIComponents components, SceneEntity entity, const UIComponentSet& values) { Apply(components, entity, values, false); }
void SynchronizeSceneUIComponents(SceneUIComponents components, SceneEntity entity, const UIComponentSet& values) { Apply(components, entity, values, true); }

bool AreUIComponentSetsEqual(const UIComponentSet& lhs, const UIComponentSet& rhs) noexcept {
    try { return Encode(lhs) == Encode(rhs); } catch (...) { return false; }
}

std::uint64_t HashUIComponentSet(const UIComponentSet& components) noexcept {
    try {
        std::uint64_t hash = 1469598103934665603ULL;
        for (const std::uint8_t byte : Encode(components)) { hash ^= byte; hash *= 1099511628211ULL; }
        return hash;
    } catch (...) {
        return 0U;
    }
}

} // namespace kb::scene
