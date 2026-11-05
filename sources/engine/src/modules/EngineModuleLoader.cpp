#include "engine/modules/EngineModuleLoader.hpp"

#include "ecs/component/ComponentTypeCache.hpp"
#include "engine/platform/FileSystemPath.hpp"
#include "engine/security/Crypto.hpp"
#include "engine/security/ReleaseManifest.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <mutex>
#include <ranges>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace kb::modules {
namespace {

[[nodiscard]] std::string SanitizeFileName(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
        const bool valid = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' || character == '-';
        output.push_back(valid ? character : '_');
    }
    return output.empty() ? "engine_module" : output;
}

[[nodiscard]] unsigned long long CurrentProcessId() {
#if defined(_WIN32)
    return static_cast<unsigned long long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long long>(getpid());
#endif
}

[[nodiscard]] bool ProcessAlive(unsigned long long processId) {
#if defined(_WIN32)
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(processId));
    if (process == nullptr) {
        // No such process; any other refusal means it exists.
        return GetLastError() != ERROR_INVALID_PARAMETER;
    }
    const bool running = WaitForSingleObject(process, 0U) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
#else
    return kill(static_cast<pid_t>(processId), 0) == 0 || errno == EPERM;
#endif
}

// Whether another running process made the shadow copy `name` (<key>_p<pid>_<serial><extension>). Its copy is
// unlocked from when it is written until that process maps it, so deleting it then makes that load fail.
[[nodiscard]] bool OtherLiveProcessOwns(std::string_view name, std::string_view keyPrefix) {
    std::string_view rest = name.substr(keyPrefix.size());
    if (!rest.starts_with('p')) {
        return false;
    }
    rest.remove_prefix(1U);
    unsigned long long processId = 0U;
    std::size_t digits = 0U;
    while (digits < rest.size() && rest[digits] >= '0' && rest[digits] <= '9') {
        processId = processId * 10U + static_cast<unsigned long long>(rest[digits] - '0');
        ++digits;
    }
    return digits > 0U && processId != CurrentProcessId() && ProcessAlive(processId);
}

// A module file's identity: its size and last write time. A shadow copy is named after the file it copies, so a
// later process that loads the same file maps the copy that is already there instead of copying it again, and a
// rebuilt file (another size or time) gets a copy of its own.
struct ModuleStamp {
    std::uintmax_t size = 0U;
    std::filesystem::file_time_type time{};
};

[[nodiscard]] bool ReadModuleStamp(const std::filesystem::path& file, ModuleStamp& stamp) {
    std::error_code error;
    stamp.size = std::filesystem::file_size(file, error);
    if (error) {
        return false;
    }
    stamp.time = std::filesystem::last_write_time(file, error);
    return !error;
}

[[nodiscard]] std::string StampText(const ModuleStamp& stamp) {
    std::uint64_t hash = 14695981039346656037ULL;
    const std::array<std::uint64_t, 2U> parts{
        static_cast<std::uint64_t>(stamp.size), static_cast<std::uint64_t>(stamp.time.time_since_epoch().count()) };
    for (const std::uint64_t part : parts) {
        for (unsigned shift = 0U; shift < 64U; shift += 8U) {
            hash ^= (part >> shift) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    }
    std::array<char, 16U> text{};
    const char* const end = std::to_chars(text.data(), text.data() + text.size(), hash, 16).ptr;
    return std::string(text.data(), static_cast<std::size_t>(end - text.data()));
}

// Whether `copy` is a finished copy of the file `stamp` describes: a copy is given the write time of its source.
[[nodiscard]] bool IsCopyOf(const std::filesystem::path& copy, const ModuleStamp& stamp) {
    std::error_code error;
    return std::filesystem::is_regular_file(copy, error) && !error &&
           std::filesystem::file_size(copy, error) == stamp.size && !error &&
           std::filesystem::last_write_time(copy, error) == stamp.time && !error;
}

// How many copies of one version of a module (one per load in a process) stay for later processes to map.
constexpr std::uint64_t kKeptShadowSlots = 8U;
constexpr std::string_view kPartialCopyMarker = ".partial";

// Whether `rest` (a file name after the module's key) is `<stamp>_<slot>...`, and that slot.
[[nodiscard]] bool ParseStampedName(std::string_view rest, std::uint64_t& slot) {
    const std::size_t digits = rest.find_first_not_of("0123456789abcdef");
    if (digits == 0U || digits == std::string_view::npos || digits > 16U || rest[digits] != '_') {
        return false;
    }
    const std::string_view slotText = rest.substr(digits + 1U);
    const auto [end, code] = std::from_chars(slotText.data(), slotText.data() + slotText.size(), slot);
    return code == std::errc{} && end != slotText.data();
}

// Best-effort removal of shadow copies of this key that no later load can use: copies of other versions of the
// module (and older engines' copies, which were named after their process), unfinished copies, and copies beyond
// the slots kept. The copies of `current` (the name every copy of this version starts with) stay for the next
// process to map. A file still mapped by a live process stays locked and fails to delete (skipped); another live
// process's old-style copy is left alone even while unlocked, as that process may be about to load it.
void PruneStaleShadowCopies(const std::filesystem::path& directory, std::string_view keyPrefix, std::string_view current) {
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            return;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (!std::string_view{ name }.starts_with(keyPrefix)) {
            continue;
        }
        const std::string_view rest = std::string_view{ name }.substr(keyPrefix.size());
        std::uint64_t slot = 0U;
        const bool stamped = ParseStampedName(rest, slot);
        const bool oldStyle = rest.starts_with('p');
        if (!stamped && !oldStyle) {
            continue; // another module whose key starts with this one
        }
        if (stamped && std::string_view{ name }.starts_with(current) && slot >= 1U && slot <= kKeptShadowSlots &&
            name.find(kPartialCopyMarker) == std::string::npos) {
            continue;
        }
        if (oldStyle && OtherLiveProcessOwns(name, keyPrefix)) {
            continue;
        }
        std::error_code removeError;
        std::filesystem::remove(entry.path(), removeError);
    }
}

// Copies `source` to `destination`, a copy `IsCopyOf` accepts. The copy is written under another name and renamed
// when it is finished, so another process that finds `destination` finds a whole copy.
[[nodiscard]] bool CopyForLoad(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const ModuleStamp& stamp,
    std::string_view diagnosticLabel,
    std::vector<std::string>& errors) {
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
        errors.push_back(std::string{ diagnosticLabel } + " shadow copy directory could not be created");
        return false;
    }
    const std::filesystem::path partial = destination.string() + std::string{ kPartialCopyMarker } +
        std::to_string(CurrentProcessId());
    // The source DLL can be transiently locked with a sharing violation by an
    // antivirus real-time scan, the search indexer, or a build tool that just
    // wrote it — an inherent Windows condition, not a permanent failure. Retry
    // with a short backoff (the standard shadow-copy behavior for any host
    // that copies plugins aside), and only report the failure if it persists.
    // Ride out only a brief transient lock (e.g. an antivirus glancing at a
    // freshly written DLL). Kept short on purpose: this runs on the caller's
    // thread, so a long spin would freeze the editor UI, and a genuinely
    // persistent lock is a real error to surface — not something to hang on.
    // The common case (unlocked source) succeeds on the very first attempt.
    constexpr int kMaxAttempts = 6;
    constexpr std::chrono::milliseconds kRetryDelay{ 100 };
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        error.clear();
        std::filesystem::copy_file(source, partial, std::filesystem::copy_options::overwrite_existing, error);
        if (!error) {
            break;
        }
        if (attempt + 1 < kMaxAttempts) {
            std::this_thread::sleep_for(kRetryDelay);
        }
    }
    if (error) {
        errors.push_back(std::string{ diagnosticLabel } + " shadow copy failed after retries (" + error.message() + "): " +
                         source.string() + " -> " + destination.string());
        return false;
    }
    std::filesystem::last_write_time(partial, stamp.time, error);
    if (!error) {
        std::filesystem::rename(partial, destination, error);
    }
    if (error) {
        // Another process put the same copy there first and has it mapped, so it cannot be replaced; it is what
        // is wanted when it is a copy of this file.
        const std::string reason = error.message();
        std::error_code removeError;
        std::filesystem::remove(partial, removeError);
        if (!IsCopyOf(destination, stamp)) {
            errors.push_back(std::string{ diagnosticLabel } + " shadow copy could not be put in place (" + reason + "): " +
                             source.string() + " -> " + destination.string());
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string NativeLibraryError(std::string_view diagnosticLabel) {
#if defined(_WIN32)
    return std::string{ diagnosticLabel } + " loader error " + std::to_string(static_cast<unsigned long>(GetLastError()));
#else
    const char* error = dlerror();
    return error == nullptr ? std::string{ diagnosticLabel } + " loader error" : std::string{ error };
#endif
}

void CloseNativeLibrary(void* library) noexcept {
    if (library == nullptr) {
        return;
    }
    kb::ecs::ComponentTypeCache::InvalidateTypeInfoEntries();
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

[[nodiscard]] bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_regular_file(path, error) && !error;
}

// An open, write-locked handle on the module file being verified. Windows: no share-write or
// share-delete, so the file cannot change, be renamed or be replaced until it is closed, while
// LoadLibrary (which only reads) still opens it. Elsewhere: a descriptor the hash is read from.
class ModuleFileHold final {
public:
    ModuleFileHold() = default;
    ModuleFileHold(const ModuleFileHold&) = delete;
    ModuleFileHold& operator=(const ModuleFileHold&) = delete;
    ~ModuleFileHold() {
        Close();
    }

    [[nodiscard]] bool Open(const std::filesystem::path& path) {
#if defined(_WIN32)
        handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        return handle_ != INVALID_HANDLE_VALUE;
#else
        descriptor_ = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        return descriptor_ >= 0;
#endif
    }

    // Streams the held file through SHA-512.
    [[nodiscard]] bool Hash(kb::security::Sha512Digest& digest, std::uint64_t& size) {
        kb::security::Sha512Hasher hasher;
        std::vector<std::uint8_t> buffer(1U << 20U);
        size = 0U;
        for (;;) {
#if defined(_WIN32)
            DWORD read = 0U;
            if (ReadFile(handle_, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) == FALSE) {
                return false;
            }
#else
            const ssize_t read = ::read(descriptor_, buffer.data(), buffer.size());
            if (read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
#endif
            if (read == 0) {
                break;
            }
            hasher.Update(std::span{ buffer.data(), static_cast<std::size_t>(read) });
            size += static_cast<std::uint64_t>(read);
        }
        digest = hasher.Finish();
        return true;
    }

    void Close() noexcept {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (descriptor_ >= 0) {
            close(descriptor_);
            descriptor_ = -1;
        }
#endif
    }

private:
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

// Checks the bytes at `loadPath` -- a shadow copy of `sourcePath`, or the file itself -- against
// the verified release. `hold` keeps them locked for the caller to load.
[[nodiscard]] bool VerifyModuleAgainstRelease(
    const kb::security::InstalledRelease* release,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& loadPath,
    std::string_view diagnosticLabel,
    ModuleFileHold& hold,
    EngineModuleLoadResult& result) {
    const std::string label{ diagnosticLabel };
    if (release == nullptr) {
        result.warnings.push_back(label + " '" + sourcePath.string() +
            "' is not verified: this process runs no signed release (development build)");
        return true;
    }
    std::error_code error;
    const std::filesystem::path root = std::filesystem::weakly_canonical(release->root, error);
    const std::filesystem::path source = std::filesystem::weakly_canonical(sourcePath, error);
    const std::filesystem::path relative = source.lexically_relative(root);
    const std::u8string portable = relative.generic_u8string();
    const kb::security::ReleaseManifestFile* listed = error || relative.empty() || *relative.begin() == ".."
        ? nullptr
        : release->manifest.FindFile(std::string_view{ reinterpret_cast<const char*>(portable.data()), portable.size() });
    if (listed == nullptr || !kb::security::IsCriticalReleaseFile(relative)) {
        result.errors.push_back(label + " '" + sourcePath.string() + "' is not listed in the signed release manifest");
        return false;
    }
    kb::security::Sha512Digest digest{};
    std::uint64_t size = 0U;
    if (!hold.Open(loadPath) || !hold.Hash(digest, size)) {
        result.errors.push_back(label + " '" + sourcePath.string() + "' could not be locked and read for verification");
        return false;
    }
    if (size != listed->size || !kb::security::ConstantTimeEqual(digest, listed->sha512)) {
        result.errors.push_back(label + " '" + sourcePath.string() + "' does not match the signed release manifest");
        return false;
    }
    return true;
}

void AppendCandidate(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& candidate) {
    if (candidate.empty()) {
        return;
    }
    if (std::ranges::find(candidates, candidate) == candidates.end()) {
        candidates.push_back(candidate);
    }
}

} // namespace

struct EngineModuleLibraryLifetime {
    void* handle = nullptr;
    bool unload = true;
    ~EngineModuleLibraryLifetime() { if (unload) CloseNativeLibrary(handle); }
};

EngineModuleLibrary::EngineModuleLibrary(std::filesystem::path originalPath, std::filesystem::path loadedPath, void* handle) noexcept
    : originalPath_(std::move(originalPath))
    , loadedPath_(std::move(loadedPath))
    , handle_(handle) {}

EngineModuleLibrary::~EngineModuleLibrary() {
    Reset();
}

EngineModuleLibrary::EngineModuleLibrary(EngineModuleLibrary&& other) noexcept
    : originalPath_(std::move(other.originalPath_))
    , loadedPath_(std::move(other.loadedPath_))
    , handle_(std::exchange(other.handle_, nullptr))
    , lifetime_(std::move(other.lifetime_)) {}

EngineModuleLibrary& EngineModuleLibrary::operator=(EngineModuleLibrary&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    Reset();
    originalPath_ = std::move(other.originalPath_);
    loadedPath_ = std::move(other.loadedPath_);
    handle_ = std::exchange(other.handle_, nullptr);
    lifetime_ = std::move(other.lifetime_);
    return *this;
}

void* EngineModuleLibrary::FindSymbol(std::string_view name, std::vector<std::string>& errors, std::string_view diagnosticLabel) const {
    if (handle_ == nullptr || name.empty()) {
        errors.push_back(std::string{ diagnosticLabel } + " symbol lookup request is invalid");
        return nullptr;
    }
    const std::string symbolName{ name };
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle_), symbolName.c_str());
    if (symbol == nullptr) {
        errors.push_back(NativeLibraryError(diagnosticLabel));
    }
    return reinterpret_cast<void*>(symbol);
#else
    void* symbol = dlsym(handle_, symbolName.c_str());
    if (symbol == nullptr) {
        errors.push_back(NativeLibraryError(diagnosticLabel));
    }
    return symbol;
#endif
}

void EngineModuleLibrary::Reset() noexcept {
    if (lifetime_) lifetime_.reset();
    else CloseNativeLibrary(handle_);
    handle_ = nullptr;
    originalPath_.clear();
    loadedPath_.clear();
}

void EngineModuleLibrary::ReleaseWithoutUnload() noexcept {
    if (lifetime_) { lifetime_->unload = false; lifetime_.reset(); }
    handle_ = nullptr;
    originalPath_.clear();
    loadedPath_.clear();
}

std::shared_ptr<void> EngineModuleLibrary::Retain() {
    if (!handle_) return {};
    if (!lifetime_) {
        lifetime_ = std::make_shared<EngineModuleLibraryLifetime>();
        lifetime_->handle = handle_;
    }
    return lifetime_;
}

EngineModuleLoadResult EngineModuleLoader::Load(EngineModuleLoadDesc desc) {
    EngineModuleLoadResult result{};
    desc.key = NormalizeKey(std::move(desc.key), desc.modulePath);
    if (desc.modulePath.empty()) {
        result.errors.push_back(desc.diagnosticLabel + " path is empty");
        return result;
    }

    const std::filesystem::path sourcePath = ResolveModulePath(desc.modulePath);
    if (!IsRegularFile(sourcePath)) {
        result.errors.push_back(desc.diagnosticLabel + " file is missing: " + desc.modulePath.string());
        return result;
    }

    std::uint64_t serial = 0U;
    std::filesystem::path loadPath = sourcePath;
    if (desc.shadowCopy) {
        ModuleStamp stamp;
        if (!ReadModuleStamp(sourcePath, stamp)) {
            result.errors.push_back(desc.diagnosticLabel + " file could not be inspected: " + sourcePath.string());
            return result;
        }
        const std::string keyPrefix = SanitizeFileName(desc.key) + "_";
        const std::string current = keyPrefix + StampText(stamp) + "_";
        std::uint64_t slot = 0U;
        loadPath = ResolveShadowCopyPath(
            current, sourcePath, desc.shadowCopyDirectory, desc.shadowCopyDirectoryName, serial, slot);
        std::error_code shadowDirError;
        std::filesystem::create_directories(loadPath.parent_path(), shadowDirError);
        PruneStaleShadowCopies(loadPath.parent_path(), keyPrefix, current);
        if (!IsCopyOf(loadPath, stamp) && !CopyForLoad(sourcePath, loadPath, stamp, desc.diagnosticLabel, result.errors)) {
            return result;
        }
    }

    // Hashed and loaded under one lock: what is verified is what gets mapped.
    ModuleFileHold hold;
    if (!VerifyModuleAgainstRelease(kb::security::CurrentVerifiedRelease().get(), sourcePath, loadPath,
            desc.diagnosticLabel, hold, result)) {
        return result;
    }
    void* library = LoadNativeLibrary(loadPath, desc.diagnosticLabel, result.errors);
    hold.Close();
    if (library == nullptr) {
        return result;
    }

    result.loaded = true;
    result.originalPath = sourcePath;
    result.loadedPath = loadPath;
    result.reloadSerial = serial;
    result.library = EngineModuleLibrary{ sourcePath, loadPath, library };
    return result;
}

std::string EngineModuleLoader::NormalizeKey(std::string key, const std::filesystem::path& modulePath) {
    if (!key.empty()) {
        return key;
    }
    return std::filesystem::absolute(modulePath).string();
}

std::filesystem::path EngineModuleLoader::ResolveModulePath(const std::filesystem::path& modulePath) {
    if (modulePath.empty()) {
        return {};
    }

    std::vector<std::filesystem::path> candidates;
    if (modulePath.is_absolute()) {
        AppendCandidate(candidates, modulePath);
    } else {
        std::error_code error;
        AppendCandidate(candidates, std::filesystem::absolute(modulePath, error));

        const std::filesystem::path exePath = kb::platform::RunningExecutablePath();
        const std::filesystem::path exeDir = exePath.parent_path();
        AppendCandidate(candidates, exeDir / modulePath);
        AppendCandidate(candidates, exeDir.parent_path() / modulePath);
        AppendCandidate(candidates, exeDir.parent_path().parent_path() / modulePath);

        if (modulePath.filename() == modulePath) {
            const std::filesystem::path exeParent = exeDir.parent_path();
            if (!exeParent.empty()) {
                std::error_code iterError;
                for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(exeParent, iterError)) {
                    if (iterError || !entry.is_directory()) {
                        continue;
                    }
                    AppendCandidate(candidates, entry.path() / modulePath.filename());
                }
            }

            const std::filesystem::path buildRoot = exeDir.parent_path().parent_path();
            const std::filesystem::path configDir = exeDir.filename();
            if (!buildRoot.empty() && !configDir.empty()) {
                std::error_code iterError;
                for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(buildRoot, iterError)) {
                    if (iterError || !entry.is_directory()) {
                        continue;
                    }
                    AppendCandidate(candidates, entry.path() / configDir / modulePath.filename());
                }
            }
        }
    }

    for (const std::filesystem::path& candidate : candidates) {
        if (IsRegularFile(candidate)) {
            std::error_code error;
            const std::filesystem::path absolute = std::filesystem::absolute(candidate, error);
            return error ? candidate : absolute;
        }
    }

    return modulePath.is_absolute() ? modulePath : std::filesystem::absolute(modulePath);
}

std::filesystem::path EngineModuleLoader::ResolveShadowCopyPath(
    std::string_view namePrefix,
    const std::filesystem::path& modulePath,
    const std::filesystem::path& shadowCopyDirectory,
    std::string_view shadowCopyDirectoryName,
    std::uint64_t& serial,
    std::uint64_t& slot) {
    std::filesystem::path directory = shadowCopyDirectory;
    if (directory.empty()) {
        directory = std::filesystem::temp_directory_path() / std::string{ shadowCopyDirectoryName.empty() ? "21kb_engine_modules" : shadowCopyDirectoryName };
    }
    // Serial from a PROCESS-GLOBAL counter, not the per-loader member: every
    // EngineModuleHost creates its own loader whose member would reset to the
    // same value, so two loaders alive at once in one process (an editor scene
    // reload creating a new host while the old host's plugin DLL is still mapped,
    // or two hosts sharing a scene) would load the same copy twice.
    static std::atomic<std::uint64_t> processShadowSerial{ 1U };
    serial = processShadowSerial.fetch_add(1U, std::memory_order_relaxed);
    // The copy's name carries the load's number among the loads of this version of the module in this process, so
    // no load in the process maps a copy another load of it has mapped. The numbers repeat from one process to the
    // next, which is what lets a later process find the copies this one made and map them without copying again.
    static std::mutex slotsMutex;
    static std::unordered_map<std::string, std::uint64_t> loadsOfVersion;
    {
        const std::lock_guard lock{ slotsMutex };
        slot = ++loadsOfVersion[(directory / std::string{ namePrefix }).string()];
    }
    return directory / (std::string{ namePrefix } + std::to_string(slot) + modulePath.extension().string());
}

void* EngineModuleLoader::LoadNativeLibrary(
    const std::filesystem::path& path,
    std::string_view diagnosticLabel,
    std::vector<std::string>& errors) {
#if defined(_WIN32)
    HMODULE library = LoadLibraryW(path.wstring().c_str());
    if (library == nullptr) {
        errors.push_back(NativeLibraryError(diagnosticLabel));
    }
    return reinterpret_cast<void*>(library);
#else
    void* library = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        errors.push_back(NativeLibraryError(diagnosticLabel));
    }
    return library;
#endif
}

} // namespace kb::modules
