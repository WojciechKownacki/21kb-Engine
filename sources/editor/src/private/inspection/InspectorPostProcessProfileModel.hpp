#pragma once

#include "inspection/InspectorPanelState.hpp"
#include "kb/render/post/ScenePostProcessSettings.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace kb::editor {

enum class InspectorPostProcessProfileRowKind : std::uint8_t {
    Bool,
    Number,
    Choice,
};

// One row of the post-process profile Inspector. `field` is the profile file's own field name
// (PostProcessProfileAssetLoader::SetField), so the Inspector, the file and kb_cli
// post-process-set take exactly the same values; the anti-aliasing choice writes two fields.
struct InspectorPostProcessProfileRow {
    InspectorSectionId section = InspectorSectionId::None;
    InspectorPostProcessProfileRowKind kind = InspectorPostProcessProfileRowKind::Number;
    std::string_view field;
};

struct InspectorPostProcessProfileSection {
    InspectorSectionId section = InspectorSectionId::None;
};

class InspectorPostProcessProfileModel {
public:
    InspectorPostProcessProfileModel() = delete;

    [[nodiscard]] static std::span<const InspectorPostProcessProfileSection> Sections() noexcept;
    [[nodiscard]] static std::span<const InspectorPostProcessProfileRow> Rows() noexcept;
    [[nodiscard]] static bool IsSection(InspectorSectionId section) noexcept;
    [[nodiscard]] static int RowCount(InspectorSectionId section) noexcept;
    // Names in the editor language.
    [[nodiscard]] static std::string_view SectionTitle(InspectorSectionId section);
    [[nodiscard]] static std::string_view RowLabel(int row);

    // The row's value as the Inspector shows it (a number, a choice name), and a Bool row's state.
    [[nodiscard]] static std::string ValueText(const kb::render::ScenePostProcessSettings& settings, int row);
    [[nodiscard]] static bool BoolValue(const kb::render::ScenePostProcessSettings& settings, int row);
    // The settings after a click on a Bool (toggle) or Choice (next choice) row; nullopt for a
    // Number row or a row out of range.
    [[nodiscard]] static std::optional<kb::render::ScenePostProcessSettings> Clicked(
        const kb::render::ScenePostProcessSettings& settings, int row);
    // The settings after typing `text` into a Number row; nullopt when the field refuses it.
    [[nodiscard]] static std::optional<kb::render::ScenePostProcessSettings> Edited(
        const kb::render::ScenePostProcessSettings& settings, int row, std::string_view text);
    // The whole Inspector as text, for the headless editor and its self-tests.
    [[nodiscard]] static std::string Text(const kb::render::ScenePostProcessSettings& settings);
};

} // namespace kb::editor
