#pragma once

#include "engine/core/JsonValue.hpp"

#include <string_view>

namespace kb::editor {

struct EditorApplicationState;

// What an agent asks of the running editor through `kb_cli editor`: start or stop the game, take a picture of the view, measure
// the frame times. They work while the game runs; the answer is a file under Saved/Agent/EditorReply, which the command waits for.
[[nodiscard]] bool IsAgentControlAction(std::string_view action) noexcept;
void StartAgentControl(EditorApplicationState& state, const kb::core::JsonValue& request);
// Once per editor frame: finishes a capture that landed or timed out, and feeds a running measurement.
void PumpAgentControl(EditorApplicationState& state, double frameMilliseconds);

} // namespace kb::editor
