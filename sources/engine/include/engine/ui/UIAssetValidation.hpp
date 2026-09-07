#pragma once

#include "engine/scene/UIAssets.hpp"

#include <cstddef>

namespace kb::scene {

class UIAssetValidation final {
  public:
    UIAssetValidation() = delete;

    [[nodiscard]] static bool RectTransform(const UIRectTransform& value) noexcept;
    [[nodiscard]] static bool Canvas(const UICanvas& value) noexcept;
    [[nodiscard]] static bool ContainerLayout(const UIContainerLayout& value) noexcept;
    [[nodiscard]] static bool Paint(const UIPaint& value) noexcept;
    [[nodiscard]] static bool Image(const UIImage& value) noexcept;
    [[nodiscard]] static bool Text(const UIText& value) noexcept;
    [[nodiscard]] static bool Interaction(const UIInteraction& value) noexcept;
    [[nodiscard]] static bool Effects(const UIEffects& value) noexcept;
    [[nodiscard]] static bool Control(const UIControlState& value) noexcept;
    [[nodiscard]] static bool ElementComposition(const UIDocumentElement& value, bool isRoot,
                                                 std::size_t directChildCount) noexcept;
};

} // namespace kb::scene
