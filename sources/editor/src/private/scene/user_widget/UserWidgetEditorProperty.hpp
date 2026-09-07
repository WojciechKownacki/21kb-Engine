#pragma once

#include "engine/scene/UIAssets.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

enum class UserWidgetEditorProperty {
    Name,
    Visible,
    RectAnchors,
    RectOffsets,
    RectPivot,
    RectScale,
    RectRotation,
    RectZOrder,
    Canvas,
    Layout,
    Paint,
    PaintBackgroundColor,
    PaintBorderColor,
    Image,
    Text,
    TextStyle,
    TextColor,
    Interaction,
    InteractionAction,
    Effects,
    EffectsShadowColor,
    EffectsOutlineColor,
    ControlState,
    ListItems,
};

struct UserWidgetEditorPropertyRow {
    UserWidgetEditorProperty property = UserWidgetEditorProperty::Name;
    std::string label;
    std::string value;
    std::string hint;
};

// Formats and parses the canonical fields of one UIDocumentElement. Rows are
// transient editor views; Apply mutates the supplied canonical element copy,
// which UserWidgetEditorDocument then validates and commits as one undo step.
class UserWidgetEditorPropertyAdapter final {
  public:
    UserWidgetEditorPropertyAdapter() = delete;

    [[nodiscard]] static std::vector<UserWidgetEditorPropertyRow> Rows(const kb::scene::UIDocumentElement& element);
    [[nodiscard]] static bool Apply(UserWidgetEditorProperty property, std::string_view value,
                                    kb::scene::UIDocumentElement& element);
    [[nodiscard]] static std::optional<std::array<float, 4U>>
    Color(UserWidgetEditorProperty property, const kb::scene::UIDocumentElement& element) noexcept;
    [[nodiscard]] static bool ApplyColor(UserWidgetEditorProperty property, const std::array<float, 4U>& color,
                                         kb::scene::UIDocumentElement& element) noexcept;
};

} // namespace kb::editor
