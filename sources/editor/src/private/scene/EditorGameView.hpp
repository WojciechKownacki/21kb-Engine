#pragma once

#include "app/EditorPlayPreferences.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kb::editor {

struct EditorGameViewSize {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;

    // A size of zero follows the panel instead of fixing the picture.
    [[nodiscard]] bool FollowsPanel() const noexcept { return width == 0U || height == 0U; }
    [[nodiscard]] bool operator==(const EditorGameViewSize&) const noexcept = default;
};

// The Game View's resolution list: what each choice renders at, what it is called in the settings file and
// in the list, and how large the picture appears inside the panel.
class EditorGameView final {
public:
    EditorGameView() = delete;

    static constexpr std::array<EditorGameViewResolution, 7U> kResolutions{
        EditorGameViewResolution::FreeAspect,
        EditorGameViewResolution::ProjectResolution,
        EditorGameViewResolution::Landscape16x9,
        EditorGameViewResolution::Portrait9x16,
        EditorGameViewResolution::PhoneLandscape,
        EditorGameViewResolution::Tablet4x3,
        EditorGameViewResolution::Tablet16x10,
    };

    // The render size of a choice; the project's window size stands in for Project Resolution.
    [[nodiscard]] static EditorGameViewSize Size(EditorGameViewResolution resolution, EditorGameViewSize project) noexcept;
    // Stable text for the editor settings file.
    [[nodiscard]] static std::string_view Name(EditorGameViewResolution resolution) noexcept;
    [[nodiscard]] static std::optional<EditorGameViewResolution> FromName(std::string_view name) noexcept;
    // The entry as the list shows it, in the editor language, with the size where it is fixed.
    [[nodiscard]] static std::string Label(EditorGameViewResolution resolution, EditorGameViewSize project);
    // How large the picture appears next to the pixels it renders: "0.62x".
    [[nodiscard]] static std::string FormatScale(std::uint32_t shownWidth, std::uint32_t renderWidth);
};

} // namespace kb::editor
