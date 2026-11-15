#include "engine/scene/SceneUIComponents.hpp"

#include "engine/ecs/World.hpp"
#include "engine/ui/UIComponentValidation.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

#include <stdexcept>

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] bool Has(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).world.Has<T>(entity);
}

template <typename T>
[[nodiscard]] const T* TryGet(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).world.TryGet<T>(entity) : nullptr;
}

template <typename T>
[[nodiscard]] T* TryGet(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).world.TryGetMutable<T>(entity) : nullptr;
}

template <typename T>
void SetComponent(Scene& scene, SceneEntity entity, const void* component, std::size_t size) {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    if (component == nullptr || size != sizeof(T)) {
        throw std::invalid_argument("UI component payload does not match its registered type");
    }
    if (!IsUIComponentValid(*static_cast<const T*>(component))) {
        throw std::invalid_argument("UI component contains an invalid authored value");
    }
    SceneState& state = SceneAccess::State(scene);
    state.world.Set<T>(entity, *static_cast<const T*>(component));
    MarkScenePrefabNodeDirty(state, entity);
}

template <typename T>
void RemoveComponent(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.world.Remove<T>(entity);
    MarkScenePrefabNodeDirty(state, entity);
}

template <typename T>
void MarkComponentModified(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.world.MarkModified<T>(entity);
    MarkScenePrefabNodeDirty(state, entity);
}

#define KB_UI_COMPONENTS(X) \
    X(RectTransform, UIRectTransform) X(Canvas, UICanvas) X(CanvasScaler, UICanvasScaler) X(CanvasGroup, UICanvasGroup) \
    X(HorizontalLayout, UIHorizontalLayout) X(VerticalLayout, UIVerticalLayout) X(GridLayout, UIGridLayout) \
    X(WrapLayout, UIWrapLayout) X(OverlayLayout, UIOverlayLayout) X(LayoutElement, UILayoutElement) \
    X(ContentSizeFitter, UIContentSizeFitter) X(AspectRatioFitter, UIAspectRatioFitter) X(Sprite, UISprite) \
    X(Image, UIImage) X(RawImage, UIRawImage) X(Text, UIText) X(Border, UIBorder) X(Mask, UIMask) \
    X(Shadow, UIShadow) X(Outline, UIOutline) X(BackgroundBlur, UIBackgroundBlur) X(Selectable, UISelectable) \
    X(Button, UIButton) X(Toggle, UIToggle) X(Slider, UISlider) X(Scrollbar, UIScrollbar) X(ScrollView, UIScrollView) \
    X(InputField, UIInputField) X(Dropdown, UIDropdown) X(ProgressBar, UIProgressBar) X(WidgetSwitcher, UIWidgetSwitcher) X(Accessibility, UIAccessibility)

[[nodiscard]] bool HasByType(const Scene& scene, SceneEntity entity, UIComponentType type) noexcept {
    switch (type) {
#define KB_UI_HAS(Value, Component) case UIComponentType::Value: return Has<Component>(scene, entity);
        KB_UI_COMPONENTS(KB_UI_HAS)
#undef KB_UI_HAS
    }
    return false;
}

[[nodiscard]] const void* TryGetByType(const Scene& scene, SceneEntity entity, UIComponentType type) noexcept {
    switch (type) {
#define KB_UI_GET(Value, Component) case UIComponentType::Value: return TryGet<Component>(scene, entity);
        KB_UI_COMPONENTS(KB_UI_GET)
#undef KB_UI_GET
    }
    return nullptr;
}

[[nodiscard]] void* TryGetByType(Scene& scene, SceneEntity entity, UIComponentType type) noexcept {
    switch (type) {
#define KB_UI_GET(Value, Component) case UIComponentType::Value: return TryGet<Component>(scene, entity);
        KB_UI_COMPONENTS(KB_UI_GET)
#undef KB_UI_GET
    }
    return nullptr;
}

// Moves the write revision and remembers what was written, so a layout can visit only that. Past a few hundred writes
// the list is dropped and the next layout starts over.
constexpr std::size_t kMaxRecordedUIWrites = 256U;
void NoteWrite(Scene& scene, SceneEntity entity, UIComponentType type, bool structural) noexcept {
    SceneState& state = SceneAccess::State(scene);
    ++state.uiWriteRevision;
    if (state.uiWritesOverflow)
        return;
    const auto code = static_cast<std::uint8_t>(type);
    if (!state.uiWrites.empty()) {
        SceneState::UIWrite& last = state.uiWrites.back();
        if (last.entity == entity.Id() && last.type == code) {
            last.structural = last.structural || structural;
            return;
        }
    }
    if (state.uiWrites.size() >= kMaxRecordedUIWrites) {
        state.uiWritesOverflow = true;
        return;
    }
    try {
        state.uiWrites.push_back({entity.Id(), code, structural});
    } catch (...) {
        state.uiWritesOverflow = true;
    }
}

} // namespace

SceneUIComponentQueries::SceneUIComponentQueries(const Scene& scene) noexcept : scene_(scene) {}

bool SceneUIComponentQueries::HasRaw(SceneEntity entity, UIComponentType type) const noexcept {
    return HasByType(scene_, entity, type);
}

const void* SceneUIComponentQueries::TryGetRaw(SceneEntity entity, UIComponentType type) const noexcept {
    return TryGetByType(scene_, entity, type);
}

SceneUIComponents::SceneUIComponents(Scene& scene) noexcept : scene_(scene) {}

bool SceneUIComponents::HasRaw(SceneEntity entity, UIComponentType type) const noexcept {
    return HasByType(scene_, entity, type);
}

const void* SceneUIComponents::TryGetRaw(SceneEntity entity, UIComponentType type) const noexcept {
    return TryGetByType(static_cast<const Scene&>(scene_), entity, type);
}

void* SceneUIComponents::TryGetMutableRaw(SceneEntity entity, UIComponentType type) noexcept {
    NoteWrite(scene_, entity, type, false);
    SceneHistoryService::NoteObjectChanging(scene_, entity);
    return TryGetByType(scene_, entity, type);
}

void SceneUIComponents::SetRaw(SceneEntity entity, UIComponentType type, const void* component, std::size_t size) {
    NoteWrite(scene_, entity, type, !HasByType(scene_, entity, type));
    SceneHistoryService::NoteObjectChanging(scene_, entity);
    switch (type) {
#define KB_UI_SET_CASE(Value, Component) case UIComponentType::Value: SetComponent<Component>(scene_, entity, component, size); return;
        KB_UI_COMPONENTS(KB_UI_SET_CASE)
#undef KB_UI_SET_CASE
    }
}

void SceneUIComponents::RemoveRaw(SceneEntity entity, UIComponentType type) noexcept {
    NoteWrite(scene_, entity, type, true);
    SceneHistoryService::NoteObjectChanging(scene_, entity);
    switch (type) {
#define KB_UI_REMOVE_CASE(Value, Component) case UIComponentType::Value: RemoveComponent<Component>(scene_, entity); return;
        KB_UI_COMPONENTS(KB_UI_REMOVE_CASE)
#undef KB_UI_REMOVE_CASE
    }
}

void SceneUIComponents::MarkModifiedRaw(SceneEntity entity, UIComponentType type) noexcept {
    NoteWrite(scene_, entity, type, false);
    switch (type) {
#define KB_UI_MARK_CASE(Value, Component) case UIComponentType::Value: MarkComponentModified<Component>(scene_, entity); return;
        KB_UI_COMPONENTS(KB_UI_MARK_CASE)
#undef KB_UI_MARK_CASE
    }
}

#undef KB_UI_COMPONENTS

} // namespace kb::scene
