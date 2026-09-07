#pragma once

#include "engine/ui/effects/UIBackgroundBlur.hpp"
#include "engine/ui/effects/UIMask.hpp"
#include "engine/ui/effects/UIOutline.hpp"
#include "engine/ui/effects/UIShadow.hpp"
#include "engine/ui/interaction/UIButton.hpp"
#include "engine/ui/interaction/UIDropdown.hpp"
#include "engine/ui/interaction/UIInputField.hpp"
#include "engine/ui/interaction/UIProgressBar.hpp"
#include "engine/ui/interaction/UIScrollbar.hpp"
#include "engine/ui/interaction/UIScrollView.hpp"
#include "engine/ui/interaction/UISelectable.hpp"
#include "engine/ui/interaction/UISlider.hpp"
#include "engine/ui/interaction/UIToggle.hpp"
#include "engine/ui/interaction/UIWidgetSwitcher.hpp"
#include "engine/ui/layout/UIAspectRatioFitter.hpp"
#include "engine/ui/layout/UICanvas.hpp"
#include "engine/ui/layout/UICanvasScaler.hpp"
#include "engine/ui/layout/UICanvasGroup.hpp"
#include "engine/ui/layout/UIContentSizeFitter.hpp"
#include "engine/ui/layout/UIGridLayout.hpp"
#include "engine/ui/layout/UIHorizontalLayout.hpp"
#include "engine/ui/layout/UILayoutElement.hpp"
#include "engine/ui/layout/UIOverlayLayout.hpp"
#include "engine/ui/layout/UIRectTransform.hpp"
#include "engine/ui/layout/UIVerticalLayout.hpp"
#include "engine/ui/layout/UIWrapLayout.hpp"
#include "engine/ui/visual/UIBorder.hpp"
#include "engine/ui/visual/UIImage.hpp"
#include "engine/ui/visual/UIRawImage.hpp"
#include "engine/ui/visual/UISprite.hpp"
#include "engine/ui/visual/UIText.hpp"

#include <optional>

namespace kb::scene {

struct UIComponentSet {
    std::optional<UIRectTransform> rectTransform;
    std::optional<UICanvas> canvas;
    std::optional<UICanvasScaler> canvasScaler;
    std::optional<UICanvasGroup> canvasGroup;
    std::optional<UIHorizontalLayout> horizontalLayout;
    std::optional<UIVerticalLayout> verticalLayout;
    std::optional<UIGridLayout> gridLayout;
    std::optional<UIWrapLayout> wrapLayout;
    std::optional<UIOverlayLayout> overlayLayout;
    std::optional<UILayoutElement> layoutElement;
    std::optional<UIContentSizeFitter> contentSizeFitter;
    std::optional<UIAspectRatioFitter> aspectRatioFitter;
    std::optional<UISprite> sprite;
    std::optional<UIImage> image;
    std::optional<UIRawImage> rawImage;
    std::optional<UIText> text;
    std::optional<UIBorder> border;
    std::optional<UIMask> mask;
    std::optional<UIShadow> shadow;
    std::optional<UIOutline> outline;
    std::optional<UIBackgroundBlur> backgroundBlur;
    std::optional<UISelectable> selectable;
    std::optional<UIButton> button;
    std::optional<UIToggle> toggle;
    std::optional<UISlider> slider;
    std::optional<UIScrollbar> scrollbar;
    std::optional<UIScrollView> scrollView;
    std::optional<UIInputField> inputField;
    std::optional<UIDropdown> dropdown;
    std::optional<UIProgressBar> progressBar;
    std::optional<UIWidgetSwitcher> widgetSwitcher;

    [[nodiscard]] bool Empty() const noexcept {
        return !rectTransform && !canvas && !canvasScaler && !canvasGroup && !horizontalLayout && !verticalLayout &&
            !gridLayout && !wrapLayout && !overlayLayout && !layoutElement && !contentSizeFitter && !aspectRatioFitter &&
            !sprite && !image && !rawImage && !text && !border && !mask && !shadow && !outline && !backgroundBlur &&
            !selectable && !button && !toggle && !slider && !scrollbar && !scrollView && !inputField && !dropdown &&
            !progressBar && !widgetSwitcher;
    }
};

} // namespace kb::scene
