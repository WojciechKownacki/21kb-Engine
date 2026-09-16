#include "engine/scene/SceneUIHierarchyPresets.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/ui/UIComponentCatalog.hpp"

#include <string>
#include <utility>

namespace kb::scene {
namespace {

using kb::math::Color;
using kb::math::Vec2;

[[nodiscard]] UIRectTransform Anchored(Vec2 anchorMin, Vec2 anchorMax, Vec2 offsetMin, Vec2 offsetMax, Vec2 pivot = {0.5F, 0.5F}) {
    UIRectTransform rect{};
    rect.anchorMin = anchorMin;
    rect.anchorMax = anchorMax;
    rect.offsetMin = offsetMin;
    rect.offsetMax = offsetMax;
    rect.pivot = pivot;
    return rect;
}

[[nodiscard]] UIText Label(std::string_view content, float size, UITextHorizontalAlignment alignment) {
    UIText text{};
    static_cast<void>(SetUITextContent(text, content));
    text.fontSize = size;
    text.color = {0.90F, 0.92F, 0.95F, 1.0F};
    text.horizontalAlignment = alignment;
    text.verticalAlignment = UITextVerticalAlignment::Center;
    text.wrapMode = UITextWrapMode::NoWrap;
    text.richText = false;
    return text;
}

[[nodiscard]] UIBorder Surface(Color background) {
    UIBorder border{};
    border.backgroundColor = background;
    border.borderColor = {0.42F, 0.46F, 0.54F, 1.0F};
    border.borderWidth = {1.0F, 1.0F, 1.0F, 1.0F};
    border.cornerRadius = {3.0F, 3.0F, 3.0F, 3.0F};
    return border;
}

class Builder {
  public:
    Builder(Scene& scene, std::vector<SceneEntity>* created) : scene_(scene), created_(created) {}

    [[nodiscard]] SceneEntity Add(SceneObject parent, std::string_view name, const UIComponentSet& components) {
        SceneObjectDesc desc;
        desc.name = std::string{name};
        desc.parent = parent;
        const SceneEntity entity = scene_.Entities().CreateObject(std::move(desc)).Entity();
        ApplySceneUIComponents(scene_.Components().UI(), entity, components);
        if (created_ != nullptr) created_->push_back(entity);
        return entity;
    }

    [[nodiscard]] SceneObject Object(SceneEntity entity) const noexcept { return scene_.Entities().Object(entity); }

  private:
    Scene& scene_;
    std::vector<SceneEntity>* created_;
};

} // namespace

SceneEntity CreateUIDropdownHierarchy(Scene& scene, SceneObject parent, std::string_view name,
                                      std::vector<SceneEntity>* created) {
    Builder build{scene, created};

    UIComponentSet root = BuildUIComponentPreset(UIComponentPreset::Dropdown);
    const SceneEntity dropdown = build.Add(parent, name, root);

    UIComponentSet label;
    label.rectTransform = Anchored({0.0F, 0.0F}, {1.0F, 1.0F}, {10.0F, 6.0F}, {-25.0F, -7.0F});
    label.text = Label("Option A", 14.0F, UITextHorizontalAlignment::Left);
    const SceneEntity caption = build.Add(build.Object(dropdown), "Label", label);

    UIComponentSet arrow;
    arrow.rectTransform = Anchored({1.0F, 0.5F}, {1.0F, 0.5F}, {-25.0F, -10.0F}, {-5.0F, 10.0F});
    arrow.text = Label("\xE2\x96\xBC", 11.0F, UITextHorizontalAlignment::Center);
    static_cast<void>(build.Add(build.Object(dropdown), "Arrow", arrow));

    UIComponentSet templateSet;
    templateSet.rectTransform = Anchored({0.0F, 1.0F}, {1.0F, 1.0F}, {0.0F, 2.0F}, {0.0F, 152.0F}, {0.5F, 0.0F});
    templateSet.border = Surface({0.14F, 0.16F, 0.20F, 1.0F});
    // Hidden, not interactive and not hit: the template exists only to be cloned when the list opens.
    UICanvasGroup hidden{};
    hidden.opacity = 0.0F;
    hidden.interactable = false;
    hidden.blocksRaycasts = false;
    templateSet.canvasGroup = hidden;
    const SceneEntity templateEntity = build.Add(build.Object(dropdown), "Template", templateSet);

    UIComponentSet viewport;
    viewport.rectTransform = Anchored({0.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}, {-18.0F, 0.0F});
    viewport.mask = UIMask{.showGraphic = false};
    UIScrollView scroll{};
    scroll.horizontal = false;
    scroll.vertical = true;
    scroll.inertia = false;
    viewport.scrollView = scroll;
    const SceneEntity viewportEntity = build.Add(build.Object(templateEntity), "Viewport", viewport);

    UIComponentSet content;
    content.rectTransform = Anchored({0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 0.0F}, {0.0F, 28.0F});
    const SceneEntity contentEntity = build.Add(build.Object(viewportEntity), "Content", content);

    UIComponentSet itemSet;
    itemSet.rectTransform = Anchored({0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 4.0F}, {0.0F, 24.0F});
    itemSet.selectable.emplace();
    itemSet.selectable->highlightedColor = {0.80F, 0.86F, 1.0F, 1.0F};
    itemSet.toggle = UIToggle{.toggled = true};
    const SceneEntity item = build.Add(build.Object(contentEntity), "Item", itemSet);

    UIComponentSet background;
    background.rectTransform = Anchored({0.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}, {0.0F, 0.0F});
    background.border = UIBorder{};
    background.border->backgroundColor = {0.24F, 0.28F, 0.36F, 1.0F};
    const SceneEntity itemBackground = build.Add(build.Object(item), "Item Background", background);

    UIComponentSet checkmark;
    checkmark.rectTransform = Anchored({0.0F, 0.5F}, {0.0F, 0.5F}, {0.0F, -10.0F}, {20.0F, 10.0F});
    checkmark.text = Label("\xE2\x9C\x93", 13.0F, UITextHorizontalAlignment::Center);
    const SceneEntity itemCheckmark = build.Add(build.Object(item), "Item Checkmark", checkmark);

    UIComponentSet itemLabel;
    itemLabel.rectTransform = Anchored({0.0F, 0.0F}, {1.0F, 1.0F}, {20.0F, 1.0F}, {-10.0F, -2.0F});
    itemLabel.text = Label("Option A", 14.0F, UITextHorizontalAlignment::Left);
    const SceneEntity itemLabelEntity = build.Add(build.Object(item), "Item Label", itemLabel);

    UIComponentSet scrollbarSet;
    scrollbarSet.rectTransform = Anchored({1.0F, 0.0F}, {1.0F, 1.0F}, {-18.0F, 0.0F}, {0.0F, 0.0F});
    scrollbarSet.border = Surface({0.12F, 0.13F, 0.16F, 1.0F});
    scrollbarSet.selectable.emplace();
    scrollbarSet.scrollbar = UIScrollbar{.value = 0.0F, .size = 0.2F, .direction = UIAxisDirection::TopToBottom};
    const SceneEntity scrollbar = build.Add(build.Object(templateEntity), "Scrollbar", scrollbarSet);

    SceneUIComponents ui = scene.Components().UI();
    UISelectable itemSelectable = *ui.TryGet<UISelectable>(item);
    itemSelectable.targetGraphic = itemBackground.Id();
    ui.Set(item, itemSelectable);
    UIToggle itemToggle = *ui.TryGet<UIToggle>(item);
    itemToggle.graphic = itemCheckmark.Id();
    ui.Set(item, itemToggle);
    UIScrollView viewportScroll = *ui.TryGet<UIScrollView>(viewportEntity);
    viewportScroll.verticalScrollbar = scrollbar.Id();
    ui.Set(viewportEntity, viewportScroll);
    UIDropdown dropdownComponent = *ui.TryGet<UIDropdown>(dropdown);
    dropdownComponent.templateEntity = templateEntity.Id();
    dropdownComponent.captionText = caption.Id();
    dropdownComponent.itemText = itemLabelEntity.Id();
    ui.Set(dropdown, dropdownComponent);
    return dropdown;
}

} // namespace kb::scene
