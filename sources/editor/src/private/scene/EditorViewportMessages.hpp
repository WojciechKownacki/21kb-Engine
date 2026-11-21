#pragma once

#include "console/EditorConsoleState.hpp"
#include "rendering/EditorSceneViewportTextOverlay.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

struct EditorViewportMessage {
    EditorConsoleLevel level = EditorConsoleLevel::Warning;
    std::string text;
};

// The warnings and errors a viewport shows over its picture, as plates in the top left corner.
class EditorViewportMessages {
public:
    EditorViewportMessages() = delete;

    // At most this many plates are shown, each for this long after the console received its message.
    static constexpr std::size_t kMaxShown = 3U;
    static constexpr std::uint64_t kShownMilliseconds = 8000U;
    // A plate is at most this wide in pixels and holds at most this many lines; the rest of a longer text ends in "...".
    static constexpr float kMaxWidth = 460.0F;
    static constexpr std::size_t kMaxLines = 6U;

    // The latest warnings and errors of the console that are still young at nowMs (milliseconds since the epoch,
    // as the console stamps them), oldest first.
    [[nodiscard]] static std::vector<EditorViewportMessage> Recent(const EditorConsoleState& console, std::uint64_t nowMs);
    // The text cut at spaces into lines of at most `columns` characters and at most `maxLines` lines. A word longer
    // than a line is cut where the line ends.
    [[nodiscard]] static std::vector<std::string> Wrap(std::string_view text, std::size_t columns, std::size_t maxLines);
    // Lays the messages out one under the other from the corner (x, y) of the picture, in its pixels.
    static void Append(std::vector<EditorSceneViewportTextLabel>& labels, std::vector<EditorSceneViewportOverlayShape>& shapes,
        std::span<const EditorViewportMessage> messages, float x, float y);
};

} // namespace kb::editor
