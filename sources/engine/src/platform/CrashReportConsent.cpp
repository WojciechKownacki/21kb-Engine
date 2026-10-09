#include "engine/platform/CrashReportConsent.hpp"

#include "engine/platform/UserStorage.hpp"

#include <optional>
#include <string>

namespace kb::platform {
namespace {

constexpr std::string_view kGranted = "granted\n";
constexpr std::string_view kDeclined = "declined\n";

} // namespace

CrashUploadConsentChoice ReadCrashUploadConsentChoice(const UserStorage& storage) {
    const std::optional<std::string> stored = storage.Read(kCrashUploadConsentStorageKey);
    if (!stored.has_value()) return CrashUploadConsentChoice::Undecided;
    if (*stored == kGranted) return CrashUploadConsentChoice::Granted;
    if (*stored == kDeclined) return CrashUploadConsentChoice::Declined;
    return CrashUploadConsentChoice::Undecided;
}

bool WriteCrashUploadConsentChoice(UserStorage& storage, bool granted) {
    return storage.Write(kCrashUploadConsentStorageKey, granted ? kGranted : kDeclined);
}

bool ClearCrashUploadConsentChoice(UserStorage& storage) {
    return !storage.Read(kCrashUploadConsentStorageKey).has_value() || storage.Delete(kCrashUploadConsentStorageKey);
}

} // namespace kb::platform
