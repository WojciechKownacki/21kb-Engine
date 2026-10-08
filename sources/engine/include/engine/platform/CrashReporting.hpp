#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::platform {

// Runs on the reporter's own thread once a report is on disk, while the thread
// that crashed is still parked in the handler. It may write files but must not
// wait for a lock the crashed code could be holding.
using CrashReportWrittenCallback = void (*)(const char* reason) noexcept;

struct CrashReporterOptions {
    // Empty values are read from the executable's version resource; the
    // executable's file name stands in for a missing product name.
    std::string productName;
    std::string version;
    // Empty: DefaultCrashReportDirectory() for the running executable.
    std::filesystem::path reportDirectory;
    CrashReportWrittenCallback onReportWritten = nullptr;
    // Send reports left by earlier runs, in the background, when
    // <executable directory>/CrashReports.ini names an endpoint and the user
    // has consented. Nothing is sent otherwise.
    bool uploadPendingReports = true;
};

// Process-wide crash capture: an unhandled structured exception, std::terminate,
// abort(), a pure virtual call or an invalid CRT parameter leaves a minidump with
// every thread's stack plus a small JSON description beside it, then ends the
// process. The dump is written by a thread created at installation, so neither
// a corrupted heap nor an exhausted stack on the crashing thread stops it.
class CrashReporter final {
public:
    CrashReporter() = delete;

    // Once per process; a second call changes nothing and returns false.
    static bool Install(const CrashReporterOptions& options = {});
    [[nodiscard]] static bool IsInstalled() noexcept;
    [[nodiscard]] static std::filesystem::path ReportDirectory();
    // Copies the line into a fixed ring the next report carries as its recent
    // log. Never allocates and never blocks; callable from any thread.
    static void Note(std::string_view line) noexcept;
};

// %LOCALAPPDATA%\21kb\CrashReports\<executable name without extension>.
[[nodiscard]] std::filesystem::path DefaultCrashReportDirectory(std::wstring_view executableStem);

struct CrashReportFiles {
    std::filesystem::path minidump;
    std::filesystem::path metadata;
};

// Complete reports only: a dump whose description was never written is still
// being written, or was cut short, and is neither listed nor sent.
[[nodiscard]] std::vector<CrashReportFiles> ListCrashReports(const std::filesystem::path& directory);
// Removes every report file in the directory, finished or not, and returns how
// many files went. The consent choice is kept.
std::size_t DeleteCrashReports(const std::filesystem::path& directory);

// Upload consent is off until the user turns it on and is remembered per user
// in the report directory.
[[nodiscard]] bool HasCrashUploadConsent(const std::filesystem::path& directory);
[[nodiscard]] bool SetCrashUploadConsent(const std::filesystem::path& directory, bool consent);

// HTTPS only, except plain HTTP to 127.0.0.1 or ::1.
[[nodiscard]] bool IsAllowedCrashUploadEndpoint(std::string_view url) noexcept;
// [CrashReports] UploadUrl from the given ini file; empty when absent or not allowed.
[[nodiscard]] std::string ReadCrashUploadEndpoint(const std::filesystem::path& configFile);
inline constexpr std::wstring_view kCrashUploadConfigFile = L"CrashReports.ini";

struct CrashUploadSummary {
    bool attempted = false;
    std::size_t uploaded = 0U;
    std::size_t failed = 0U;
    std::string error;
};

// Posts each complete report as multipart/form-data (upload_file_minidump plus
// prod, ver, build_id and the JSON description) and deletes the ones the server
// accepted with a 2xx status. Does nothing without consent or with an endpoint
// that is not allowed.
[[nodiscard]] CrashUploadSummary UploadPendingCrashReports(
    const std::filesystem::path& directory, std::string_view endpoint);

} // namespace kb::platform
