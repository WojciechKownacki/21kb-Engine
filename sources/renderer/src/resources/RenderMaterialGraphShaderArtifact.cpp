#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/SceneGBufferContract.hpp"

#include "engine/platform/FileSystemPath.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <system_error>
#include <unordered_set>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace kb::render {
namespace {

[[nodiscard]] constexpr SceneGBufferShadingModelId GBufferShadingModelId(RenderMaterialShadingModel model) noexcept {
    switch (model) {
    case RenderMaterialShadingModel::Unlit: return SceneGBufferShadingModelId::Unlit;
    case RenderMaterialShadingModel::DefaultLit: return SceneGBufferShadingModelId::DefaultLit;
    case RenderMaterialShadingModel::Subsurface: return SceneGBufferShadingModelId::Subsurface;
    case RenderMaterialShadingModel::ClearCoat: return SceneGBufferShadingModelId::ClearCoat;
    case RenderMaterialShadingModel::Cloth: return SceneGBufferShadingModelId::Cloth;
    case RenderMaterialShadingModel::Hair: return SceneGBufferShadingModelId::Hair;
    case RenderMaterialShadingModel::Eye: return SceneGBufferShadingModelId::Eye;
    case RenderMaterialShadingModel::SingleLayerWater: return SceneGBufferShadingModelId::SingleLayerWater;
    case RenderMaterialShadingModel::ThinTranslucent: return SceneGBufferShadingModelId::ThinTranslucent;
    }
    return SceneGBufferShadingModelId::DefaultLit;
}

constexpr std::uint64_t kArtifactHashPrime = 1099511628211ULL;

void HashByte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kArtifactHashPrime;
}

void HashU64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        HashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void HashString64(std::uint64_t& hash, std::string_view value) noexcept {
    HashU64(hash, static_cast<std::uint64_t>(value.size()));
    for (const char ch : value) {
        HashByte(hash, static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
    }
}

void AddArtifactDiagnostic(
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    RenderMaterialGraphDiagnosticSeverity severity,
    std::string message,
    std::string pass = {},
    std::string backend = {});

[[nodiscard]] bool ReadTextFileStrict(const std::filesystem::path& path, std::string& text) {
    std::ifstream input{ kb::platform::ExtendedLengthPath(path), std::ios::binary };
    if (!input) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        return false;
    }
    text = buffer.str();
    return true;
}

[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path) {
    std::string text;
    static_cast<void>(ReadTextFileStrict(path, text));
    return text;
}

[[nodiscard]] std::vector<std::string> ShaderIncludeNames(std::string_view source) {
    std::vector<std::string> names;
    std::size_t lineBegin = 0U;
    while (lineBegin < source.size()) {
        const std::size_t lineEnd = source.find('\n', lineBegin);
        std::string_view line = source.substr(
            lineBegin,
            lineEnd == std::string_view::npos ? source.size() - lineBegin : lineEnd - lineBegin);
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first != std::string_view::npos) {
            line.remove_prefix(first);
            if (line.front() == '#') {
                line.remove_prefix(1U);
                const std::size_t directiveBegin = line.find_first_not_of(" \t");
                if (directiveBegin != std::string_view::npos) {
                    line.remove_prefix(directiveBegin);
                }
                constexpr std::string_view directive = "include";
                if (line.starts_with(directive) &&
                    (line.size() == directive.size() || line[directive.size()] == ' ' || line[directive.size()] == '\t' ||
                     line[directive.size()] == '"' || line[directive.size()] == '<')) {
                    line.remove_prefix(directive.size());
                    const std::size_t delimiter = line.find_first_of("\"<");
                    if (delimiter != std::string_view::npos) {
                        const char close = line[delimiter] == '"' ? '"' : '>';
                        const std::size_t closeAt = line.find(close, delimiter + 1U);
                        if (closeAt != std::string_view::npos && closeAt > delimiter + 1U) {
                            names.emplace_back(line.substr(delimiter + 1U, closeAt - delimiter - 1U));
                        }
                    }
                }
            }
        }
        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineBegin = lineEnd + 1U;
    }
    return names;
}

struct ResolvedShaderDependency {
    std::filesystem::path path;
    std::string name;
    std::string content;
};

[[nodiscard]] std::optional<std::filesystem::path> ResolveShaderInclude(
    std::string_view name,
    const std::filesystem::path& includingDirectory,
    std::span<const std::string> includeDirs) {
    std::error_code error;
    if (!includingDirectory.empty()) {
        std::filesystem::path candidate = (includingDirectory / std::filesystem::path{ name }).lexically_normal();
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
        error.clear();
    }
    for (const std::string& includeDir : includeDirs) {
        std::filesystem::path candidate = (std::filesystem::path{ includeDir } / std::filesystem::path{ name }).lexically_normal();
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
        error.clear();
    }
    return std::nullopt;
}

[[nodiscard]] bool CollectShaderDependencies(
    std::string_view wrapperSource,
    const RenderMaterialGraphShaderArtifactRequest& request,
    std::vector<ResolvedShaderDependency>& dependencies,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics) {
    std::unordered_set<std::string> visitedPaths;
    const auto addDependency = [&](const std::filesystem::path& inputPath,
                                   std::string logicalName,
                                   const auto& self) -> bool {
        std::error_code canonicalError;
        std::filesystem::path path = std::filesystem::weakly_canonical(inputPath, canonicalError);
        if (canonicalError) {
            path = inputPath.lexically_normal();
        }
        std::string pathKey = path.generic_string();
#if defined(_WIN32)
        std::ranges::transform(pathKey, pathKey.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
#endif
        if (!visitedPaths.insert(pathKey).second) {
            return true;
        }

        std::string content;
        if (!ReadTextFileStrict(path, content)) {
            AddArtifactDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader dependency could not be read: " + path.generic_string() + ".");
            return false;
        }

        dependencies.push_back(ResolvedShaderDependency{
            .path = path,
            .name = std::move(logicalName),
            .content = std::move(content),
        });
        const std::string& dependencySource = dependencies.back().content;
        for (const std::string& includeName : ShaderIncludeNames(dependencySource)) {
            const std::optional<std::filesystem::path> included = ResolveShaderInclude(includeName, path.parent_path(), request.includeDirs);
            if (!included.has_value()) {
                AddArtifactDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph shader include dependency '" + includeName + "' could not be resolved.");
                return false;
            }
            if (!self(*included, includeName, self)) {
                return false;
            }
        }
        return true;
    };

    for (const std::string& dependencyFile : request.dependencyFiles) {
        const std::filesystem::path path{ dependencyFile };
        if (!addDependency(path, path.filename().generic_string(), addDependency)) {
            return false;
        }
    }
    if (!request.varyingDefPath.empty()) {
        const std::filesystem::path varyingPath{ request.varyingDefPath };
        if (!addDependency(varyingPath, varyingPath.filename().generic_string(), addDependency)) {
            return false;
        }
    }
    for (const std::string& includeName : ShaderIncludeNames(wrapperSource)) {
        const std::optional<std::filesystem::path> included = ResolveShaderInclude(includeName, {}, request.includeDirs);
        if (!included.has_value()) {
            AddArtifactDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader include dependency '" + includeName + "' could not be resolved.");
            return false;
        }
        if (!addDependency(*included, includeName, addDependency)) {
            return false;
        }
    }

    std::ranges::sort(dependencies, [](const ResolvedShaderDependency& lhs, const ResolvedShaderDependency& rhs) {
        if (lhs.name != rhs.name) {
            return lhs.name < rhs.name;
        }
        return lhs.path.generic_string() < rhs.path.generic_string();
    });
    return true;
}

[[nodiscard]] bool ShaderCompilerInputsUnchanged(
    std::span<const ResolvedShaderDependency> dependencies,
    const std::filesystem::path& shadercPath,
    std::string_view shadercBytes) {
    std::string current;
    if (!ReadTextFileStrict(shadercPath, current) || current != shadercBytes) return false;
    for (const ResolvedShaderDependency& dependency : dependencies) {
        if (!ReadTextFileStrict(dependency.path, current) || current != dependency.content) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsNonEmptyRegularFile(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error &&
        std::filesystem::file_size(path, error) > 0U && !error;
}

struct ShaderCacheFileIdentity {
    std::uint64_t bytes = 0U;
    std::uint64_t digest = 0U;
};

class ScopedShaderInputDirectory final {
public:
    explicit ScopedShaderInputDirectory(std::filesystem::path path) noexcept
        : path_{ std::move(path) } {}
    ScopedShaderInputDirectory(const ScopedShaderInputDirectory&) = delete;
    ScopedShaderInputDirectory& operator=(const ScopedShaderInputDirectory&) = delete;
    ~ScopedShaderInputDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

private:
    std::filesystem::path path_;
};

class ShaderCacheFileLock final {
public:
    ShaderCacheFileLock() = default;
    ShaderCacheFileLock(const ShaderCacheFileLock&) = delete;
    ShaderCacheFileLock& operator=(const ShaderCacheFileLock&) = delete;
    ~ShaderCacheFileLock() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) {
            OVERLAPPED overlapped{};
            static_cast<void>(UnlockFileEx(handle_, 0U, 1U, 0U, &overlapped));
            static_cast<void>(CloseHandle(handle_));
        }
#else
        if (descriptor_ >= 0) {
            static_cast<void>(flock(descriptor_, LOCK_UN));
            static_cast<void>(close(descriptor_));
        }
#endif
    }

    [[nodiscard]] bool Acquire(const std::filesystem::path& path) {
#if defined(_WIN32)
        // The cache root is the user's project directory, so this path is as deep as the user's
        // folders are: past MAX_PATH the directories still create through std::filesystem and only
        // this open would fail, which is why it goes to Win32 in extended-length form.
        handle_ = CreateFileW(
            kb::platform::ExtendedLengthPath(path).c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            failure_ = GetLastError();
            return false;
        }
        OVERLAPPED overlapped{};
        if (LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0U, 1U, 0U, &overlapped) == FALSE) {
            failure_ = GetLastError();
            return false;
        }
        return true;
#else
        descriptor_ = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        if (descriptor_ < 0 || flock(descriptor_, LOCK_EX) != 0) {
            failure_ = static_cast<unsigned long>(errno);
            return false;
        }
        return true;
#endif
    }

    // Why the last Acquire refused, in the platform's own numbering. A lock that cannot be taken
    // stops a cook, so the diagnostic has to name the cause rather than the symptom.
    [[nodiscard]] std::string FailureDetail() const {
#if defined(_WIN32)
        return "Windows error " + std::to_string(failure_);
#else
        return "errno " + std::to_string(failure_);
#endif
    }

private:
    unsigned long failure_ = 0UL;
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

[[nodiscard]] bool RelativePathWithin(
    const std::filesystem::path& child,
    const std::filesystem::path& parent,
    std::filesystem::path& relative) {
    relative = child.lexically_relative(parent);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

[[nodiscard]] bool WriteCapturedShaderInput(
    const std::filesystem::path& path,
    std::string_view content) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream output{ kb::platform::ExtendedLengthPath(path), std::ios::binary | std::ios::trunc };
    if (!output) return false;
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return output.good();
}

[[nodiscard]] bool BuildCapturedShaderInputMirror(
    const RenderMaterialGraphShaderArtifactRequest& request,
    std::span<const ResolvedShaderDependency> dependencies,
    const std::filesystem::path& mirrorRoot,
    std::vector<std::string>& includeDirs,
    std::filesystem::path& varyingDefPath) {
    includeDirs.clear();
    includeDirs.reserve(request.includeDirs.size());
    std::vector<std::filesystem::path> canonicalIncludeDirs;
    canonicalIncludeDirs.reserve(request.includeDirs.size());
    for (std::size_t index = 0U; index < request.includeDirs.size(); ++index) {
        std::error_code canonicalError;
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(request.includeDirs[index], canonicalError);
        if (canonicalError) return false;
        canonicalIncludeDirs.push_back(canonical);
        const std::filesystem::path mirror = mirrorRoot / "include" / std::to_string(index);
        std::error_code directoryError;
        std::filesystem::create_directories(mirror, directoryError);
        if (directoryError) return false;
        includeDirs.push_back(mirror.generic_string());
    }

    std::error_code varyingCanonicalError;
    const std::filesystem::path canonicalVarying = request.varyingDefPath.empty()
        ? std::filesystem::path{}
        : std::filesystem::weakly_canonical(request.varyingDefPath, varyingCanonicalError);
    if (varyingCanonicalError) return false;
    bool varyingWritten = request.varyingDefPath.empty();
    for (const ResolvedShaderDependency& dependency : dependencies) {
        for (std::size_t index = 0U; index < canonicalIncludeDirs.size(); ++index) {
            std::filesystem::path relative;
            if (RelativePathWithin(dependency.path, canonicalIncludeDirs[index], relative) &&
                !WriteCapturedShaderInput(
                    std::filesystem::path{ includeDirs[index] } / relative,
                    dependency.content)) {
                return false;
            }
        }
        if (!canonicalVarying.empty() && dependency.path == canonicalVarying) {
            varyingDefPath = mirrorRoot / "varying" / canonicalVarying.filename();
            if (!WriteCapturedShaderInput(varyingDefPath, dependency.content)) return false;
            varyingWritten = true;
        }
    }
    return varyingWritten;
}

[[nodiscard]] bool ReadShaderCacheFileIdentity(
    const std::filesystem::path& path,
    ShaderCacheFileIdentity& identity) {
    std::ifstream input{ kb::platform::ExtendedLengthPath(path), std::ios::binary };
    if (!input) return false;
    std::array<std::uint8_t, 64U * 1024U> buffer{};
    std::uint64_t hash = 1469598103934665603ULL;
    std::uint64_t bytes = 0U;
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = input.gcount();
        if (read <= 0) break;
        bytes += static_cast<std::uint64_t>(read);
        for (std::streamsize index = 0; index < read; ++index) {
            HashByte(hash, buffer[static_cast<std::size_t>(index)]);
        }
    }
    if (input.bad() || !input.eof() || bytes == 0U) return false;
    identity = ShaderCacheFileIdentity{ .bytes = bytes, .digest = hash };
    return true;
}

[[nodiscard]] std::string ShaderCacheMarker(
    std::uint64_t cookKey,
    const ShaderCacheFileIdentity& identity) {
    return "1 " + std::to_string(cookKey) + " " + std::to_string(identity.bytes) + " " +
        std::to_string(identity.digest) + "\n";
}

[[nodiscard]] bool ShaderCacheEntryIsValid(
    const std::filesystem::path& binaryPath,
    const std::filesystem::path& markerPath,
    std::uint64_t cookKey,
    ShaderCacheFileIdentity* identityOut = nullptr) {
    std::istringstream marker{ ReadTextFile(markerPath) };
    std::uint32_t version = 0U;
    std::uint64_t markerKey = 0U;
    ShaderCacheFileIdentity expected{};
    std::string trailing;
    if (!(marker >> version >> markerKey >> expected.bytes >> expected.digest) ||
        (marker >> trailing) || version != 1U || markerKey != cookKey || expected.bytes == 0U) {
        return false;
    }
    ShaderCacheFileIdentity actual{};
    if (!ReadShaderCacheFileIdentity(binaryPath, actual) ||
        actual.bytes != expected.bytes || actual.digest != expected.digest) {
        return false;
    }
    if (identityOut != nullptr) *identityOut = actual;
    return true;
}

#if defined(_WIN32)
[[nodiscard]] std::wstring ShaderCompilerWideArgument(std::string_view argument) {
    if (argument.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        argument.data(),
        static_cast<int>(argument.size()),
        nullptr,
        0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            argument.data(),
            static_cast<int>(argument.size()),
            result.data(),
            size) != size) {
        return {};
    }
    return result;
}

[[nodiscard]] std::wstring QuoteWindowsShaderCompilerArgument(std::wstring_view value) {
    std::wstring quoted{ L"\"" };
    std::size_t backslashes = 0U;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(backslashes * 2U + 1U, L'\\');
            quoted.push_back(character);
            backslashes = 0U;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0U;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2U, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}
#endif

[[nodiscard]] int RunShaderCompilerHidden(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const std::filesystem::path& diagnosticPath) {
#if defined(_WIN32)
    const std::wstring executableWide = executable.wstring();
    if (executableWide.empty()) {
        return -1;
    }
    std::wstring commandLine = QuoteWindowsShaderCompilerArgument(executableWide);
    for (const std::string& argument : arguments) {
        const std::wstring wide = ShaderCompilerWideArgument(argument);
        if (!argument.empty() && wide.empty()) {
            return -1;
        }
        commandLine.push_back(L' ');
        commandLine += QuoteWindowsShaderCompilerArgument(wide);
    }
    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };
    const HANDLE diagnostic = CreateFileW(
        kb::platform::ExtendedLengthPath(diagnosticPath).c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr);
    if (diagnostic == INVALID_HANDLE_VALUE) {
        return -1;
    }
    const HANDLE nullInput = CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = nullInput == INVALID_HANDLE_VALUE ? nullptr : nullInput;
    startup.hStdOutput = diagnostic;
    startup.hStdError = diagnostic;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        executableWide.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process);
    CloseHandle(diagnostic);
    if (nullInput != INVALID_HANDLE_VALUE) {
        CloseHandle(nullInput);
    }
    if (created == FALSE) {
        return -1;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = static_cast<DWORD>(-1);
    static_cast<void>(GetExitCodeProcess(process.hProcess, &exitCode));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
#else
    posix_spawn_file_actions_t actions{};
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return -1;
    }
    const std::string diagnostic = diagnosticPath.string();
    const int inputAction = posix_spawn_file_actions_addopen(
        &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    const int outputAction = posix_spawn_file_actions_addopen(
        &actions, STDOUT_FILENO, diagnostic.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const int errorAction = posix_spawn_file_actions_adddup2(
        &actions, STDOUT_FILENO, STDERR_FILENO);
    if (inputAction != 0 || outputAction != 0 || errorAction != 0) {
        static_cast<void>(posix_spawn_file_actions_destroy(&actions));
        return -1;
    }
    std::vector<std::string> ownedArguments;
    ownedArguments.reserve(arguments.size() + 1U);
    ownedArguments.push_back(executable.string());
    ownedArguments.insert(ownedArguments.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(ownedArguments.size() + 1U);
    for (std::string& argument : ownedArguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    pid_t process = 0;
    const int spawned = posix_spawn(
        &process, ownedArguments.front().c_str(), &actions, nullptr, argv.data(), environ);
    static_cast<void>(posix_spawn_file_actions_destroy(&actions));
    if (spawned != 0) {
        return -1;
    }
    int status = 0;
    if (waitpid(process, &status, 0) < 0 || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
#endif
}

[[nodiscard]] std::string TrimDiagnosticText(std::string text) {
    const auto isTrimmed = [](char ch) {
        return ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t';
    };
    while (!text.empty() && isTrimmed(text.back())) {
        text.pop_back();
    }
    std::size_t begin = 0U;
    while (begin < text.size() && isTrimmed(text[begin])) {
        ++begin;
    }
    return text.substr(begin);
}

void AddArtifactDiagnostic(
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    RenderMaterialGraphDiagnosticSeverity severity,
    std::string message,
    std::string pass,
    std::string backend) {
    diagnostics.push_back(RenderMaterialGraphDiagnostic{
        .severity = severity,
        .kind = RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
        .pass = std::move(pass),
        .backend = std::move(backend),
        .message = std::move(message),
    });
}

// Unreal's Pixel Depth Offset: the depth the pass writes moves away from the viewer by
// surface.pixelDepthOffset metres (never toward it) - along the view ray of a perspective view, along
// the view axis of an orthographic one (a directional light's shadow cascade). The shadow pass pushes
// the caster by the same amount, so the pushed surface the lighting reconstructs does not shadow
// itself. Writing gl_FragDepth turns off early depth testing for the material.
void AppendMaterialGraphPixelDepthOffsetDepth(std::string& wrapper) {
    wrapper += "    vec4 kbPdoView = mul(u_view, vec4(v_worldPos, 1.0));\n";
    wrapper += "    float kbPdoOffset = max(surface.pixelDepthOffset, 0.0);\n";
    wrapper += "    float kbPdoDistance = max(length(kbPdoView.xyz), 0.0001);\n";
    wrapper += "    bool kbPdoOrthographic = abs(mul(u_proj, vec4(0.0, 0.0, 1.0, 0.0)).w) < 0.5;\n";
    wrapper += "    vec3 kbPdoPushed = kbPdoOrthographic ? kbPdoView.xyz + vec3(0.0, 0.0, kbPdoOffset)\n";
    wrapper += "        : kbPdoView.xyz * ((kbPdoDistance + kbPdoOffset) / kbPdoDistance);\n";
    wrapper += "    vec4 kbPdoClip = mul(u_proj, vec4(kbPdoPushed, 1.0));\n";
    wrapper += "    float kbPdoDepth = kbPdoClip.z / kbPdoClip.w;\n";
    wrapper += "#if BGFX_SHADER_LANGUAGE_GLSL\n";
    // OpenGL clip depth is -1..1; the depth buffer stores 0..1.
    wrapper += "    kbPdoDepth = kbPdoDepth * 0.5 + 0.5;\n";
    wrapper += "#endif\n";
}

void AppendMaterialGraphPixelDepthOffset(std::string& wrapper) {
    AppendMaterialGraphPixelDepthOffsetDepth(wrapper);
    wrapper += "    gl_FragDepth = kbPdoDepth;\n";
}

// Unreal's Refraction (Index of Refraction mode) on a Translucent surface: the opaque scene behind it
// is read from the scene-color snapshot at an offset along the view-space normal, scaled by (IOR - 1)
// and the projection, toward the middle of a convex surface when IOR > 1 like a lens; the lit surface
// then covers it by its opacity. The fragment writes alpha 1, so the alpha blend keeps its colour.
// The sign between NDC y and the screen position comes from the derivatives, so D3D, Vulkan and
// OpenGL (bottom-left origin) bend the same way.
// The opaque scene behind the surface (kbBehind), at the refracted position when the graph drives
// Refraction (see above), else straight behind.
void AppendMaterialGraphSceneBehind(std::string& wrapper, const std::string& normal, const bool refracts) {
    if (!refracts) {
        wrapper += "    vec2 kbBehindUv = ctx.screenPosition;\n";
        wrapper += "    vec3 kbBehind = texture2DLod(s_kbSceneColor, kbBehindUv, 0.0).rgb;\n";
        return;
    }
    wrapper += "    vec3 kbRefrViewNormal = mul(u_view, vec4(" + normal + ", 0.0)).xyz;\n";
    wrapper += "    vec4 kbRefrClip = mul(u_viewProj, vec4(v_worldPos, 1.0));\n";
    wrapper += "    float kbRefrNdcY = kbRefrClip.y / kbRefrClip.w;\n";
    wrapper += "    float kbRefrYSign = dFdy(kbRefrNdcY) < 0.0 ? -1.0 : 1.0;\n";
    wrapper += "    vec2 kbRefrNdc = -kbRefrViewNormal.xy * (surface.refraction - 1.0) * vec2(u_proj[0][0], u_proj[1][1]);\n";
    wrapper += "    vec2 kbBehindUv = clamp(ctx.screenPosition + vec2(kbRefrNdc.x, kbRefrNdc.y * kbRefrYSign) * 0.5, vec2(0.0, 0.0), vec2(1.0, 1.0));\n";
    wrapper += "    vec3 kbBehind = texture2DLod(s_kbSceneColor, kbBehindUv, 0.0).rgb;\n";
}

void AppendMaterialGraphRefraction(std::string& wrapper, const std::string& normal, const std::string& litColor) {
    AppendMaterialGraphSceneBehind(wrapper, normal, true);
    wrapper += "    gl_FragColor = vec4(mix(kbBehind, " + litColor + ", clamp(surface.alpha, 0.0, 1.0)), 1.0);\n";
}

// Unreal's Thin Translucent: the scene behind passes coloured by Transmittance Color where the
// surface does not cover it, the diffuse light is scaled by the opacity and the specular reflection
// stays whole, as on clear glass. The specular is the lighting of the same surface with no albedo.
// The fragment writes alpha 1 over the scene copy, so the alpha blend keeps its colour.
void AppendMaterialGraphThinTranslucent(std::string& wrapper, const RenderMaterialGraphShaderSource& shader,
    const std::string& shadingModel) {
    wrapper += "    vec3 kbThinSpecular = KbEvaluateForwardLighting(worldNormal, v_worldPos, vec3(0.0, 0.0, 0.0), 0.0, roughness, specular, occlusion, " +
        shadingModel + ", 0.0, 0.0);\n";
    wrapper += "    vec3 kbThinDiffuse = max(lighting - kbThinSpecular, vec3(0.0, 0.0, 0.0));\n";
    AppendMaterialGraphSceneBehind(wrapper, "worldNormal", shader.reflection.hasRefraction);
    wrapper += "    float kbThinCoverage = clamp(surface.alpha, 0.0, 1.0);\n";
    wrapper += "    gl_FragColor = vec4(kbBehind * surface.transmittanceColor * (1.0 - kbThinCoverage) + kbThinDiffuse * kbThinCoverage + kbThinSpecular + surface.emissive, 1.0);\n";
}

// Unreal's Single Layer Water: the scene behind is dimmed by the water between the surface and it,
// exp(-(absorption + scattering) * thickness) times Color Scale Behind Water; the water scatters the
// sky and the sun (Henyey-Greenstein phase of Phase G) in proportion scattering / extinction; the
// surface reflects on top. Thickness comes from the opaque scene depth behind, along the view ray.
void AppendMaterialGraphSingleLayerWater(std::string& wrapper, const RenderMaterialGraphShaderSource& shader,
    const std::string& shadingModel) {
    wrapper += "    vec3 kbWaterSpecular = KbEvaluateForwardLighting(worldNormal, v_worldPos, vec3(0.0, 0.0, 0.0), 0.0, roughness, specular, occlusion, " +
        shadingModel + ", 0.0, 0.0);\n";
    AppendMaterialGraphSceneBehind(wrapper, "worldNormal", shader.reflection.hasRefraction);
    wrapper += "    float kbWaterDeviceDepth = texture2DLod(s_kbSceneDepth, kbBehindUv, 0.0).x;\n";
    wrapper += "#if BGFX_SHADER_LANGUAGE_GLSL\n";
    // OpenGL clip depth is -1..1; the depth buffer stores 0..1.
    wrapper += "    kbWaterDeviceDepth = kbWaterDeviceDepth * 2.0 - 1.0;\n";
    wrapper += "#endif\n";
    wrapper += "    vec4 kbWaterBehindView = mul(u_invProj, vec4(0.0, 0.0, kbWaterDeviceDepth, 1.0));\n";
    wrapper += "    vec4 kbWaterSurfaceView = mul(u_view, vec4(v_worldPos, 1.0));\n";
    if (shader.reflection.hasRefraction) {
        // Like Unreal: a refracted sample that lands on something in front of the water falls back to
        // the scene straight behind.
        wrapper += "    if ((kbWaterBehindView.z / kbWaterBehindView.w) / kbWaterSurfaceView.z < 1.0) {\n";
        wrapper += "        kbBehindUv = ctx.screenPosition;\n";
        wrapper += "        kbBehind = texture2DLod(s_kbSceneColor, kbBehindUv, 0.0).rgb;\n";
        wrapper += "        kbWaterDeviceDepth = texture2DLod(s_kbSceneDepth, kbBehindUv, 0.0).x;\n";
        wrapper += "#if BGFX_SHADER_LANGUAGE_GLSL\n";
        wrapper += "        kbWaterDeviceDepth = kbWaterDeviceDepth * 2.0 - 1.0;\n";
        wrapper += "#endif\n";
        wrapper += "        kbWaterBehindView = mul(u_invProj, vec4(0.0, 0.0, kbWaterDeviceDepth, 1.0));\n";
        wrapper += "    }\n";
    }
    wrapper += "    float kbWaterThickness = max(length(kbWaterSurfaceView.xyz) * ((kbWaterBehindView.z / kbWaterBehindView.w) / kbWaterSurfaceView.z - 1.0), 0.0);\n";
    wrapper += "    vec3 kbWaterScattering = max(surface.waterScattering, vec3(0.0, 0.0, 0.0));\n";
    wrapper += "    vec3 kbWaterExtinction = max(kbWaterScattering + max(surface.waterAbsorption, vec3(0.0, 0.0, 0.0)), vec3(0.00001, 0.00001, 0.00001));\n";
    wrapper += "    vec3 kbWaterTransmittance = exp(-kbWaterExtinction * kbWaterThickness);\n";
    wrapper += "    vec3 kbWaterView = normalize(u_cameraPosition.xyz - v_worldPos);\n";
    wrapper += "    float kbWaterG = clamp(surface.waterPhaseG, -0.99, 0.99);\n";
    wrapper += "    vec3 kbWaterSunLight = vec3(0.0, 0.0, 0.0);\n";
    wrapper += "    if (u_lightParams.x > 0.5 && u_lightDirKind[0].w < 0.5) {\n";
    wrapper += "        float kbWaterCos = dot(normalize(u_lightDirKind[0].xyz), kbWaterView);\n";
    // 4 pi times the Henyey-Greenstein phase: 1 for an isotropic medium.
    wrapper += "        float kbWaterPhase = (1.0 - kbWaterG * kbWaterG) / pow(max(1.0 + kbWaterG * kbWaterG - 2.0 * kbWaterG * kbWaterCos, 0.0001), 1.5);\n";
    wrapper += "        kbWaterSunLight = u_lightColorIntensity[0].rgb * (u_lightColorIntensity[0].a * 3.14159265 * kbWaterPhase);\n";
    wrapper += "    }\n";
    wrapper += "    vec3 kbWaterAmbient = KbEnvironmentColor(vec3(0.0, 1.0, 0.0)) * u_environmentParams.y;\n";
    wrapper += "    vec3 kbWaterInScatter = kbWaterScattering / kbWaterExtinction * (vec3(1.0, 1.0, 1.0) - kbWaterTransmittance) * (kbWaterAmbient + kbWaterSunLight);\n";
    wrapper += "    gl_FragColor = vec4(kbBehind * kbWaterTransmittance * max(surface.waterColorScaleBehind, 0.0) + kbWaterInScatter + kbWaterSpecular + surface.emissive, 1.0);\n";
}

// The surface field with the colour the model scatters with (writesShadingModelColor).
std::string_view GraphShadingModelColorField(const RenderMaterialGraphShaderSource& shader) {
    return shader.reflection.shadingModel == RenderMaterialShadingModel::Cloth ? "surface.fuzzColor" : "surface.subsurfaceColor";
}

// The shading basis of a pixel, like Unreal's: the interpolated vertex normal renormalized per pixel. World
// Position Offset and Displacement move the geometry but keep the vertex normals (a material that bends the
// surface supplies its own normal); a normal rebuilt from screen derivatives of the position would be the flat
// normal of each triangle and facet every curved surface.
void AppendMaterialGraphTangentBasis(std::string& wrapper) {
    wrapper += "    vec3 basisNormal = normalize(v_normal);\n";
    // The graph source's tangent basis, the one PixelNormalWS reads the Normal through.
    wrapper += "    vec3 vertexTangent = KbGraphBasisTangent(basisNormal, v_tangent);\n";
    wrapper += "    float vertexHandedness = KbGraphBasisHandedness(basisNormal, vertexTangent, v_bitangent);\n";
    wrapper += "    vec3 vertexBitangent = normalize(cross(basisNormal, vertexTangent) * vertexHandedness);\n";
    wrapper += "    vec3 basisTangent = vertexTangent;\n";
    wrapper += "    vec3 basisBitangent = vertexBitangent;\n";
}

} // namespace

std::string_view RenderMaterialGraphShaderBackendName(RenderMaterialGraphShaderBackend backend) noexcept {
    switch (backend) {
    case RenderMaterialGraphShaderBackend::Dxbc: return "dxbc";
    case RenderMaterialGraphShaderBackend::Dxil: return "dxil";
    case RenderMaterialGraphShaderBackend::Spirv: return "spirv";
    case RenderMaterialGraphShaderBackend::Metal: return "metal";
    case RenderMaterialGraphShaderBackend::Essl: return "essl";
    case RenderMaterialGraphShaderBackend::Glsl: return "glsl";
    case RenderMaterialGraphShaderBackend::Wgsl: return "wgsl";
    }
    return "spirv";
}

std::string_view RenderMaterialGraphShaderBackendProfile(RenderMaterialGraphShaderBackend backend) noexcept {
    switch (backend) {
    case RenderMaterialGraphShaderBackend::Dxbc: return "s_5_0";
    case RenderMaterialGraphShaderBackend::Dxil: return "s_6_0";
    case RenderMaterialGraphShaderBackend::Spirv: return "spirv";
    case RenderMaterialGraphShaderBackend::Metal: return "metal";
    case RenderMaterialGraphShaderBackend::Essl: return "300_es";
    case RenderMaterialGraphShaderBackend::Glsl: return "440";
    case RenderMaterialGraphShaderBackend::Wgsl: return "wgsl";
    }
    return "spirv";
}

std::string_view RenderMaterialGraphShaderBackendDirectory(RenderMaterialGraphShaderBackend backend) noexcept {
    return RenderMaterialGraphShaderBackendName(backend);
}

std::optional<RenderMaterialGraphShaderBackend> ParseRenderMaterialGraphShaderBackend(std::string_view text) noexcept {
    if (text == "dxbc") return RenderMaterialGraphShaderBackend::Dxbc;
    if (text == "dxil") return RenderMaterialGraphShaderBackend::Dxil;
    if (text == "spirv") return RenderMaterialGraphShaderBackend::Spirv;
    if (text == "metal") return RenderMaterialGraphShaderBackend::Metal;
    if (text == "essl") return RenderMaterialGraphShaderBackend::Essl;
    if (text == "glsl") return RenderMaterialGraphShaderBackend::Glsl;
    if (text == "wgsl") return RenderMaterialGraphShaderBackend::Wgsl;
    return std::nullopt;
}

[[nodiscard]] std::string UniqueShaderTemporaryName(std::string_view suffix) {
    static std::atomic<std::uint64_t> serial{ 0U };
#if defined(_WIN32)
    const std::uint64_t processId = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const std::uint64_t processId = static_cast<std::uint64_t>(getpid());
#endif
    return ".kb-" + std::string{ suffix } + "." + std::to_string(processId) + "." +
        std::to_string(serial.fetch_add(1U, std::memory_order_relaxed));
}

[[nodiscard]] std::filesystem::path UniqueShaderTemporaryPath(
    const std::filesystem::path& destination,
    std::string_view suffix) {
    return destination.parent_path() / UniqueShaderTemporaryName(suffix);
}

// bx, and so the shaderc built on it, opens every file through the narrow CRT, which resolves a
// path against MAX_PATH whatever prefix it carries: the compiler cannot read or write anything
// under a project directory deeper than that, and no amount of care on this side changes it.
// Everything the compiler itself touches - its captured input mirror, the binary it writes, its
// log - therefore lives under a scratch root the engine owns and keeps short. Only the finished
// binary is published into the project's cache, which is as deep as the user's folders are.
[[nodiscard]] std::filesystem::path ShaderCompilerScratchPath(std::string_view suffix, std::error_code& error) {
    return kb::platform::ToolScratchPath(suffix, error);
}

// `failure` carries the platform's own error number when the rename does not happen, so a cook
// that cannot publish says which call refused rather than only which file it wanted.
[[nodiscard]] bool PublishShaderFileAtomically(
    const std::filesystem::path& staging,
    const std::filesystem::path& destination,
    unsigned long& failure) noexcept {
#if defined(_WIN32)
    const std::filesystem::path from = kb::platform::ExtendedLengthPath(staging);
    const std::filesystem::path to = kb::platform::ExtendedLengthPath(destination);
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
        return true;
    }
    failure = GetLastError();
    return false;
#else
    if (::rename(staging.c_str(), destination.c_str()) == 0) {
        return true;
    }
    failure = static_cast<unsigned long>(errno);
    return false;
#endif
}

[[nodiscard]] std::string PlatformFailureDetail(unsigned long failure) {
#if defined(_WIN32)
    return "Windows error " + std::to_string(failure);
#else
    return "errno " + std::to_string(failure);
#endif
}

// The compiler writes on the scratch volume, so making its result visible is a copy next to the
// destination followed by a rename within that directory: a reader sees either the binary that was
// there before or the whole new one, never a half-copied file.
[[nodiscard]] bool PublishCompiledShaderBinary(
    const std::filesystem::path& staging,
    const std::filesystem::path& destination,
    std::string& failure) {
    const std::filesystem::path local = UniqueShaderTemporaryPath(destination, "publish.tmp");
    std::error_code error;
    std::filesystem::copy_file(staging, local, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        failure = "copying the compiled binary into the cache directory failed: " + error.message();
        return false;
    }
    unsigned long renameFailure = 0UL;
    if (PublishShaderFileAtomically(local, destination, renameFailure)) {
        return true;
    }
    failure = "renaming it onto the cache entry failed: " + PlatformFailureDetail(renameFailure);
    std::filesystem::remove(local, error);
    return false;
}

[[nodiscard]] bool WriteShaderTextAtomically(
    const std::filesystem::path& destination,
    std::string_view text) {
    const std::filesystem::path staging = UniqueShaderTemporaryPath(destination, "write.tmp");
    {
        std::ofstream output{ kb::platform::ExtendedLengthPath(staging), std::ios::binary | std::ios::trunc };
        if (!output) return false;
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output) {
            output.close();
            std::error_code error;
            std::filesystem::remove(staging, error);
            return false;
        }
    }
    unsigned long failure = 0UL;
    if (PublishShaderFileAtomically(staging, destination, failure)) return true;
    std::error_code error;
    std::filesystem::remove(staging, error);
    return false;
}

namespace {

[[nodiscard]] std::optional<kb::assets::bake::ShaderBakeBackend> ToBakeShaderBackend(
    RenderMaterialGraphShaderBackend backend) noexcept {
    using kb::assets::bake::ShaderBakeBackend;
    switch (backend) {
    case RenderMaterialGraphShaderBackend::Dxbc: return ShaderBakeBackend::Dxbc;
    case RenderMaterialGraphShaderBackend::Dxil: return ShaderBakeBackend::Dxil;
    case RenderMaterialGraphShaderBackend::Spirv: return ShaderBakeBackend::Spirv;
    case RenderMaterialGraphShaderBackend::Metal: return ShaderBakeBackend::Metal;
    case RenderMaterialGraphShaderBackend::Essl: return ShaderBakeBackend::Essl;
    case RenderMaterialGraphShaderBackend::Glsl: return ShaderBakeBackend::Glsl;
    case RenderMaterialGraphShaderBackend::Wgsl: return ShaderBakeBackend::Wgsl;
    }
    return std::nullopt;
}

} // namespace

const RenderMaterialGraphShaderBinary* RenderMaterialGraphShaderArtifact::FindBinary(RenderMaterialGraphShaderBackend backend) const noexcept {
    for (const RenderMaterialGraphShaderBinary& binary : binaries) {
        if (binary.backend == backend) {
            return &binary;
        }
    }
    return nullptr;
}

const RenderMaterialGraphShaderBinary* RenderMaterialGraphShaderArtifact::FindVertexBinary(RenderMaterialGraphShaderBackend backend) const noexcept {
    for (const RenderMaterialGraphShaderBinary& binary : vertexBinaries) {
        if (binary.backend == backend) {
            return &binary;
        }
    }
    return nullptr;
}

bool RenderMaterialGraphShaderArtifactResult::Succeeded() const noexcept {
    if (!artifact.has_value()) {
        return false;
    }
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
}

const RenderMaterialGraphShaderManifestEntry* RenderMaterialGraphShaderManifest::Find(
    std::uint64_t graphSourceHash,
    std::uint64_t variantKey,
    std::string_view pass,
    RenderMaterialGraphShaderBackend backend) const noexcept {
    for (const RenderMaterialGraphShaderManifestEntry& entry : entries) {
        if (entry.graphSourceHash == graphSourceHash &&
            entry.variantKey == variantKey &&
            entry.pass == pass &&
            entry.backend == backend) {
            return &entry;
        }
    }
    return nullptr;
}

const RenderMaterialGraphShaderManifestEntry* RenderMaterialGraphShaderManifest::Find(
    std::uint64_t graphSourceHash,
    std::string_view pass,
    RenderMaterialGraphShaderBackend backend) const noexcept {
    const RenderMaterialGraphShaderManifestEntry* match = nullptr;
    for (const RenderMaterialGraphShaderManifestEntry& entry : entries) {
        if (entry.graphSourceHash != graphSourceHash || entry.pass != pass || entry.backend != backend) {
            continue;
        }
        if (match != nullptr && match->variantKey != entry.variantKey) {
            return nullptr;
        }
        match = &entry;
    }
    return match;
}

namespace {

// UV2..UV7 travel as three extra varyings, only for a graph that reads them.
[[nodiscard]] bool MaterialGraphUsesExtraUvs(const RenderMaterialGraphShaderSource& shader) {
    return std::ranges::find(shader.reflection.requiredVaryings, std::string_view{ "extraUvs" }) != shader.reflection.requiredVaryings.end();
}
[[nodiscard]] bool MaterialGraphIsDecal(const RenderMaterialGraphShaderSource& shader) noexcept {
    return shader.reflection.domain == RenderMaterialDomain::DeferredDecal;
}
// A Deferred Decal carries the rows of its box's inverse transform in the same three varyings.
[[nodiscard]] std::string ExtraUvVaryings(const RenderMaterialGraphShaderSource& shader) {
    return MaterialGraphUsesExtraUvs(shader) || MaterialGraphIsDecal(shader) ? ", v_uv23, v_uv45, v_uv67" : "";
}

// Unreal's Deferred Decal: the opaque surface under this pixel (the scene depth) in the space of the decal's
// box (v_uv23/45/67 hold the rows of its inverse transform). Only what lies inside the unit box is decorated;
// the decal projects along the box's Y axis, its X and Z span the texture.
void AppendMaterialGraphDecalProjection(std::string& wrapper) {
    wrapper += "    float kbDecalDepth = texture2D(s_kbSceneDepth, ctx.screenPosition).x;\n";
    wrapper += "#if BGFX_SHADER_LANGUAGE_GLSL\n";
    wrapper += "    vec3 kbDecalNdc = vec3(ctx.screenPosition * 2.0 - 1.0, kbDecalDepth * 2.0 - 1.0);\n";
    wrapper += "#else\n";
    wrapper += "    vec3 kbDecalNdc = vec3(ctx.screenPosition.x * 2.0 - 1.0, 1.0 - ctx.screenPosition.y * 2.0, kbDecalDepth);\n";
    wrapper += "#endif\n";
    wrapper += "    vec4 kbDecalClip = mul(u_invViewProj, vec4(kbDecalNdc, 1.0));\n";
    wrapper += "    vec3 kbDecalPos = kbDecalClip.xyz / kbDecalClip.w;\n";
    wrapper += "    vec3 kbDecalLocal = vec3(dot(v_uv23, vec4(kbDecalPos, 1.0)), dot(v_uv45, vec4(kbDecalPos, 1.0)), dot(v_uv67, vec4(kbDecalPos, 1.0)));\n";
    wrapper += "    if (max(max(abs(kbDecalLocal.x), abs(kbDecalLocal.y)), abs(kbDecalLocal.z)) > 0.5)\n    {\n        discard;\n    }\n";
    wrapper += "    vec3 kbDecalNormal = normalize(cross(dFdx(kbDecalPos), dFdy(kbDecalPos)));\n";
    wrapper += "    kbDecalNormal = dot(kbDecalNormal, u_cameraPosition.xyz - kbDecalPos) < 0.0 ? -kbDecalNormal : kbDecalNormal;\n";
    // The texture's X (the box's X axis, or its Z on a surface that faces X) along the surface.
    wrapper += "    vec3 kbDecalTangent = v_uv23.xyz - kbDecalNormal * dot(kbDecalNormal, v_uv23.xyz);\n";
    wrapper += "    kbDecalTangent = normalize(dot(kbDecalTangent, kbDecalTangent) > 0.000001 ? kbDecalTangent : v_uv67.xyz - kbDecalNormal * dot(kbDecalNormal, v_uv67.xyz));\n";
    wrapper += "    ctx.uv0 = kbDecalLocal.xz + 0.5;\n";
    wrapper += "    ctx.uv1 = ctx.uv0;\n";
    wrapper += "    ctx.uv23 = vec4_splat(0.0);\n    ctx.uv45 = vec4_splat(0.0);\n    ctx.uv67 = vec4_splat(0.0);\n";
    wrapper += "    ctx.normal = kbDecalNormal;\n";
    wrapper += "    ctx.tangent = kbDecalTangent;\n";
    wrapper += "    ctx.bitangent = cross(kbDecalNormal, kbDecalTangent);\n";
    wrapper += "    ctx.worldPos = kbDecalPos + u_renderOriginOffset.xyz;\n";
    wrapper += "    ctx.viewDir = normalize(u_cameraPosition.xyz - kbDecalPos);\n";
    wrapper += "    ctx.localPosition = kbDecalLocal;\n";
    wrapper += "    ctx.fragmentDepth = kbDecalDepth;\n";
}

// The material context of a fragment. The shadow pass has no lighting uniforms, so it uses explicit
// neutral constants for the view and light inputs; every other pass reads the real camera and light.
void AppendMaterialGraphFragmentContext(std::string& wrapper, bool shadowPass, bool extraUvs, bool decal = false) {
    wrapper += "    MaterialGraphContext ctx;\n";
    wrapper += "    ctx.uv0 = v_texcoord0;\n";
    wrapper += "    ctx.uv1 = v_shadowFlags.zw;\n";
    wrapper += extraUvs ? "    ctx.uv23 = v_uv23;\n    ctx.uv45 = v_uv45;\n    ctx.uv67 = v_uv67;\n"
                        : "    ctx.uv23 = vec4_splat(0.0);\n    ctx.uv45 = vec4_splat(0.0);\n    ctx.uv67 = vec4_splat(0.0);\n";
    wrapper += "    ctx.normal = normalize(v_normal);\n";
    wrapper += "    ctx.tangent = normalize(v_tangent);\n";
    wrapper += "    ctx.bitangent = normalize(v_bitangent);\n";
    wrapper += "    ctx.worldPos = v_worldPos + u_renderOriginOffset.xyz;\n";
    if (shadowPass) {
        wrapper += "    ctx.viewDir = vec3(0.0, 0.0, 1.0);\n";
    } else {
        wrapper += "    ctx.viewDir = normalize(u_cameraPosition.xyz - v_worldPos);\n";
    }
    wrapper += "    ctx.vertexColor = v_color0;\n";
    wrapper += "    ctx.time = u_time.x;\n";
    wrapper += "    ctx.deltaTime = u_time.y;\n";
    wrapper += "    ctx.dynamicParameter = u_dynamicParameter;\n";
    // MAT-75 screen-space coordinate (0..1) from the fragment position and bgfx viewport rect.
    wrapper += "    ctx.screenPosition = gl_FragCoord.xy / max(u_viewRect.zw, vec2(1.0, 1.0));\n";
    // MAT-76 object-space inputs interpolated from the vertex shader.
    wrapper += "    ctx.localPosition = v_objectLocalPos.xyz;\n";
    wrapper += "    ctx.objectPosition = v_objectWorldPos.xyz + u_renderOriginOffset.xyz;\n";
    // MAT-77 per-instance scalars carried in the free .w lanes of the object-space varyings.
    wrapper += "    ctx.perInstanceRandom = v_objectLocalPos.w;\n";
    wrapper += "    ctx.objectRadius = v_objectWorldPos.w;\n";
    wrapper += "    ctx.perInstanceFadeAmount = v_shadowFlags.y;\n";
    wrapper += "    ctx.perInstanceCustomData = v_objectOrientation.w;\n";
    wrapper += "    ctx.objectBounds = vec4(v_objectWorldPos.xyz + u_renderOriginOffset.xyz, max(v_objectWorldPos.w, 0.0));\n";
    wrapper += "    ctx.objectOrientation = dot(v_objectOrientation.xyz, v_objectOrientation.xyz) > 0.0001 ? normalize(v_objectOrientation.xyz) : vec3(0.0, 0.0, 1.0);\n";
    wrapper += "    ctx.preSkinnedPosition = v_objectLocalPos.xyz;\n";
    wrapper += "    ctx.preSkinnedNormal = dot(v_preSkinnedNormal, v_preSkinnedNormal) > 0.0001 ? normalize(v_preSkinnedNormal) : vec3(0.0, 0.0, 1.0);\n";
    // MAT-46: world-space view/light inputs.
    wrapper += "    ctx.viewSize = u_viewRect.zw;\n";
    wrapper += "    ctx.twoSidedSign = gl_FrontFacing ? 1.0 : -1.0;\n";
    // MAT-80/#18b: this fragment's device depth, so DepthFade can compare against the sampled scene depth.
    wrapper += "    ctx.fragmentDepth = gl_FragCoord.z;\n";
    if (shadowPass) {
        wrapper += "    ctx.cameraPosition = vec3(0.0, 0.0, 0.0);\n";
        wrapper += "    ctx.lightVector = vec3(0.0, 1.0, 0.0);\n";
    } else {
        wrapper += "    ctx.cameraPosition = u_cameraPosition.xyz + u_renderOriginOffset.xyz;\n";
        wrapper += "    ctx.lightVector = (u_lightParams.x > 0.5) ? normalize(-u_lightDirKind[0].xyz) : vec3(0.0, 1.0, 0.0);\n";
    }
    if (decal) {
        AppendMaterialGraphDecalProjection(wrapper);
    }
    wrapper += "    MaterialSurface surface = EvaluateMaterialGraph(ctx);\n";
}

[[nodiscard]] bool MaterialGraphWritesDepth(const RenderMaterialGraphShaderSource& shader) noexcept {
    return shader.reflection.blendMode == RenderMaterialGraphBlendMode::Opaque ||
        shader.reflection.blendMode == RenderMaterialGraphBlendMode::Masked;
}

// Unreal's velocity pass with the material's own shaders: the surface the base pass kept - clipped by a
// Masked material, at the depth its Pixel Depth Offset wrote - writes the motion between the positions its
// vertex shader produces for this frame and for the previous one (BuildGraphVertexWrapperSource).
std::string BuildGraphMotionVectorFragmentWrapperSource(const RenderMaterialGraphShaderSource& shader) {
    const bool masked = shader.reflection.blendMode == RenderMaterialGraphBlendMode::Masked;
    const bool pixelDepthOffset = shader.reflection.hasPixelDepthOffset && MaterialGraphWritesDepth(shader);
    std::string wrapper;
    wrapper += "$input v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal, v_currentClip, v_previousClip" + ExtraUvVaryings(shader) + "\n\n";
    wrapper += "#include <bgfx_shader.sh>\n";
    wrapper += "#include \"motion_vectors.sh\"\n";
    wrapper += "SAMPLER2D(s_sceneDepth, 0);\n";
    wrapper += "uniform vec4 u_time;\n";
    wrapper += "uniform vec4 u_dynamicParameter;\n";
    wrapper += "uniform vec4 u_materialParams;\n";
    wrapper += "uniform vec4 u_renderOriginOffset;\n";
    wrapper += "uniform vec4 u_cameraPosition;\n";
    wrapper += "uniform vec4 u_lightDirKind[32];\n";
    wrapper += "uniform vec4 u_lightParams;\n";
    wrapper += "\n// pass:MotionVectors\n\n";
    wrapper += shader.source;
    wrapper += "\nvoid main()\n{\n";
    wrapper += "    float kbMotionDepth = gl_FragCoord.z;\n";
    if (masked || pixelDepthOffset) {
        AppendMaterialGraphFragmentContext(wrapper, false, MaterialGraphUsesExtraUvs(shader));
    }
    if (pixelDepthOffset) {
        AppendMaterialGraphPixelDepthOffsetDepth(wrapper);
        wrapper += "    kbMotionDepth = kbPdoDepth;\n";
    }
    wrapper += "    bool kbMotionVisible = KbMotionSurfaceVisible(s_sceneDepth, gl_FragCoord.xy, kbMotionDepth);\n";
    if (masked) {
        wrapper += "    kbMotionVisible = kbMotionVisible && surface.alpha >= surface.alphaClipThreshold;\n";
    }
    wrapper += "    if (!kbMotionVisible)\n    {\n        discard;\n    }\n";
    wrapper += "    gl_FragColor = vec4(KbMotionVelocity(v_currentClip, v_previousClip), 0.0, 1.0);\n";
    wrapper += "}\n";
    return wrapper;
}

} // namespace

bool RenderMaterialGraphDrawsOwnMotionVectors(const RenderMaterialGraphReflection& reflection) noexcept {
    const bool writesDepth = reflection.blendMode == RenderMaterialGraphBlendMode::Opaque ||
        reflection.blendMode == RenderMaterialGraphBlendMode::Masked;
    return writesDepth &&
        (reflection.hasWorldPositionOffset || reflection.hasDisplacement || reflection.hasPixelDepthOffset ||
         reflection.blendMode == RenderMaterialGraphBlendMode::Masked);
}

bool RenderMaterialGraphCooksScenePass(const RenderMaterialGraphReflection& reflection, std::string_view pass) noexcept {
    // A graph of another domain draws in its own pass only.
    if (reflection.domain == RenderMaterialDomain::PostProcess) {
        return pass == kRenderMaterialGraphPostProcessPass;
    }
    if (reflection.domain == RenderMaterialDomain::DeferredDecal) {
        return pass == "BaseTransparent";
    }
    if (pass == kRenderMaterialGraphPostProcessPass) {
        return false;
    }
    return pass != kRenderMaterialGraphMotionVectorsPass || RenderMaterialGraphDrawsOwnMotionVectors(reflection);
}

bool RenderMaterialGraphRequiresGeneratedVertexShader(const RenderMaterialGraphReflection& reflection) noexcept {
    return reflection.hasWorldPositionOffset || reflection.hasCustomizedUv0 || reflection.hasDisplacement ||
        reflection.domain == RenderMaterialDomain::DeferredDecal;
}

namespace {

// Unreal's post-process material: the graph evaluated for every pixel of the view with Scene Color / Scene Depth
// reading the chain's input (s_kbSceneColor at stage 4, s_kbSceneDepth at 5); its Emissive Color replaces the
// colour.
std::string BuildGraphPostProcessFragmentWrapperSource(const RenderMaterialGraphShaderSource& shader) {
    std::string wrapper;
    wrapper += "$input v_texcoord0\n\n";
    wrapper += "#include <bgfx_shader.sh>\n";
    wrapper += "uniform vec4 u_time;\n";
    wrapper += "uniform vec4 u_dynamicParameter;\n";
    wrapper += "uniform vec4 u_materialParams;\n";
    wrapper += "uniform vec4 u_renderOriginOffset;\n";
    wrapper += "uniform vec4 u_cameraPosition;\n\n";
    wrapper += shader.source;
    wrapper += "\nvoid main()\n{\n";
    wrapper += "    MaterialGraphContext ctx;\n";
    wrapper += "    ctx.uv0 = v_texcoord0;\n";
    wrapper += "    ctx.uv1 = v_texcoord0;\n";
    wrapper += "    ctx.uv23 = vec4_splat(0.0);\n    ctx.uv45 = vec4_splat(0.0);\n    ctx.uv67 = vec4_splat(0.0);\n";
    wrapper += "    ctx.normal = vec3(0.0, 0.0, 1.0);\n";
    wrapper += "    ctx.tangent = vec3(1.0, 0.0, 0.0);\n";
    wrapper += "    ctx.bitangent = vec3(0.0, 1.0, 0.0);\n";
    wrapper += "    ctx.worldPos = u_cameraPosition.xyz;\n";
    wrapper += "    ctx.viewDir = vec3(0.0, 0.0, 1.0);\n";
    wrapper += "    ctx.vertexColor = vec4_splat(1.0);\n";
    wrapper += "    ctx.time = u_time.x;\n";
    wrapper += "    ctx.deltaTime = u_time.y;\n";
    wrapper += "    ctx.dynamicParameter = u_dynamicParameter;\n";
    wrapper += "    ctx.screenPosition = v_texcoord0;\n";
    wrapper += "    ctx.localPosition = vec3_splat(0.0);\n";
    wrapper += "    ctx.objectPosition = vec3_splat(0.0);\n";
    wrapper += "    ctx.perInstanceRandom = 0.0;\n";
    wrapper += "    ctx.perInstanceFadeAmount = 0.0;\n";
    wrapper += "    ctx.perInstanceCustomData = 0.0;\n";
    wrapper += "    ctx.objectRadius = 0.0;\n";
    wrapper += "    ctx.objectBounds = vec4_splat(0.0);\n";
    wrapper += "    ctx.objectOrientation = vec3(0.0, 0.0, 1.0);\n";
    wrapper += "    ctx.preSkinnedPosition = vec3_splat(0.0);\n";
    wrapper += "    ctx.preSkinnedNormal = vec3(0.0, 0.0, 1.0);\n";
    wrapper += "    ctx.cameraPosition = u_cameraPosition.xyz;\n";
    wrapper += "    ctx.lightVector = vec3(0.0, 1.0, 0.0);\n";
    wrapper += "    ctx.viewSize = u_viewRect.zw;\n";
    wrapper += "    ctx.twoSidedSign = 1.0;\n";
    wrapper += "    ctx.fragmentDepth = 0.0;\n";
    wrapper += "    MaterialSurface surface = EvaluateMaterialGraph(ctx);\n";
    wrapper += "    gl_FragColor = vec4(surface.emissive, 1.0);\n";
    wrapper += "}\n";
    return wrapper;
}

// The fullscreen triangle of the post-process chain (vs_present.sc).
std::string BuildGraphPostProcessVertexWrapperSource() {
    return "$input a_position, a_texcoord0\n$output v_texcoord0\n\n#include <bgfx_shader.sh>\n\nvoid main()\n{\n"
           "    gl_Position = vec4(a_position.xy, 0.0, 1.0);\n    v_texcoord0 = a_texcoord0;\n}\n";
}

} // namespace

std::string BuildGraphFragmentWrapperSource(
    const RenderMaterialGraphShaderSource& shader,
    std::string_view pass) {
    if (pass == kRenderMaterialGraphMotionVectorsPass) {
        return BuildGraphMotionVectorFragmentWrapperSource(shader);
    }
    if (pass == kRenderMaterialGraphPostProcessPass) {
        return BuildGraphPostProcessFragmentWrapperSource(shader);
    }
    const bool shadowPass = pass == "ShadowDepth";
    const bool gbufferPass = pass == "GBuffer";

    std::string wrapper;
    wrapper += "$input v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal" + ExtraUvVaryings(shader) + "\n\n";
    wrapper += "#include <bgfx_shader.sh>\n";
    if (!shadowPass) {
        wrapper += "#include \"pbr_graph_forward.sh\"\n";
        wrapper += "SAMPLER2D(s_terrainLayerWeights, 15);\n";
        wrapper += "uniform vec4 u_terrainLayerParams;\n";
        wrapper += "float KbTerrainLayerOpacity(vec2 terrainUv)\n{\n";
        wrapper += "    if (u_terrainLayerParams.y < 0.5 || u_terrainLayerParams.x < 0.5) return 1.0;\n";
        wrapper += "    vec4 weights = texture2D(s_terrainLayerWeights, terrainUv);\n";
        wrapper += "    if (u_terrainLayerParams.x < 1.5) return weights.y / max(weights.x + weights.y, 0.0001);\n";
        wrapper += "    if (u_terrainLayerParams.x < 2.5) return weights.z / max(weights.x + weights.y + weights.z, 0.0001);\n";
        wrapper += "    return weights.w / max(weights.x + weights.y + weights.z + weights.w, 0.0001);\n}\n";
    }
    if (gbufferPass) {
        wrapper += "#include \"gbuffer_contract.sh\"\n";
    }
    // MAT-72 frame time constants (x=time seconds, y=deltaTime, z=frameIndex). Bound per frame by
    // SceneMeshPassResources so graph Time/animation nodes read real engine time.
    wrapper += "uniform vec4 u_time;\n";
    wrapper += "uniform vec4 u_dynamicParameter;\n";
    wrapper += "uniform vec4 u_materialParams;\n";
    // Positions arrive camera-relative (docs/large_worlds.md); this offset turns them into the world positions the
    // graph reads (the render origin, wrapped to a period that keeps them exact in float).
    wrapper += "uniform vec4 u_renderOriginOffset;\n";
    wrapper += "\n// pass:" + std::string{ pass } + "\n\n";
    wrapper += shader.source;
    wrapper += "\nvoid main()\n{\n";
    const bool decal = MaterialGraphIsDecal(shader);
    AppendMaterialGraphFragmentContext(wrapper, shadowPass, MaterialGraphUsesExtraUvs(shader), decal);
    if (decal) {
        // The decorated surface is lit like the forward pass lights a surface; the decal's opacity blends it in.
        if (shader.reflection.shadingModel == RenderMaterialShadingModel::Unlit) {
            wrapper += "    gl_FragColor = vec4(surface.baseColor.rgb + surface.emissive, surface.alpha);\n";
        } else {
            wrapper += "    vec3 graphNormal = (u_materialParams.z > 0.0) ? normalize(vec3(surface.normal.xy * u_materialParams.z, surface.normal.z)) : vec3(0.0, 0.0, 1.0);\n";
            wrapper += "    vec3 worldNormal = normalize(ctx.tangent * graphNormal.x + ctx.bitangent * graphNormal.y + ctx.normal * graphNormal.z);\n";
            wrapper += "    vec3 lighting = KbEvaluateForwardLighting(worldNormal, kbDecalPos, surface.baseColor.rgb, clamp(surface.metallic, 0.0, 1.0), "
                       "clamp(surface.roughness, 0.04, 1.0), clamp(surface.specular, 0.0, 1.0), clamp(surface.occlusion, 0.0, 1.0), " +
                std::to_string(static_cast<std::uint32_t>(GBufferShadingModelId(shader.reflection.shadingModel))) + ".0, 0.0, 0.0);\n";
            wrapper += "    gl_FragColor = vec4(lighting + surface.emissive, surface.alpha);\n";
        }
        wrapper += "}\n";
        return wrapper;
    }
    const bool writesDepth = MaterialGraphWritesDepth(shader);
    if (shader.reflection.hasPixelDepthOffset && writesDepth) {
        AppendMaterialGraphPixelDepthOffset(wrapper);
    }
    if (shadowPass) {
        wrapper += "    if (surface.alpha < surface.alphaClipThreshold)\n    {\n        discard;\n    }\n";
        wrapper += "    gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n";
    } else {
        wrapper += "    float terrainLayerOpacity = KbTerrainLayerOpacity(v_shadowFlags.zw);\n";
        wrapper += "    if (u_terrainLayerParams.y > 0.5 && terrainLayerOpacity <= 0.0001)\n    {\n        discard;\n    }\n";
        wrapper += "    surface.alpha *= terrainLayerOpacity;\n";
        // The scene colour alpha (SceneColorAlphaPolicy): an opaque or masked surface covers what is behind it,
        // only a translucent one has an opacity; a terrain layer blends by its weight.
        const std::string outputAlpha = writesDepth ? "terrainLayerOpacity" : "surface.alpha";
        // MAT-38 Masked: clip fragments whose alpha is below the clip threshold so the background shows
        // through (binary opacity). The transparent modes keep every fragment and blend at the ROP stage.
        if (shader.reflection.blendMode == RenderMaterialGraphBlendMode::Masked) {
            wrapper += "    if (surface.alpha < surface.alphaClipThreshold)\n    {\n        discard;\n    }\n";
        }
        if (gbufferPass) {
            const auto shadingModelId = static_cast<std::uint32_t>(GBufferShadingModelId(shader.reflection.shadingModel));
            AppendMaterialGraphTangentBasis(wrapper);
            if (shader.reflection.hasTangentOutput) {
                wrapper += "    vec3 materialTangent = basisTangent * surface.tangentOutput.x + basisBitangent * surface.tangentOutput.y + basisNormal * surface.tangentOutput.z;\n";
                wrapper += "    materialTangent = dot(materialTangent, materialTangent) > 0.0001 ? normalize(materialTangent) : basisTangent;\n";
                wrapper += "    basisTangent = normalize(materialTangent - basisNormal * dot(basisNormal, materialTangent));\n";
                wrapper += "    basisTangent = dot(basisTangent, basisTangent) > 0.0001 ? basisTangent : normalize(v_tangent);\n";
                wrapper += "    basisBitangent = normalize(cross(basisNormal, basisTangent) * vertexHandedness);\n";
            }
            wrapper += "    vec3 graphNormal = surface.normal;\n";
            wrapper += "    graphNormal = (u_materialParams.z > 0.0) ? normalize(vec3(graphNormal.xy * u_materialParams.z, graphNormal.z)) : vec3(0.0, 0.0, 1.0);\n";
            wrapper += "    vec3 worldNormal = normalize(basisTangent * graphNormal.x + basisBitangent * graphNormal.y + basisNormal * graphNormal.z);\n";
            // Alpha of the albedo and normal targets: the shading model's CustomData0/1 (gbuffer_contract.sh).
            wrapper += "    gl_FragData[0] = vec4(surface.baseColor.rgb, clamp(surface.clearCoat, 0.0, 1.0));\n";
            wrapper += "    gl_FragData[1] = vec4(worldNormal * 0.5 + 0.5, KbEncodeGBufferCustomData1(surface.clearCoatRoughness));\n";
            wrapper += "    gl_FragData[2] = vec4(clamp(surface.metallic, 0.0, 1.0), clamp(surface.roughness, 0.04, 1.0), clamp(surface.occlusion, 0.0, 1.0), KbEncodeGBufferShadingModel(" + std::to_string(shadingModelId) + ".0));\n";
            wrapper += "    gl_FragData[3] = vec4(surface.emissive, clamp(surface.specular, 0.0, 1.0));\n";
            if (shader.reflection.writesShadingModelColor && shader.reflection.shadingModel == RenderMaterialShadingModel::Eye) {
                // Unreal's Eye: the iris (octahedral world normal, mask, distance) in the fifth target.
                wrapper += "    vec3 kbIrisNormal = normalize(basisTangent * surface.irisNormal.x + basisBitangent * surface.irisNormal.y + basisNormal * surface.irisNormal.z);\n";
                wrapper += "    gl_FragData[4] = vec4(KbOctEncode(kbIrisNormal) * 0.5 + 0.5, clamp(surface.irisMask, 0.0, 1.0), clamp(surface.irisDistance, 0.0, 1.0));\n";
            } else if (shader.reflection.writesShadingModelColor) {
                // Unreal's custom data: the model colour (alpha 1 = given) in the fifth target, which the
                // G-buffer carries in frames that draw such a material.
                wrapper += "    gl_FragData[4] = vec4(clamp(" + std::string{ GraphShadingModelColorField(shader) } +
                    ", vec3_splat(0.0), vec3_splat(1.0)), 1.0);\n";
            }
        } else if (shader.reflection.shadingModel == RenderMaterialShadingModel::Unlit) {
            // MAT-37 Unlit: the surface emissive plus base color go straight to the framebuffer with no lighting.
            if (shader.reflection.hasRefraction) {
                wrapper += "    vec3 worldNormal = normalize(v_normal) * (gl_FrontFacing ? 1.0 : -1.0);\n";
                AppendMaterialGraphRefraction(wrapper, "worldNormal", "surface.baseColor.rgb + surface.emissive");
            } else {
                wrapper += "    gl_FragColor = vec4(surface.baseColor.rgb + surface.emissive, " + outputAlpha + ");\n";
            }
        } else {
            // MAT-37 DefaultLit: the metallic-roughness forward PBR path.
            AppendMaterialGraphTangentBasis(wrapper);
            if (shader.reflection.hasTangentOutput) {
                wrapper += "    vec3 materialTangent = basisTangent * surface.tangentOutput.x + basisBitangent * surface.tangentOutput.y + basisNormal * surface.tangentOutput.z;\n";
                wrapper += "    materialTangent = dot(materialTangent, materialTangent) > 0.0001 ? normalize(materialTangent) : basisTangent;\n";
                wrapper += "    basisTangent = normalize(materialTangent - basisNormal * dot(basisNormal, materialTangent));\n";
                wrapper += "    basisTangent = dot(basisTangent, basisTangent) > 0.0001 ? basisTangent : normalize(v_tangent);\n";
                wrapper += "    basisBitangent = normalize(cross(basisNormal, basisTangent) * vertexHandedness);\n";
            }
            wrapper += "    vec3 graphNormal = surface.normal;\n";
            wrapper += "    graphNormal = (u_materialParams.z > 0.0) ? normalize(vec3(graphNormal.xy * u_materialParams.z, graphNormal.z)) : vec3(0.0, 0.0, 1.0);\n";
            wrapper += "    vec3 worldNormal = normalize(basisTangent * graphNormal.x + basisBitangent * graphNormal.y + basisNormal * graphNormal.z);\n";
            wrapper += "    float metallic = clamp(surface.metallic, 0.0, 1.0);\n";
            wrapper += "    float roughness = clamp(surface.roughness, 0.04, 1.0);\n";
            wrapper += "    float occlusion = clamp(surface.occlusion, 0.0, 1.0);\n";
            wrapper += "    float specular = clamp(surface.specular, 0.0, 1.0);\n";
            const auto shadingModelId = static_cast<std::uint32_t>(GBufferShadingModelId(shader.reflection.shadingModel));
            if (shader.reflection.shadingModel == RenderMaterialShadingModel::Eye) {
                // Unreal's Eye in the forward pass: the iris normal in world space, as the G-buffer stores it.
                wrapper += "    vec3 kbIrisNormal = normalize(basisTangent * surface.irisNormal.x + basisBitangent * surface.irisNormal.y + basisNormal * surface.irisNormal.z);\n";
                wrapper += "    vec3 lighting = KbEvaluateForwardLightingFull(worldNormal, v_worldPos, surface.baseColor.rgb, metallic, roughness, specular, occlusion, " +
                    std::to_string(shadingModelId) + ".0, clamp(surface.clearCoat, 0.0, 1.0), clamp(surface.clearCoatRoughness, 0.0, 1.0), " +
                    "KbDefaultShadingModelColor(" + std::to_string(shadingModelId) + ".0, surface.baseColor.rgb, metallic), kbIrisNormal, " +
                    "vec2(clamp(surface.irisMask, 0.0, 1.0), clamp(surface.irisDistance, 0.0, 1.0)));\n";
            } else if (shader.reflection.writesShadingModelColor) {
                wrapper += "    vec3 lighting = KbEvaluateForwardLightingColor(worldNormal, v_worldPos, surface.baseColor.rgb, metallic, roughness, specular, occlusion, " +
                    std::to_string(shadingModelId) + ".0, clamp(surface.clearCoat, 0.0, 1.0), clamp(surface.clearCoatRoughness, 0.0, 1.0), " +
                    std::string{ GraphShadingModelColorField(shader) } + ");\n";
            } else {
                wrapper += "    vec3 lighting = KbEvaluateForwardLighting(worldNormal, v_worldPos, surface.baseColor.rgb, metallic, roughness, specular, occlusion, " +
                    std::to_string(shadingModelId) + ".0, clamp(surface.clearCoat, 0.0, 1.0), clamp(surface.clearCoatRoughness, 0.0, 1.0));\n";
            }
            if (shader.reflection.shadingModel == RenderMaterialShadingModel::ThinTranslucent) {
                AppendMaterialGraphThinTranslucent(wrapper, shader, std::to_string(shadingModelId) + ".0");
            } else if (shader.reflection.shadingModel == RenderMaterialShadingModel::SingleLayerWater) {
                AppendMaterialGraphSingleLayerWater(wrapper, shader, std::to_string(shadingModelId) + ".0");
            } else if (shader.reflection.hasRefraction) {
                AppendMaterialGraphRefraction(wrapper, "worldNormal", "lighting + surface.emissive");
            } else {
                wrapper += "    gl_FragColor = vec4(lighting + surface.emissive, " + outputAlpha + ");\n";
            }
        }
    }
    wrapper += "}\n";
    return wrapper;
}

namespace {

// The vertex-domain evaluation of a graph for one model and one material time: the instanced mesh vertex
// transform (vs_mesh_instanced.sc) with the graph's vertex outputs applied before projection. Every name it
// declares ends in `suffix`, so the motion vector pass evaluates the previous frame next to the current one.
void AppendGraphVertexEvaluation(std::string& vs, const RenderMaterialGraphShaderSource& shader,
    const std::string& suffix, const std::string& model, const std::string& time, const std::string& deltaTime) {
    const auto name = [&suffix](const char* base) { return std::string{ base } + suffix; };
    vs += "    mat4 " + name("model") + " = " + model + ";\n";
    vs += "    vec4 " + name("worldPos") + " = mul(" + name("model") + ", vec4(a_position, 1.0));\n";
    vs += "    vec3 " + name("objectWorldPos") + " = mul(" + name("model") + ", vec4(0.0, 0.0, 0.0, 1.0)).xyz;\n";
    vs += "    vec3 " + name("objectOrientationRaw") + " = mul(" + name("model") + ", vec4(0.0, 0.0, 1.0, 0.0)).xyz;\n";
    vs += "    vec3 " + name("objectOrientation") + " = dot(" + name("objectOrientationRaw") + ", " + name("objectOrientationRaw") +
        ") > 0.0001 ? normalize(" + name("objectOrientationRaw") + ") : vec3(0.0, 0.0, 1.0);\n";
    vs += "    vec3 " + name("vsNormal") + " = KbModelNormal(" + name("model") + ", a_normal);\n";
    vs += "    vec3 " + name("vsTangent") + " = mul(" + name("model") + ", vec4(a_tangent.xyz, 0.0)).xyz;\n";
    vs += "    vec3 " + name("fallbackAxis") + " = abs(" + name("vsNormal") + ".y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n";
    vs += "    " + name("vsTangent") + " = dot(" + name("vsTangent") + ", " + name("vsTangent") + ") > 0.0001 ? normalize(" +
        name("vsTangent") + ") : normalize(cross(" + name("fallbackAxis") + ", " + name("vsNormal") + "));\n";
    vs += "    " + name("vsTangent") + " = normalize(" + name("vsTangent") + " - " + name("vsNormal") + " * dot(" + name("vsNormal") +
        ", " + name("vsTangent") + "));\n";
    vs += "    float " + name("handedness") + " = abs(a_tangent.w) > 0.0001 ? a_tangent.w : 1.0;\n";
    vs += "    vec3 " + name("vsBitangent") + " = normalize(cross(" + name("vsNormal") + ", " + name("vsTangent") + ") * " + name("handedness") + ");\n";
    vs += "    vec3 " + name("preSkinnedNormal") + " = dot(a_normal, a_normal) > 0.0001 ? normalize(a_normal) : vec3(0.0, 0.0, 1.0);\n";
    const std::string ctx = name("ctx");
    vs += "    MaterialGraphContext " + ctx + ";\n";
    vs += "    " + ctx + ".uv0 = a_texcoord0;\n";
    vs += "    " + ctx + ".uv1 = a_texcoord1;\n";
    vs += MaterialGraphUsesExtraUvs(shader) && !MaterialGraphIsDecal(shader)
        ? "    " + ctx + ".uv23 = a_color1;\n    " + ctx + ".uv45 = a_color2;\n    " + ctx + ".uv67 = a_color3;\n"
        : "    " + ctx + ".uv23 = vec4_splat(0.0);\n    " + ctx + ".uv45 = vec4_splat(0.0);\n    " + ctx + ".uv67 = vec4_splat(0.0);\n";
    vs += "    " + ctx + ".normal = " + name("vsNormal") + ";\n";
    vs += "    " + ctx + ".tangent = " + name("vsTangent") + ";\n";
    vs += "    " + ctx + ".bitangent = " + name("vsBitangent") + ";\n";
    vs += "    " + ctx + ".worldPos = " + name("worldPos") + ".xyz + u_renderOriginOffset.xyz;\n";
    vs += "    " + ctx + ".viewDir = vec3(0.0, 0.0, 1.0);\n";
    vs += "    " + ctx + ".vertexColor = a_color0 * vec4(i_data4.rgb, abs(i_data4.w));\n";
    vs += "    " + ctx + ".time = " + time + ";\n";
    vs += "    " + ctx + ".deltaTime = " + deltaTime + ";\n";
    vs += "    " + ctx + ".dynamicParameter = u_dynamicParameter;\n";
    vs += "    " + ctx + ".screenPosition = vec2(0.0, 0.0);\n";
    vs += "    " + ctx + ".localPosition = a_position;\n";
    vs += "    " + ctx + ".objectPosition = " + name("objectWorldPos") + " + u_renderOriginOffset.xyz;\n";
    vs += "    " + ctx + ".perInstanceRandom = instanceRandom;\n";
    vs += "    " + ctx + ".objectRadius = instanceRadius;\n";
    vs += "    " + ctx + ".perInstanceFadeAmount = instanceFadeAmount;\n";
    vs += "    " + ctx + ".perInstanceCustomData = instanceCustomData;\n";
    vs += "    " + ctx + ".objectBounds = vec4(" + name("objectWorldPos") + " + u_renderOriginOffset.xyz, max(instanceRadius, 0.0));\n";
    vs += "    " + ctx + ".objectOrientation = " + name("objectOrientation") + ";\n";
    vs += "    " + ctx + ".preSkinnedPosition = a_position;\n";
    vs += "    " + ctx + ".preSkinnedNormal = " + name("preSkinnedNormal") + ";\n";
    vs += "    " + ctx + ".cameraPosition = vec3(0.0, 0.0, 0.0);\n";
    vs += "    " + ctx + ".lightVector = vec3(0.0, 1.0, 0.0);\n";
    vs += "    " + ctx + ".viewSize = vec2(0.0, 0.0);\n";
    vs += "    " + ctx + ".twoSidedSign = 1.0;\n";
    vs += "    " + ctx + ".fragmentDepth = 0.0;\n";
    vs += "    vec2 " + name("materialUv0") + " = a_texcoord0;\n";
    if (shader.reflection.hasCustomizedUv0) {
        vs += "    " + name("materialUv0") + " = EvaluateCustomizedUv0(" + ctx + ");\n";
        vs += "    " + ctx + ".uv0 = " + name("materialUv0") + ";\n";
    }
    if (shader.reflection.hasWorldPositionOffset) {
        vs += "    " + name("worldPos") + ".xyz += EvaluateWorldPositionOffset(" + ctx + ");\n";
    }
    if (shader.reflection.hasDisplacement) {
        vs += "    " + name("worldPos") + ".xyz += EvaluateDisplacement(" + ctx + ");\n";
    }
}

} // namespace

std::string BuildGraphVertexWrapperSource(const RenderMaterialGraphShaderSource& shader, std::string_view pass) {
    if (pass == kRenderMaterialGraphPostProcessPass) {
        return BuildGraphPostProcessVertexWrapperSource();
    }
    // MAT-67/#54: the instanced mesh vertex shader with graph vertex-domain outputs applied before
    // projection/rasterization. WPO and displacement move real geometry; CustomizedUV0 rewrites the
    // interpolated UV that fragment TextureSample nodes read. Mirrors vs_mesh_instanced.sc.
    // The motion vector pass evaluates the graph twice, like Unreal's velocity pass runs the material's
    // vertex factory with the previous frame's parameters: at this frame's material time with the current
    // model, and at the previous frame's time with the previous model (motion_vectors.sh).
    const bool motionVectors = pass == kRenderMaterialGraphMotionVectorsPass;
    std::string vs;
    const bool extraUvs = MaterialGraphUsesExtraUvs(shader) && !MaterialGraphIsDecal(shader);
    vs += std::string{ "$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_color0" } +
        (extraUvs ? ", a_color1, a_color2, a_color3" : "") + ", i_data0, i_data1, i_data2, i_data3, i_data4\n";
    if (motionVectors) {
        vs += "$output v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal, v_currentClip, v_previousClip" + ExtraUvVaryings(shader) + "\n\n";
        vs += "#include <bgfx_shader.sh>\n";
        vs += "#include \"motion_vectors.sh\"\n";
        vs += "#include \"model_normal.sh\"\n";
        vs += "SAMPLER2D(s_motionPreviousModels, 15);\n";
        // x: the material time of the previous frame, y: its delta time.
        vs += "uniform vec4 u_motionPreviousTime;\n";
    } else {
        vs += "$output v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent, v_objectLocalPos, v_objectWorldPos, v_objectOrientation, v_preSkinnedNormal" + ExtraUvVaryings(shader) + "\n\n";
        vs += "#include <bgfx_shader.sh>\n";
        vs += "#include \"model_normal.sh\"\n";
        vs += "uniform mat4 u_shadowViewProj;\n";
    }
    vs += "uniform vec4 u_time;\n";
    vs += "uniform vec4 u_dynamicParameter;\n";
    vs += "uniform vec4 u_renderOriginOffset;\n\n";
    vs += shader.source;
    vs += "\nvoid main()\n{\n";
    vs += "    float instanceRandom = i_data0.w;\n";
    vs += "    float instanceRadius = i_data1.w;\n";
    vs += "    float instanceFadeAmount = i_data2.w;\n";
    vs += "    float instanceCustomData = i_data3.w;\n";
    vs += "    vec4 modelColumn0 = vec4(i_data0.xyz, 0.0);\n";
    vs += "    vec4 modelColumn1 = vec4(i_data1.xyz, 0.0);\n";
    vs += "    vec4 modelColumn2 = vec4(i_data2.xyz, 0.0);\n";
    vs += "    vec4 modelColumn3 = vec4(i_data3.xyz, 1.0);\n";
    AppendGraphVertexEvaluation(vs, shader, "", "mtxFromCols(modelColumn0, modelColumn1, modelColumn2, modelColumn3)", "u_time.x", "u_time.y");
    vs += "    gl_Position = mul(u_viewProj, worldPos);\n";
    if (motionVectors) {
        AppendGraphVertexEvaluation(vs, shader, "Previous",
            "KbMotionPreviousModel(s_motionPreviousModels, float(gl_InstanceID), modelColumn0, modelColumn1, modelColumn2, modelColumn3)",
            "u_motionPreviousTime.x", "u_motionPreviousTime.y");
        vs += "    v_currentClip = mul(u_motionCurrentViewProjection, worldPos);\n";
        vs += "    v_previousClip = mul(u_motionPreviousViewProjection, worldPosPrevious);\n";
    } else {
        vs += "    v_shadowPos = mul(u_shadowViewProj, worldPos);\n";
    }
    vs += "    v_worldPos = worldPos.xyz;\n";
    vs += "    v_objectLocalPos = vec4(a_position, instanceRandom);\n";
    vs += "    v_objectWorldPos = vec4(objectWorldPos, instanceRadius);\n";
    vs += "    v_objectOrientation = vec4(objectOrientation, instanceCustomData);\n";
    vs += "    v_shadowFlags = vec4(i_data4.w >= 0.0 ? 1.0 : 0.0, instanceFadeAmount, a_texcoord1.x, a_texcoord1.y);\n";
    vs += "    v_normal = vsNormal;\n";
    vs += "    v_tangent = vsTangent;\n";
    vs += "    v_bitangent = vsBitangent;\n";
    vs += "    v_texcoord0 = materialUv0;\n";
    vs += "    v_color0 = ctx.vertexColor;\n";
    vs += "    v_preSkinnedNormal = preSkinnedNormal;\n";
    if (MaterialGraphIsDecal(shader)) {
        // The rows of the box's inverse transform: the cofactors of the model's columns over its determinant.
        vs += "    vec3 kbDecalRow0 = cross(modelColumn1.xyz, modelColumn2.xyz);\n";
        vs += "    vec3 kbDecalRow1 = cross(modelColumn2.xyz, modelColumn0.xyz);\n";
        vs += "    vec3 kbDecalRow2 = cross(modelColumn0.xyz, modelColumn1.xyz);\n";
        vs += "    float kbDecalInvDet = 1.0 / dot(modelColumn0.xyz, kbDecalRow0);\n";
        vs += "    v_uv23 = vec4(kbDecalRow0, -dot(kbDecalRow0, modelColumn3.xyz)) * kbDecalInvDet;\n";
        vs += "    v_uv45 = vec4(kbDecalRow1, -dot(kbDecalRow1, modelColumn3.xyz)) * kbDecalInvDet;\n";
        vs += "    v_uv67 = vec4(kbDecalRow2, -dot(kbDecalRow2, modelColumn3.xyz)) * kbDecalInvDet;\n";
    } else if (extraUvs) {
        vs += "    v_uv23 = a_color1;\n    v_uv45 = a_color2;\n    v_uv67 = a_color3;\n";
    }
    vs += "}\n";
    return vs;
}

std::uint64_t ComputeRenderMaterialGraphReflectionHash(const RenderMaterialGraphReflection& reflection) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    HashU64(hash, reflection.uniforms.size());
    for (const RenderMaterialGraphReflectionUniform& uniform : reflection.uniforms) {
        HashString64(hash, uniform.name);
        HashString64(hash, uniform.stableId);
        HashU64(hash, static_cast<std::uint64_t>(uniform.kind));
        HashU64(hash, static_cast<std::uint64_t>(uniform.source));
        HashU64(hash, uniform.collectionAssetId);
        HashString64(hash, uniform.collectionParameterStableId);
        // Dynamic parameter defaults are binding data, not shader/program identity. Changing a
        // default must update uploaded uniforms without creating a new GPU permutation.
    }
    HashU64(hash, reflection.textures.size());
    for (const RenderMaterialGraphReflectionTexture& texture : reflection.textures) {
        HashString64(hash, texture.samplerName);
        HashString64(hash, texture.stableId);
        HashString64(hash, texture.role);
        HashU64(hash, texture.slot);
        HashU64(hash, static_cast<std::uint64_t>(texture.colorSpace));
        HashU64(hash, static_cast<std::uint64_t>(texture.samplerState.minFilter));
        HashU64(hash, static_cast<std::uint64_t>(texture.samplerState.magFilter));
        HashU64(hash, static_cast<std::uint64_t>(texture.samplerState.mipFilter));
        HashU64(hash, static_cast<std::uint64_t>(texture.samplerState.wrapU));
        HashU64(hash, static_cast<std::uint64_t>(texture.samplerState.wrapV));
        HashU64(hash, static_cast<std::uint64_t>(texture.dimension));
    }
    HashU64(hash, reflection.requiredVaryings.size());
    for (const std::string& varying : reflection.requiredVaryings) {
        HashString64(hash, varying);
    }
    HashU64(hash, reflection.hasWorldPositionOffset ? 1U : 0U);
    HashU64(hash, reflection.hasCustomizedUv0 ? 1U : 0U);
    HashU64(hash, reflection.hasDisplacement ? 1U : 0U);
    HashU64(hash, reflection.hasTangentOutput ? 1U : 0U);
    HashU64(hash, reflection.hasPixelDepthOffset ? 1U : 0U);
    HashU64(hash, reflection.hasRefraction ? 1U : 0U);
    HashU64(hash, reflection.writesShadingModelColor ? 1U : 0U);
    // MAT-37: the shading model selects the fragment wrapper lighting branch, so it is part of program identity.
    HashU64(hash, static_cast<std::uint64_t>(reflection.shadingModel));
    HashU64(hash, static_cast<std::uint64_t>(reflection.domain));
    // MAT-38: the blend mode changes the wrapper (masked clip) and the cooked pass, so it is part of identity.
    HashU64(hash, static_cast<std::uint64_t>(reflection.blendMode));
    HashU64(hash, reflection.usesSceneDepth ? 1U : 0U);
    HashU64(hash, reflection.usesSceneColor ? 1U : 0U);
    return hash;
}

std::uint64_t ComputeRenderMaterialGraphVariantKey(const RenderMaterialGraphShaderSource& shader) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    HashU64(hash, shader.sourceHash);
    HashU64(hash, kRenderMaterialGraphShaderWrapperVersion);
    HashU64(hash, ComputeRenderMaterialGraphReflectionHash(shader.reflection));
    HashString64(hash, shader.entryPoint);
    return hash;
}

RenderMaterialGraphShaderArtifactResult CookRenderMaterialGraphShaderArtifact(
    const RenderMaterialGraphShaderSource& shader,
    std::span<const RenderMaterialGraphShaderBackend> backends,
    const RenderMaterialGraphShaderArtifactRequest& request) {
    RenderMaterialGraphShaderArtifactResult result{};

    if (request.shadercPath.empty() || request.cacheRoot.empty() || !request.shaderPlatform.has_value()) {
        AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
            "Material graph shader cook requires a shaderc tool path, cache root and explicit target platform.");
        return result;
    }
    if (!RenderMaterialGraphCooksScenePass(shader.reflection, request.pass)) {
        AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
            "Material graph shader cook refused pass '" + request.pass + "': the graph neither moves nor clips its "
            "surface (no world position offset, displacement, pixel depth offset or Masked blend mode), so the "
            "motion vector pass draws it with the builtin program.",
            request.pass);
        return result;
    }
    for (const RenderMaterialGraphShaderBackend backend : backends) {
        const std::optional<kb::assets::bake::ShaderBakeBackend> bakeBackend = ToBakeShaderBackend(backend);
        if (!bakeBackend.has_value() ||
            !kb::assets::bake::ShaderBakePlatformSupportsBackend(*request.shaderPlatform, *bakeBackend)) {
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader backend '" + std::string{ RenderMaterialGraphShaderBackendName(backend) } +
                    "' is not valid for shader platform '" +
                    std::string{ kb::assets::bake::ShaderBakePlatformName(*request.shaderPlatform) } + "'.");
            return result;
        }
    }

    RenderMaterialGraphShaderArtifact artifact{};
    artifact.graphSourceHash = shader.sourceHash;
    artifact.variantKey = ComputeRenderMaterialGraphVariantKey(shader);
    artifact.pass = request.pass;
    artifact.entryPoint = shader.entryPoint;
    artifact.graphGenerated = true;
    artifact.shaderPlatform = *request.shaderPlatform;
    artifact.wrapperSource = BuildGraphFragmentWrapperSource(shader, request.pass);
    std::uint64_t wrapperHash = 1469598103934665603ULL;
    HashString64(wrapperHash, artifact.wrapperSource);
    artifact.wrapperHash = wrapperHash;
    artifact.reflectionHash = ComputeRenderMaterialGraphReflectionHash(shader.reflection);
    artifact.materialTypeVersion = request.materialTypeVersion;

    // Dependency graph: the wrapper includes (e.g. the shared PBR library, varying definitions) and those
    // of the generated vertex shader contribute to the artifact identity so editing them invalidates
    // cooked binaries, and they are what the cook compiles against.
    const bool cooksVertexShader =
        RenderMaterialGraphRequiresGeneratedVertexShader(shader.reflection) ||
        MaterialGraphUsesExtraUvs(shader) ||
        request.pass == kRenderMaterialGraphMotionVectorsPass ||
        request.pass == kRenderMaterialGraphPostProcessPass;
    const std::string vertexWrapperSource = cooksVertexShader ? BuildGraphVertexWrapperSource(shader, request.pass) : std::string{};
    std::vector<ResolvedShaderDependency> dependencies;
    if (!CollectShaderDependencies(artifact.wrapperSource + vertexWrapperSource, request, dependencies, result.diagnostics)) {
        return result;
    }
    std::uint64_t dependencyHash = 1469598103934665603ULL;
    // Resolution order is compiler input: two include roots containing the same logical
    // name may resolve to different files. Keep both the ordered roots and the resolved
    // canonical path in the identity instead of hashing only logical name/content.
    HashU64(dependencyHash, request.includeDirs.size());
    for (std::size_t index = 0U; index < request.includeDirs.size(); ++index) {
        std::error_code canonicalError;
        std::filesystem::path includePath = std::filesystem::weakly_canonical(request.includeDirs[index], canonicalError);
        if (canonicalError) {
            includePath = std::filesystem::path{ request.includeDirs[index] }.lexically_normal();
        }
        HashU64(dependencyHash, index);
        HashString64(dependencyHash, includePath.generic_string());
        artifact.dependencies.push_back(RenderMaterialGraphArtifactDependency{
            .name = "include-dir[" + std::to_string(index) + "]:" + includePath.generic_string(),
            .contentHash = 0U,
        });
    }

    std::error_code shadercCanonicalError;
    std::filesystem::path shadercPath = std::filesystem::weakly_canonical(request.shadercPath, shadercCanonicalError);
    if (shadercCanonicalError) {
        shadercPath = std::filesystem::path{ request.shadercPath }.lexically_normal();
    }
    std::string shadercBytes;
    if (!ReadTextFileStrict(shadercPath, shadercBytes)) {
        AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
            "Material graph shader compiler identity could not be read: " + shadercPath.generic_string() + ".");
        return result;
    }
    std::uint64_t shadercHash = 1469598103934665603ULL;
    HashString64(shadercHash, shadercBytes);
    HashString64(dependencyHash, shadercPath.generic_string());
    HashU64(dependencyHash, shadercHash);
    artifact.dependencies.push_back(RenderMaterialGraphArtifactDependency{
        .name = "shaderc:" + shadercPath.generic_string(),
        .contentHash = shadercHash,
    });
    const std::array<std::filesystem::path, 2U> shadercRuntimeCandidates{
        shadercPath.parent_path() / "dxcompiler.dll",
        shadercPath.parent_path() / "libdxcompiler.so",
    };
    for (const std::filesystem::path& runtimeCandidate : shadercRuntimeCandidates) {
        std::error_code runtimeError;
        if (!std::filesystem::is_regular_file(runtimeCandidate, runtimeError) || runtimeError) continue;
        std::error_code canonicalError;
        const std::filesystem::path canonicalRuntime =
            std::filesystem::weakly_canonical(runtimeCandidate, canonicalError);
        std::string runtimeBytes;
        if (canonicalError || !ReadTextFileStrict(canonicalRuntime, runtimeBytes)) {
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader compiler runtime identity could not be read: " +
                    runtimeCandidate.generic_string() + ".");
            return result;
        }
        dependencies.push_back(ResolvedShaderDependency{
            .path = canonicalRuntime,
            .name = "shaderc-runtime:" + canonicalRuntime.filename().generic_string(),
            .content = std::move(runtimeBytes),
        });
    }

    HashU64(dependencyHash, dependencies.size());
    for (const ResolvedShaderDependency& dependency : dependencies) {
        std::uint64_t contentHash = 1469598103934665603ULL;
        HashString64(contentHash, dependency.content);
        HashString64(dependencyHash, dependency.name);
        HashString64(dependencyHash, dependency.path.generic_string());
        HashU64(dependencyHash, contentHash);
        artifact.dependencies.push_back(RenderMaterialGraphArtifactDependency{
            .name = dependency.name,
            .contentHash = contentHash,
        });
    }
    artifact.dependencyHash = dependencyHash;

    std::uint64_t artifactHash = 1469598103934665603ULL;
    HashU64(artifactHash, artifact.graphSourceHash);
    HashU64(artifactHash, kRenderMaterialGraphShaderWrapperVersion);
    HashU64(artifactHash, artifact.variantKey);
    HashU64(artifactHash, artifact.wrapperHash);
    HashU64(artifactHash, artifact.dependencyHash);
    HashU64(artifactHash, artifact.reflectionHash);
    HashU64(artifactHash, artifact.materialTypeVersion);
    HashU64(artifactHash, static_cast<std::uint64_t>(artifact.shaderPlatform));
    artifact.artifactHash = artifactHash;

    // The per-binary cache key combines the wrapper, its dependencies and the material type version,
    // so any of those changing forces a recompile rather than serving a stale binary.
    std::uint64_t cookKey = 1469598103934665603ULL;
    HashU64(cookKey, kRenderMaterialGraphShaderWrapperVersion);
    HashU64(cookKey, artifact.wrapperHash);
    HashU64(cookKey, artifact.dependencyHash);
    HashU64(cookKey, artifact.materialTypeVersion);
    HashU64(cookKey, static_cast<std::uint64_t>(artifact.shaderPlatform));
    HashU64(cookKey, request.debug ? 1U : 0U);

    std::error_code error;
    // The cache root is the user's project directory and everything below it is built from hashes,
    // so the entries reach past MAX_PATH on a deep project. Addressing the root in extended-length
    // form once makes every path derived from it - directories, binaries, markers, locks - one the
    // operating system will open, and only the paths that outlive the cook are turned back.
    const std::filesystem::path passRoot = kb::platform::ExtendedLengthPath(request.cacheRoot) /
        std::string{ kb::assets::bake::ShaderBakePlatformName(artifact.shaderPlatform) } /
        ("graph_" + std::to_string(shader.sourceHash)) /
        ("variant_" + std::to_string(artifact.variantKey)) /
        request.pass;
    std::filesystem::create_directories(passRoot, error);
    if (error) {
        AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
            "Material graph shader cache directory could not be created: " + kb::platform::PortablePath(passRoot).generic_string() + ".");
        return result;
    }
    std::filesystem::path inputMirrorRoot;
    std::unique_ptr<ScopedShaderInputDirectory> inputMirrorCleanup;
    std::vector<std::string> capturedIncludeDirs;
    std::filesystem::path capturedVaryingDefPath;
    const auto ensureInputMirror = [&]() {
        if (inputMirrorCleanup != nullptr) return true;
        error.clear();
        inputMirrorRoot = ShaderCompilerScratchPath("work", error);
        if (error) return false;
        if (!std::filesystem::create_directory(inputMirrorRoot, error) || error ||
            !BuildCapturedShaderInputMirror(
                request,
                dependencies,
                inputMirrorRoot,
                capturedIncludeDirs,
                capturedVaryingDefPath)) {
            std::filesystem::remove_all(inputMirrorRoot, error);
            return false;
        }
        inputMirrorCleanup = std::make_unique<ScopedShaderInputDirectory>(inputMirrorRoot);
        return true;
    };
    std::filesystem::path wrapperPath;
    bool fragmentWrapperCaptured = false;

    for (const RenderMaterialGraphShaderBackend backend : backends) {
        const std::filesystem::path backendDir = passRoot / std::string{ RenderMaterialGraphShaderBackendDirectory(backend) };
        error.clear();
        std::filesystem::create_directories(backendDir, error);
        if (error) {
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader backend cache directory could not be created: " + kb::platform::PortablePath(backendDir).generic_string() + ".");
            continue;
        }
        const std::filesystem::path binaryPath =
            backendDir / ("fs_" + std::to_string(cookKey) + ".bin");
        const std::filesystem::path markerPath = binaryPath.string() + ".meta";
        ShaderCacheFileLock cacheLock;
        if (!cacheLock.Acquire(binaryPath.string() + ".lock")) {
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader cache entry could not be locked for publication: " +
                    kb::platform::PortablePath(binaryPath).generic_string() + ".lock (" + cacheLock.FailureDetail() + ").",
                request.pass,
                std::string{ RenderMaterialGraphShaderBackendName(backend) });
            continue;
        }

        ShaderCacheFileIdentity cachedIdentity{};
        if (ShaderCacheEntryIsValid(binaryPath, markerPath, cookKey, &cachedIdentity) &&
            ShaderCompilerInputsUnchanged(dependencies, shadercPath, shadercBytes)) {
            artifact.binaries.push_back(RenderMaterialGraphShaderBinary{
                .backend = backend,
                .binaryPath = kb::platform::PortablePath(binaryPath).generic_string(),
                .byteSize = cachedIdentity.bytes,
                .cacheHit = true,
            });
            continue;
        }
        if (!ensureInputMirror()) {
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader compiler inputs could not be captured into an immutable snapshot.");
            continue;
        }
        if (!fragmentWrapperCaptured) {
            wrapperPath = inputMirrorRoot / ("fs_graph_" + std::to_string(cookKey) + ".sc");
            if (!WriteShaderTextAtomically(wrapperPath, artifact.wrapperSource)) {
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph shader cook could not write its captured fragment wrapper.");
                continue;
            }
            fragmentWrapperCaptured = true;
        }

        error.clear();
        const std::filesystem::path stagingBinary = ShaderCompilerScratchPath("compile.tmp", error);
        const std::filesystem::path errorPath = ShaderCompilerScratchPath("shaderc.tmp", error);
        if (error) {
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader compiler scratch directory could not be created: " + error.message() + ".",
                request.pass,
                std::string{ RenderMaterialGraphShaderBackendName(backend) });
            continue;
        }
        std::vector<std::string> arguments{
            "--type", "fragment",
            "--platform", std::string{ kb::assets::bake::ShaderBakePlatformShadercToken(artifact.shaderPlatform) },
            "--profile", std::string{ RenderMaterialGraphShaderBackendProfile(backend) },
            "-f", wrapperPath.generic_string(),
            "-o", stagingBinary.generic_string(),
        };
        if (!capturedVaryingDefPath.empty()) {
            arguments.emplace_back("--varyingdef");
            arguments.push_back(capturedVaryingDefPath.generic_string());
        }
        for (const std::string& includeDir : capturedIncludeDirs) {
            arguments.emplace_back("-i");
            arguments.push_back(includeDir);
        }
        arguments.emplace_back("-O");
        arguments.emplace_back(request.debug ? "0" : "3");
        if (request.debug) {
            arguments.emplace_back("--debug");
        }

        const int exitCode = RunShaderCompilerHidden(shadercPath, arguments, errorPath);

        const bool produced = IsNonEmptyRegularFile(stagingBinary);
        if (exitCode != 0 || !produced) {
            std::string log = TrimDiagnosticText(ReadTextFile(errorPath));
            std::filesystem::remove(errorPath, error);
            std::filesystem::remove(stagingBinary, error);
            error.clear();
            if (log.empty()) {
                log = "shaderc exited with code " + std::to_string(exitCode) + " without producing a binary.";
            }
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader cook failed for backend '" + std::string{ RenderMaterialGraphShaderBackendName(backend) } +
                "' (pass '" + request.pass + "'): " + log,
                request.pass,
                std::string{ RenderMaterialGraphShaderBackendName(backend) });
            continue;
        }
        std::filesystem::remove(errorPath, error);
        error.clear();
        if (!ShaderCompilerInputsUnchanged(dependencies, shadercPath, shadercBytes)) {
            std::filesystem::remove(stagingBinary, error);
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader compiler inputs changed while cooking; no cache artifact was published.",
                request.pass,
                std::string{ RenderMaterialGraphShaderBackendName(backend) });
            continue;
        }
        ShaderCacheFileIdentity compiledIdentity{};
        if (!ReadShaderCacheFileIdentity(stagingBinary, compiledIdentity)) {
            std::filesystem::remove(stagingBinary, error);
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader compiler produced an unreadable binary cache artifact.",
                request.pass,
                std::string{ RenderMaterialGraphShaderBackendName(backend) });
            continue;
        }
        std::string publishFailure;
        if (!PublishCompiledShaderBinary(stagingBinary, binaryPath, publishFailure)) {
            std::filesystem::remove(stagingBinary, error);
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader binary could not be published atomically into " +
                    kb::platform::PortablePath(binaryPath).generic_string() + ": " + publishFailure + ".",
                request.pass,
                std::string{ RenderMaterialGraphShaderBackendName(backend) });
            continue;
        }
        if (!WriteShaderTextAtomically(markerPath, ShaderCacheMarker(cookKey, compiledIdentity)) ||
            !ShaderCacheEntryIsValid(binaryPath, markerPath, cookKey, &compiledIdentity)) {
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader binary cache integrity marker could not be published or verified.",
                request.pass,
                std::string{ RenderMaterialGraphShaderBackendName(backend) });
            continue;
        }
        artifact.binaries.push_back(RenderMaterialGraphShaderBinary{
            .backend = backend,
            .binaryPath = kb::platform::PortablePath(binaryPath).generic_string(),
            .byteSize = compiledIdentity.bytes,
            .cacheHit = false,
        });
    }

    if (artifact.binaries.size() != backends.size()) {
        AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
            "Material graph shader cook did not produce every requested backend binary.");
        return result;
    }

    // MAT-67/#54: when the graph drives vertex-domain outputs, cook the generated vertex shader too so the
    // scene program pairs it with the graph fragment shader and moves geometry / rewrites UVs before
    // rasterization. ShadowDepth uses the same generated vertex-domain contract for WPO/displacement.
    if (cooksVertexShader) {
        artifact.hasVertexShader = true;
        artifact.vertexWrapperSource = vertexWrapperSource;

        std::uint64_t vsCookKey = 1469598103934665603ULL;
        HashString64(vsCookKey, artifact.vertexWrapperSource);
        HashU64(vsCookKey, artifact.dependencyHash);
        HashU64(vsCookKey, artifact.materialTypeVersion);
        HashU64(vsCookKey, static_cast<std::uint64_t>(artifact.shaderPlatform));
        HashU64(vsCookKey, request.debug ? 1U : 0U);

        std::filesystem::path vsWrapperPath;
        bool vertexWrapperCaptured = false;

        for (const RenderMaterialGraphShaderBackend backend : backends) {
            const std::filesystem::path backendDir = passRoot / std::string{ RenderMaterialGraphShaderBackendDirectory(backend) };
            error.clear();
            std::filesystem::create_directories(backendDir, error);
            if (error) {
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph vertex shader backend cache directory could not be created: " +
                        kb::platform::PortablePath(backendDir).generic_string() + ".");
                continue;
            }
            const std::filesystem::path vsBinaryPath =
                backendDir / ("vs_" + std::to_string(vsCookKey) + ".bin");
            const std::filesystem::path vsMarkerPath = vsBinaryPath.string() + ".meta";
            ShaderCacheFileLock vsCacheLock;
            if (!vsCacheLock.Acquire(vsBinaryPath.string() + ".lock")) {
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph vertex shader cache entry could not be locked for publication: " +
                        kb::platform::PortablePath(vsBinaryPath).generic_string() + ".lock (" + vsCacheLock.FailureDetail() + ").",
                    request.pass,
                    std::string{ RenderMaterialGraphShaderBackendName(backend) });
                continue;
            }

            ShaderCacheFileIdentity cachedVsIdentity{};
            if (ShaderCacheEntryIsValid(
                    vsBinaryPath, vsMarkerPath, vsCookKey, &cachedVsIdentity) &&
                ShaderCompilerInputsUnchanged(dependencies, shadercPath, shadercBytes)) {
                artifact.vertexBinaries.push_back(RenderMaterialGraphShaderBinary{
                    .backend = backend,
                    .binaryPath = kb::platform::PortablePath(vsBinaryPath).generic_string(),
                    .byteSize = cachedVsIdentity.bytes,
                    .cacheHit = true,
                });
                continue;
            }
            if (!ensureInputMirror()) {
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph vertex shader compiler inputs could not be captured into an immutable snapshot.");
                continue;
            }
            if (!vertexWrapperCaptured) {
                vsWrapperPath = inputMirrorRoot / ("vs_graph_" + std::to_string(vsCookKey) + ".sc");
                if (!WriteShaderTextAtomically(vsWrapperPath, artifact.vertexWrapperSource)) {
                    AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                        "Material graph shader cook could not write its captured vertex wrapper.");
                    continue;
                }
                vertexWrapperCaptured = true;
            }

            error.clear();
            const std::filesystem::path stagingVsBinary = ShaderCompilerScratchPath("compile.tmp", error);
            const std::filesystem::path vsErrorPath = ShaderCompilerScratchPath("shaderc.tmp", error);
            if (error) {
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph shader compiler scratch directory could not be created: " + error.message() + ".",
                    request.pass,
                    std::string{ RenderMaterialGraphShaderBackendName(backend) });
                continue;
            }
            std::vector<std::string> arguments{
                "--type", "vertex",
                "--platform", std::string{ kb::assets::bake::ShaderBakePlatformShadercToken(artifact.shaderPlatform) },
                "--profile", std::string{ RenderMaterialGraphShaderBackendProfile(backend) },
                "-f", vsWrapperPath.generic_string(),
                "-o", stagingVsBinary.generic_string(),
            };
            if (!capturedVaryingDefPath.empty()) {
                arguments.emplace_back("--varyingdef");
                arguments.push_back(capturedVaryingDefPath.generic_string());
            }
            for (const std::string& includeDir : capturedIncludeDirs) {
                arguments.emplace_back("-i");
                arguments.push_back(includeDir);
            }
            arguments.emplace_back("-O");
            arguments.emplace_back(request.debug ? "0" : "3");
            if (request.debug) {
                arguments.emplace_back("--debug");
            }

            const int exitCode = RunShaderCompilerHidden(shadercPath, arguments, vsErrorPath);

            const bool produced = IsNonEmptyRegularFile(stagingVsBinary);
            if (exitCode != 0 || !produced) {
                std::string log = TrimDiagnosticText(ReadTextFile(vsErrorPath));
                std::filesystem::remove(vsErrorPath, error);
                std::filesystem::remove(stagingVsBinary, error);
                error.clear();
                if (log.empty()) {
                    log = "shaderc exited with code " + std::to_string(exitCode) + " without producing a vertex binary.";
                }
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph vertex shader cook failed for backend '" + std::string{ RenderMaterialGraphShaderBackendName(backend) } +
                    "' (pass '" + request.pass + "'): " + log,
                    request.pass,
                    std::string{ RenderMaterialGraphShaderBackendName(backend) });
                return result;
            }
            std::filesystem::remove(vsErrorPath, error);
            error.clear();
            if (!ShaderCompilerInputsUnchanged(dependencies, shadercPath, shadercBytes)) {
                std::filesystem::remove(stagingVsBinary, error);
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph vertex shader compiler inputs changed while cooking; no cache artifact was published.",
                    request.pass,
                    std::string{ RenderMaterialGraphShaderBackendName(backend) });
                continue;
            }
            ShaderCacheFileIdentity compiledVsIdentity{};
            if (!ReadShaderCacheFileIdentity(stagingVsBinary, compiledVsIdentity)) {
                std::filesystem::remove(stagingVsBinary, error);
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph vertex shader compiler produced an unreadable binary cache artifact.",
                    request.pass,
                    std::string{ RenderMaterialGraphShaderBackendName(backend) });
                continue;
            }
            std::string vsPublishFailure;
            if (!PublishCompiledShaderBinary(stagingVsBinary, vsBinaryPath, vsPublishFailure)) {
                std::filesystem::remove(stagingVsBinary, error);
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph vertex shader binary could not be published atomically into " +
                        kb::platform::PortablePath(vsBinaryPath).generic_string() + ": " + vsPublishFailure + ".",
                    request.pass,
                    std::string{ RenderMaterialGraphShaderBackendName(backend) });
                continue;
            }
            if (!WriteShaderTextAtomically(
                    vsMarkerPath, ShaderCacheMarker(vsCookKey, compiledVsIdentity)) ||
                !ShaderCacheEntryIsValid(
                    vsBinaryPath, vsMarkerPath, vsCookKey, &compiledVsIdentity)) {
                AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                    "Material graph vertex shader binary cache integrity marker could not be published or verified.",
                    request.pass,
                    std::string{ RenderMaterialGraphShaderBackendName(backend) });
                continue;
            }
            artifact.vertexBinaries.push_back(RenderMaterialGraphShaderBinary{
                .backend = backend,
                .binaryPath = kb::platform::PortablePath(vsBinaryPath).generic_string(),
                .byteSize = compiledVsIdentity.bytes,
                .cacheHit = false,
            });
        }

        if (artifact.vertexBinaries.size() != backends.size()) {
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader cook did not produce every requested vertex backend binary.");
            return result;
        }
    }

    for (const RenderMaterialGraphShaderBinary& fragmentBinary : artifact.binaries) {
        const auto vertex = std::ranges::find_if(
            artifact.vertexBinaries,
            [&fragmentBinary](const RenderMaterialGraphShaderBinary& candidate) {
                return candidate.backend == fragmentBinary.backend;
            });
        const std::filesystem::path fragmentPath = kb::platform::ExtendedLengthPath(fragmentBinary.binaryPath);
        const std::string vertexFilename = vertex == artifact.vertexBinaries.end()
            ? "-"
            : std::filesystem::path{ vertex->binaryPath }.filename().generic_string();
        if (!WriteShaderTextAtomically(
                fragmentPath.parent_path() / "program.current",
                fragmentPath.filename().generic_string() + " " + vertexFilename + "\n")) {
            AddArtifactDiagnostic(result.diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Material graph shader program cache pointer could not be published atomically.",
                request.pass,
                std::string{ RenderMaterialGraphShaderBackendName(fragmentBinary.backend) });
            return result;
        }
    }

    result.artifact = std::move(artifact);
    return result;
}

RenderMaterialGraphShaderManifest BuildRenderMaterialGraphShaderManifest(
    std::span<const RenderMaterialGraphShaderArtifact> artifacts) {
    RenderMaterialGraphShaderManifest manifest{};
    for (const RenderMaterialGraphShaderArtifact& artifact : artifacts) {
        for (const RenderMaterialGraphShaderBinary& binary : artifact.binaries) {
            manifest.entries.push_back(RenderMaterialGraphShaderManifestEntry{
                .graphSourceHash = artifact.graphSourceHash,
                .variantKey = artifact.variantKey,
                .wrapperHash = artifact.wrapperHash,
                .reflectionHash = artifact.reflectionHash,
                .dependencyHash = artifact.dependencyHash,
                .artifactHash = artifact.artifactHash,
                .materialTypeVersion = artifact.materialTypeVersion,
                .pass = artifact.pass,
                .backend = binary.backend,
                .shaderPlatform = artifact.shaderPlatform,
                .binaryPath = binary.binaryPath,
                .graphGenerated = artifact.graphGenerated,
            });
        }
    }
    std::sort(manifest.entries.begin(), manifest.entries.end(), [](const RenderMaterialGraphShaderManifestEntry& lhs, const RenderMaterialGraphShaderManifestEntry& rhs) {
        if (lhs.graphSourceHash != rhs.graphSourceHash) {
            return lhs.graphSourceHash < rhs.graphSourceHash;
        }
        if (lhs.variantKey != rhs.variantKey) {
            return lhs.variantKey < rhs.variantKey;
        }
        if (lhs.pass != rhs.pass) {
            return lhs.pass < rhs.pass;
        }
        return static_cast<std::uint8_t>(lhs.backend) < static_cast<std::uint8_t>(rhs.backend);
    });

    std::uint64_t manifestHash = 1469598103934665603ULL;
    HashU64(manifestHash, manifest.entries.size());
    for (const RenderMaterialGraphShaderManifestEntry& entry : manifest.entries) {
        HashU64(manifestHash, entry.graphSourceHash);
        HashU64(manifestHash, entry.variantKey);
        HashU64(manifestHash, entry.wrapperHash);
        HashU64(manifestHash, entry.reflectionHash);
        HashU64(manifestHash, entry.dependencyHash);
        HashU64(manifestHash, entry.artifactHash);
        HashU64(manifestHash, entry.materialTypeVersion);
        HashString64(manifestHash, entry.pass);
        HashU64(manifestHash, static_cast<std::uint64_t>(entry.backend));
        HashU64(manifestHash, static_cast<std::uint64_t>(entry.shaderPlatform));
        HashString64(manifestHash, entry.binaryPath);
        HashU64(manifestHash, entry.graphGenerated ? 1U : 0U);
    }
    manifest.manifestHash = manifestHash;
    return manifest;
}

std::vector<RenderMaterialGraphDiagnostic> ValidateRenderMaterialGraphShaderManifest(
    const RenderMaterialGraphShaderManifest& manifest) {
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;
    std::error_code error;
    for (const RenderMaterialGraphShaderManifestEntry& entry : manifest.entries) {
        if (!entry.graphGenerated) {
            continue;
        }
        const std::string backendName{ RenderMaterialGraphShaderBackendName(entry.backend) };
        if (entry.binaryPath.empty()) {
            AddArtifactDiagnostic(diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Graph shader manifest entry (pass '" + entry.pass + "', backend '" + backendName + "') has no binary path.",
                entry.pass, backendName);
            continue;
        }
        const std::filesystem::path binaryPath = kb::platform::ExtendedLengthPath(entry.binaryPath);
        if (!std::filesystem::exists(binaryPath, error) || std::filesystem::file_size(binaryPath, error) == 0U) {
            AddArtifactDiagnostic(diagnostics, RenderMaterialGraphDiagnosticSeverity::Error,
                "Graph shader manifest references a missing or empty binary at " + entry.binaryPath + ".",
                entry.pass, backendName);
        }
    }
    return diagnostics;
}

void WriteRenderMaterialGraphShaderManifest(std::ostream& output, const RenderMaterialGraphShaderManifest& manifest) {
    output << "graphShaderManifest 3\n";
    output << "manifestHash " << manifest.manifestHash << '\n';
    for (const RenderMaterialGraphShaderManifestEntry& entry : manifest.entries) {
        output << "graphArtifact "
            << entry.graphSourceHash << ' '
            << entry.variantKey << ' '
            << RenderMaterialGraphShaderBackendName(entry.backend) << ' '
            << kb::assets::bake::ShaderBakePlatformName(entry.shaderPlatform) << ' '
            << entry.wrapperHash << ' '
            << entry.reflectionHash << ' '
            << entry.dependencyHash << ' '
            << entry.artifactHash << ' '
            << entry.materialTypeVersion << ' '
            << (entry.graphGenerated ? 1U : 0U) << ' '
            << (entry.pass.empty() ? "_" : entry.pass) << ' '
            << entry.binaryPath << '\n';
    }
}

RenderMaterialGraphShaderManifest ParseRenderMaterialGraphShaderManifest(std::istream& input) {
    RenderMaterialGraphShaderManifest manifest{};
    std::uint32_t version = 1U;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream{ line };
        std::string token;
        stream >> token;
        if (token == "graphShaderManifest") {
            stream >> version;
            continue;
        }
        if (token == "manifestHash") {
            stream >> manifest.manifestHash;
            continue;
        }
        if (token != "graphArtifact") {
            continue;
        }
        RenderMaterialGraphShaderManifestEntry entry{};
        std::string backendName;
        std::string platformName;
        std::string pass;
        std::uint32_t graphGenerated = 1U;
        stream >> entry.graphSourceHash;
        if (version >= 2U) {
            stream >> entry.variantKey;
        }
        stream >> backendName;
        if (version >= 3U) {
            stream >> platformName;
            if (!kb::assets::bake::TryParseShaderBakePlatform(platformName, entry.shaderPlatform)) {
                continue;
            }
        }
        stream >> entry.wrapperHash >> entry.reflectionHash >>
            entry.dependencyHash >> entry.artifactHash >> entry.materialTypeVersion >> graphGenerated >> pass;
        entry.backend = ParseRenderMaterialGraphShaderBackend(backendName).value_or(RenderMaterialGraphShaderBackend::Spirv);
        entry.graphGenerated = graphGenerated != 0U;
        entry.pass = pass == "_" ? std::string{} : pass;
        std::string binaryPath;
        std::getline(stream, binaryPath);
        if (!binaryPath.empty() && binaryPath.front() == ' ') {
            binaryPath.erase(binaryPath.begin());
        }
        entry.binaryPath = binaryPath;
        manifest.entries.push_back(std::move(entry));
    }
    return manifest;
}

} // namespace kb::render
