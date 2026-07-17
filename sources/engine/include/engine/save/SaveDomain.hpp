#pragma once

#include <cstdint>
#include <string_view>

namespace kb::save {

// LIB-163: which category of persisted data a kb::save file holds. Stamped
// into the file header so the four kinds of game persistence stay SEPARATED
// — loading a file asks for the domain it expects and a mismatch is rejected
// (WrongDomain), so a user-settings file can never be mistaken for a save
// game or vice versa.
//
// Only the two kinds that share the kb::save scalar key/value format are
// enumerated here. The other two persistence concerns the task names are
// already separated by construction and are NOT kb::save domains:
//   - scene state -> kb::scene::SceneDocumentService, an entirely separate
//     subsystem with its own binary format and magic ("21KBSCN"); a scene is
//     structured entity/component data, not a flat key/value table.
//   - network data -> the multiplayer subsystem (backlog section 18), not yet
//     built; it is deliberately not given a kb::save domain here rather than
//     reserving an orphan enumerator with no store behind it.
enum class SaveDomain : std::uint8_t {
    SaveGame = 0,     // player progress
    UserSettings = 1, // user preferences (audio, video, controls, ...)
};

[[nodiscard]] std::string_view ToString(SaveDomain domain) noexcept;

} // namespace kb::save
