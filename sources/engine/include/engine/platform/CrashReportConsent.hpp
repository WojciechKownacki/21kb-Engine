#pragma once

#include <cstdint>
#include <string_view>

namespace kb::platform {

class UserStorage;

// The player's answer to "send crash reports?", kept in the game's user storage with the rest of the
// player's settings. Undecided until the player answers; nothing is ever sent unless it is Granted.
enum class CrashUploadConsentChoice : std::uint8_t {
    Undecided,
    Granted,
    Declined,
};

// The user storage key the choice lives under. Its extension keeps it out of the save and settings slot
// listings scripts see.
inline constexpr std::string_view kCrashUploadConsentStorageKey = "crash-report-upload.consent";

// Undecided when nothing was stored or the stored value is not one this code writes.
[[nodiscard]] CrashUploadConsentChoice ReadCrashUploadConsentChoice(const UserStorage& storage);
[[nodiscard]] bool WriteCrashUploadConsentChoice(UserStorage& storage, bool granted);
// Forgets the answer, so the next launch asks again.
[[nodiscard]] bool ClearCrashUploadConsentChoice(UserStorage& storage);

} // namespace kb::platform
