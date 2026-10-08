#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

// What the package signing brokers share: path checks that hold against a job
// directory being swapped underneath them, and a signer process that receives its
// secrets on standard input only.
namespace kb::editor::signing_process {

[[nodiscard]] std::filesystem::path Canonical(const std::filesystem::path& path) noexcept;
[[nodiscard]] bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) noexcept;
[[nodiscard]] bool DirectChildOf(const std::filesystem::path& root, const std::filesystem::path& child) noexcept;
// 32 lower-case hexadecimal digits, as the package script writes them.
[[nodiscard]] bool ValidSession(std::string_view value) noexcept;
void SecureClear(std::string& value) noexcept;

#if defined(_WIN32)
class ScopedHandle final {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE value) noexcept : value_(value) {}
    ~ScopedHandle() { if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HANDLE Get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_ = nullptr;
};

// Opens `path` without following a reparse point and keeps it from being
// replaced while the handle lives; an empty handle when it is a link or the
// wrong kind of entry.
[[nodiscard]] ScopedHandle Guard(const std::filesystem::path& path, bool directory, DWORD access, DWORD share) noexcept;
[[nodiscard]] std::wstring Quote(std::wstring_view argument);

struct SignerRun {
    bool started = false;
    bool timedOut = false;
    bool secretsWritten = false;
    DWORD exitCode = 1U;
    std::string output;
    std::string error;
};

// Starts `executable` suspended in `workingDirectory` with a sanitized environment,
// joins it to `processJob`, writes each secret followed by a newline to its standard
// input, clears the secrets and collects at most `maximumOutputBytes` of output.
[[nodiscard]] SignerRun RunSigner(
    const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments,
    const std::filesystem::path& workingDirectory,
    std::vector<std::string*> secrets,
    void* processJob,
    std::chrono::seconds timeout,
    std::size_t maximumOutputBytes);
#endif

} // namespace kb::editor::signing_process
