#include "packaging/EditorProjectPackageService.hpp"
#include "packaging/EditorAndroidSigningBroker.hpp"
#include "packaging/EditorPackageInputValidation.hpp"
#include "packaging/EditorPackageProcessEnvironment.hpp"
#include "engine/packaging/PackagingTargetCatalog.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <exception>
#include <limits>
#include <optional>
#include <sstream>
#include <system_error>

namespace kb::editor {
namespace {

constexpr std::size_t kMaximumDiagnosticCount = 256U;
constexpr std::size_t kMaximumMessageBytes = 2048U;
constexpr std::size_t kMaximumProtocolLineBytes = 8192U;

struct RequestSecretsGuard {
    EditorPackageRequest& request;
    ~RequestSecretsGuard() {
        EditorAndroidSigningBroker::SecureClear(request.androidStorePassword);
        EditorAndroidSigningBroker::SecureClear(request.androidKeyPassword);
    }
};

[[nodiscard]] std::wstring Wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
#if defined(_WIN32)
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) {
        return std::wstring{ text.begin(), text.end() };
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    static_cast<void>(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), count));
    return result;
#else
    return std::wstring{ text.begin(), text.end() };
#endif
}

[[nodiscard]] std::wstring Quote(std::wstring_view argument) {
    if (argument.find_first_of(L" \t\"") == std::wstring_view::npos) {
        return std::wstring{ argument };
    }
    std::wstring result{ L'\"' };
    std::size_t slashes = 0U;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'\"') {
            result.append((slashes * 2U) + 1U, L'\\');
            result.push_back(L'\"');
        } else {
            result.append(slashes, L'\\');
            result.push_back(character);
        }
        slashes = 0U;
    }
    result.append(slashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}

[[nodiscard]] std::wstring CommandLine(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments) {
    std::wstring result = Quote(executable.wstring());
    for (const std::wstring& argument : arguments) {
        result.push_back(L' ');
        result += Quote(argument);
    }
    return result;
}

[[nodiscard]] std::string Bounded(std::string_view message) {
    const std::size_t count = std::min(message.size(), kMaximumMessageBytes);
    return std::string{ message.substr(0U, count) };
}

[[nodiscard]] std::optional<EditorPackageStage> ParseStage(std::string_view value) noexcept {
    if (value == "Validate") return EditorPackageStage::Validate;
    if (value == "Cook") return EditorPackageStage::Cook;
    if (value == "Stage") return EditorPackageStage::Stage;
    if (value == "Verify") return EditorPackageStage::Verify;
    return std::nullopt;
}

[[nodiscard]] std::optional<EditorPackageDiagnosticSeverity> ParseSeverity(std::string_view value) noexcept {
    if (value == "Info") return EditorPackageDiagnosticSeverity::Info;
    if (value == "Warning") return EditorPackageDiagnosticSeverity::Warning;
    if (value == "Error") return EditorPackageDiagnosticSeverity::Error;
    return std::nullopt;
}

[[nodiscard]] std::array<std::string_view, 4> SplitFour(std::string_view line, std::size_t& count) noexcept {
    std::array<std::string_view, 4> fields{};
    count = 0U;
    while (count < fields.size()) {
        const std::size_t delimiter = line.find('|');
        fields[count++] = line.substr(0U, delimiter);
        if (delimiter == std::string_view::npos) {
            break;
        }
        line.remove_prefix(delimiter + 1U);
    }
    return fields;
}

[[nodiscard]] std::string LastProcessError(std::string_view operation) {
#if defined(_WIN32)
    return std::string{ operation } + " failed with Windows error " + std::to_string(GetLastError()) + '.';
#else
    return std::string{ operation } + " is unsupported on this platform.";
#endif
}

[[nodiscard]] bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) noexcept {
    std::error_code leftError;
    std::error_code rightError;
    const std::filesystem::path lhs = std::filesystem::weakly_canonical(left, leftError);
    const std::filesystem::path rhs = std::filesystem::weakly_canonical(right, rightError);
    if (leftError || rightError || lhs.empty() || rhs.empty()) return false;
#if defined(_WIN32)
    return _wcsicmp(lhs.c_str(), rhs.c_str()) == 0;
#else
    return lhs == rhs;
#endif
}

} // namespace

EditorProjectPackageService::~EditorProjectPackageService() {
    Cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool EditorProjectPackageService::Start(EditorPackageRequest request, std::string& error) {
    RequestSecretsGuard requestSecrets{ request };
    error.clear();
    JoinFinishedWorker();
    {
        std::scoped_lock lock{ mutex_ };
        if (snapshot_.state == EditorPackageJobState::Running) {
            error = "A package job is already running.";
            return false;
        }
        if (request.builderExecutable.empty() || request.packageScript.empty() || request.projectFile.empty() ||
            request.targetId.empty() || request.configuration.empty() || request.outputDirectory.empty() ||
            request.engineRoot.empty() || request.buildRoot.empty()) {
            error = "The package request is missing a required path or target value.";
            return false;
        }
        const kb::packaging::PackagingTargetSpec* targetSpec = kb::packaging::FindPackagingTarget(request.targetId);
        if (targetSpec == nullptr) {
            error = "The package request target is not registered.";
            return false;
        }
        if (request.configuration != "Development" && request.configuration != "Release") {
            error = "The package configuration is invalid.";
            return false;
        }
        std::error_code filesystemError;
        if (!std::filesystem::is_regular_file(request.packageScript, filesystemError) || filesystemError ||
            !std::filesystem::is_regular_file(request.projectFile, filesystemError) || filesystemError) {
            error = "The package script or saved project file is unavailable.";
            return false;
        }
        if (!request.outputDirectory.is_absolute() || !request.engineRoot.is_absolute() || !request.buildRoot.is_absolute()) {
            error = "Package output, engine root and build root must be absolute paths.";
            return false;
        }
        const std::filesystem::path projectRoot = request.projectFile.parent_path();
        if (package_input::IsPathInsideOrEqual(projectRoot, request.outputDirectory) ||
            package_input::IsPathInsideOrEqual(projectRoot, request.buildRoot)) {
            error = "Package output and build directories must be outside the project.";
            return false;
        }
        if (!request.applicationIcon.empty() &&
            !package_input::IsValidProjectPngIcon(request.projectFile, request.applicationIcon)) {
            error = "The application icon must be an existing PNG inside the project.";
            return false;
        }
        if (targetSpec->needsAndroidMetadata &&
            (request.androidApplicationId.empty() || request.androidLabel.empty() || request.androidVersionCode == 0U)) {
            error = "The Android package request is missing application metadata.";
            return false;
        }
        if (targetSpec->needsAndroidMetadata && request.configuration == "Release" &&
            (request.androidKeystore.empty() || request.androidKeyAlias.empty() ||
             request.androidStorePassword.empty() || request.androidKeyPassword.empty())) {
            error = "Android Release requires a keystore, key alias and both signing passwords.";
            return false;
        }
        if (targetSpec->needsAndroidMetadata && request.configuration == "Release" &&
            !package_input::IsValidAndroidKeyAlias(request.androidKeyAlias)) {
            error = "The Android signing key alias is invalid.";
            return false;
        }
        if (targetSpec->needsAndroidMetadata && request.configuration == "Release" &&
            (!request.androidKeystore.is_absolute() ||
             !std::filesystem::is_regular_file(request.androidKeystore, filesystemError) || filesystemError)) {
            error = "Android Release requires an absolute existing keystore file.";
            return false;
        }
        if (targetSpec->family == kb::packaging::PackagingTargetFamily::Web &&
            (!request.emsdkRoot.is_absolute() ||
             !std::filesystem::is_directory(request.emsdkRoot, filesystemError) || filesystemError)) {
            error = "Web packaging requires an absolute Emscripten SDK directory.";
            return false;
        }
        if (targetSpec->target == kb::packaging::PackagingTarget::LinuxX64) {
            if (!package_input::IsValidLinuxHost(request.linuxHost) ||
                !package_input::IsValidLinuxUser(request.linuxUser) ||
                !package_input::IsValidLinuxHostKey(request.linuxHostKey) || request.linuxPort == 0U ||
                !package_input::IsValidLinuxEngineRoot(request.linuxEngineRoot) ||
                !package_input::IsValidLinuxDisplay(request.linuxDisplay)) {
                error = "Linux packaging requires valid SSH build-host settings.";
                return false;
            }
            if (!request.linuxIdentity.empty() &&
                (!request.linuxIdentity.is_absolute() ||
                 !std::filesystem::is_regular_file(request.linuxIdentity, filesystemError) || filesystemError)) {
                error = "The Linux SSH identity must be an absolute existing file.";
                return false;
            }
        }
        snapshot_ = EditorPackageSnapshot{
            .state = EditorPackageJobState::Running,
            .stage = EditorPackageStage::Validate,
            .progress = 0,
            .status = "Starting package job.",
        };
        cancelRequested_ = false;
    }
    try {
        worker_ = std::thread{ &EditorProjectPackageService::Run, this, std::move(request) };
    } catch (const std::system_error& exception) {
        std::scoped_lock lock{ mutex_ };
        snapshot_ = EditorPackageSnapshot{};
        cancelRequested_ = false;
        error = std::string{ "Package worker could not be started: " } + exception.what();
        return false;
    }
    return true;
}

void EditorProjectPackageService::Cancel() noexcept {
    std::scoped_lock lock{ mutex_ };
    if (snapshot_.state != EditorPackageJobState::Running) {
        return;
    }
    cancelRequested_ = true;
#if defined(_WIN32)
    HANDLE job = static_cast<HANDLE>(processJob_);
    if (job != nullptr) {
        static_cast<void>(TerminateJobObject(job, ERROR_CANCELLED));
    }
#endif
}

EditorPackageSnapshot EditorProjectPackageService::Snapshot() const {
    std::scoped_lock lock{ mutex_ };
    return snapshot_;
}

bool EditorProjectPackageService::IsRunning() const noexcept {
    std::scoped_lock lock{ mutex_ };
    return snapshot_.state == EditorPackageJobState::Running;
}

EditorPackageProtocolEvent EditorProjectPackageService::ParseProtocolLine(std::string_view line) {
    EditorPackageProtocolEvent event;
    if (line.empty() || line.size() > kMaximumProtocolLineBytes) {
        return event;
    }
    std::size_t count = 0U;
    const auto fields = SplitFour(line, count);
    if (count == 4U && fields[0] == "STAGE") {
        const std::optional<EditorPackageStage> stage = ParseStage(fields[1]);
        int progress = -1;
        const auto parsed = std::from_chars(fields[2].data(), fields[2].data() + fields[2].size(), progress);
        if (!stage.has_value() || parsed.ec != std::errc{} || parsed.ptr != fields[2].data() + fields[2].size() ||
            progress < 0 || progress > 100) {
            return event;
        }
        event.kind = EditorPackageProtocolEvent::Kind::Stage;
        event.stage = *stage;
        event.progress = progress;
        event.message = Bounded(fields[3]);
        return event;
    }
    if (count == 3U && fields[0] == "DIAGNOSTIC") {
        const std::optional<EditorPackageDiagnosticSeverity> severity = ParseSeverity(fields[1]);
        if (!severity.has_value()) {
            return event;
        }
        event.kind = EditorPackageProtocolEvent::Kind::Diagnostic;
        event.severity = *severity;
        event.message = Bounded(fields[2]);
        return event;
    }
    if (count == 2U && fields[0] == "RESULT") {
        const std::filesystem::path result{ fields[1] };
        if (!result.is_absolute()) {
            return event;
        }
        event.kind = EditorPackageProtocolEvent::Kind::Result;
        event.resultDirectory = result.lexically_normal();
        return event;
    }
    if (count == 3U && fields[0] == "SIGNING_REQUEST") {
        const std::filesystem::path request{ fields[1] };
        const std::filesystem::path response{ fields[2] };
        if (!request.is_absolute() || !response.is_absolute()) return event;
        event.kind = EditorPackageProtocolEvent::Kind::SigningRequest;
        event.signingRequestFile = request.lexically_normal();
        event.signingResponseFile = response.lexically_normal();
    }
    return event;
}

std::vector<std::wstring> EditorProjectPackageService::BuildArguments(const EditorPackageRequest& request) {
    std::vector<std::wstring> arguments;
    arguments.reserve(48U);
    const auto append = [&arguments](std::wstring_view name, const std::filesystem::path& value) {
        arguments.emplace_back(name);
        arguments.push_back(value.wstring());
    };
    const auto appendText = [&arguments](std::wstring_view name, std::string_view value) {
        arguments.emplace_back(name);
        arguments.push_back(Wide(value));
    };
    arguments.push_back(request.packageScript.wstring());
    append(L"--project", request.projectFile);
    appendText(L"--target", request.targetId);
    appendText(L"--configuration", request.configuration);
    append(L"--output", request.outputDirectory);
    append(L"--engine-root", request.engineRoot);
    append(L"--build-root", request.buildRoot);
    appendText(L"--product-name", request.productName);
    appendText(L"--publisher", request.publisher);
    appendText(L"--version", request.version);
    appendText(L"--executable-name", request.executableName);
    if (!request.applicationIcon.empty()) {
        append(L"--application-icon", request.applicationIcon);
    }
    const kb::packaging::PackagingTargetSpec* targetSpec = kb::packaging::FindPackagingTarget(request.targetId);
    if (targetSpec != nullptr && targetSpec->needsAndroidMetadata) {
        appendText(L"--android-application-id", request.androidApplicationId);
        appendText(L"--android-version-code", std::to_string(request.androidVersionCode));
        appendText(L"--android-label", request.androidLabel);
        appendText(L"--android-min-sdk", "28");
        appendText(L"--android-target-sdk", "35");
        if (request.configuration == "Release") {
            append(L"--android-keystore", request.androidKeystore);
            appendText(L"--android-key-alias", request.androidKeyAlias);
        }
    }
    if (targetSpec != nullptr && targetSpec->family == kb::packaging::PackagingTargetFamily::Web) {
        append(L"--emsdk", request.emsdkRoot);
    }
    if (targetSpec != nullptr && targetSpec->target == kb::packaging::PackagingTarget::LinuxX64) {
        appendText(L"--linux-host", request.linuxHost);
        appendText(L"--linux-user", request.linuxUser);
        appendText(L"--linux-host-key", request.linuxHostKey);
        appendText(L"--linux-port", std::to_string(request.linuxPort));
        appendText(L"--linux-engine-root", request.linuxEngineRoot);
        appendText(L"--linux-display", request.linuxDisplay);
        if (!request.linuxIdentity.empty()) append(L"--linux-identity", request.linuxIdentity);
    }
    if (request.launch) {
        arguments.emplace_back(L"--launch");
    }
    return arguments;
}

bool EditorProjectPackageService::ResultMatchesRequest(
    const EditorPackageRequest& request,
    const std::filesystem::path& resultDirectory) noexcept {
    return resultDirectory.is_absolute() && request.outputDirectory.is_absolute() &&
        SamePath(resultDirectory, request.outputDirectory);
}

void EditorProjectPackageService::Run(EditorPackageRequest request) {
    RequestSecretsGuard secrets{ request };
    const kb::packaging::PackagingTargetSpec* targetSpec = kb::packaging::FindPackagingTarget(request.targetId);
#if !defined(_WIN32)
    Finish(EditorPackageJobState::Failed, "The editor package process is unsupported on this platform.");
    return;
#else
    SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    if (!CreatePipe(&outputRead, &outputWrite, &inheritable, 0U) ||
        !SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0U) ||
        !CreatePipe(&inputRead, &inputWrite, &inheritable, 0U) ||
        !SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0U)) {
        if (outputRead != nullptr) CloseHandle(outputRead);
        if (outputWrite != nullptr) CloseHandle(outputWrite);
        if (inputRead != nullptr) CloseHandle(inputRead);
        if (inputWrite != nullptr) CloseHandle(inputWrite);
        Finish(EditorPackageJobState::Failed, LastProcessError("Creating package process pipes"));
        return;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        CloseHandle(outputRead); CloseHandle(outputWrite); CloseHandle(inputRead); CloseHandle(inputWrite);
        Finish(EditorPackageJobState::Failed, LastProcessError("Creating the package process job"));
        return;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job); CloseHandle(outputRead); CloseHandle(outputWrite); CloseHandle(inputRead); CloseHandle(inputWrite);
        Finish(EditorPackageJobState::Failed, LastProcessError("Configuring the package process job"));
        return;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = inputRead;
    startup.StartupInfo.hStdOutput = outputWrite;
    startup.StartupInfo.hStdError = outputWrite;
    SIZE_T attributeBytes = 0U;
    static_cast<void>(InitializeProcThreadAttributeList(nullptr, 1U, 0U, &attributeBytes));
    std::vector<unsigned char> attributeStorage(attributeBytes);
    startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    const bool attributeListInitialized = InitializeProcThreadAttributeList(
        startup.lpAttributeList, 1U, 0U, &attributeBytes) != FALSE;
    std::array<HANDLE, 2> inheritedHandles{ inputRead, outputWrite };
    const bool attributesReady = attributeListInitialized && UpdateProcThreadAttribute(
        startup.lpAttributeList, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inheritedHandles.data(), sizeof(inheritedHandles), nullptr, nullptr) != FALSE;
    if (!attributesReady) {
        if (attributeListInitialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
        CloseHandle(job); CloseHandle(outputRead); CloseHandle(outputWrite); CloseHandle(inputRead); CloseHandle(inputWrite);
        Finish(EditorPackageJobState::Failed, LastProcessError("Isolating package process handles"));
        return;
    }
    PROCESS_INFORMATION process{};
    std::wstring command = CommandLine(request.builderExecutable, BuildArguments(request));
    const std::filesystem::path workingDirectory = request.engineRoot;
    std::optional<std::vector<wchar_t>> environment = package_process::BuildSanitizedEnvironment();
    if (!environment.has_value()) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        CloseHandle(job); CloseHandle(outputRead); CloseHandle(outputWrite); CloseHandle(inputRead); CloseHandle(inputWrite);
        Finish(EditorPackageJobState::Failed, "Sanitizing the package process environment failed.");
        return;
    }
    const BOOL created = CreateProcessW(
        nullptr, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
        environment->data(), workingDirectory.c_str(), &startup.StartupInfo, &process);
    SecureZeroMemory(environment->data(), environment->size() * sizeof(wchar_t));
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(outputWrite);
    CloseHandle(inputRead);
    if (!created) {
        CloseHandle(job); CloseHandle(outputRead); CloseHandle(inputWrite);
        Finish(EditorPackageJobState::Failed, LastProcessError("Starting the package process"));
        return;
    }
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        static_cast<void>(TerminateProcess(process.hProcess, ERROR_PROCESS_ABORTED));
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job); CloseHandle(outputRead); CloseHandle(inputWrite);
        Finish(EditorPackageJobState::Failed, LastProcessError("Assigning the package process job"));
        return;
    }
    {
        std::scoped_lock lock{ mutex_ };
        processJob_ = job;
        if (cancelRequested_) {
            static_cast<void>(TerminateJobObject(job, ERROR_CANCELLED));
        }
    }
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        const DWORD error = GetLastError();
        static_cast<void>(TerminateJobObject(job, ERROR_PROCESS_ABORTED));
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(outputRead); CloseHandle(inputWrite);
        {
            std::scoped_lock lock{ mutex_ };
            processJob_ = nullptr;
        }
        CloseHandle(job);
        SetLastError(error);
        Finish(EditorPackageJobState::Failed, LastProcessError("Resuming the package process"));
        return;
    }
    CloseHandle(process.hThread);

    if (inputWrite != nullptr) {
        CloseHandle(inputWrite);
    }

    std::string pending;
    std::array<char, 4096> buffer{};
    bool signingRequestSeen = false;
    for (;;) {
        DWORD read = 0U;
        if (!ReadFile(outputRead, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0U) {
            break;
        }
        pending.append(buffer.data(), read);
        for (;;) {
            const std::size_t newline = pending.find('\n');
            if (newline == std::string::npos) {
                if (pending.size() > kMaximumProtocolLineBytes) {
                    pending.erase(0U, pending.size() - kMaximumProtocolLineBytes);
                }
                break;
            }
            std::string line = pending.substr(0U, newline);
            pending.erase(0U, newline + 1U);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const EditorPackageProtocolEvent protocol = ParseProtocolLine(line);
            if (protocol.kind == EditorPackageProtocolEvent::Kind::SigningRequest) {
                if (request.configuration != "Release" || targetSpec == nullptr ||
                    !targetSpec->needsAndroidMetadata || signingRequestSeen) {
                    ApplyProtocolLine("DIAGNOSTIC|Error|Unexpected or duplicate Android signing request.");
                    static_cast<void>(TerminateJobObject(job, ERROR_ACCESS_DENIED));
                    continue;
                }
                signingRequestSeen = true;
                EditorAndroidSigningResult signing;
                try {
                    signing = EditorAndroidSigningBroker::Execute(
                        protocol.signingRequestFile, protocol.signingResponseFile,
                        request.buildRoot / "package-jobs",
                        request.androidKeystore, request.androidKeyAlias,
                        request.androidStorePassword, request.androidKeyPassword, job);
                } catch (const std::exception&) {
                    EditorAndroidSigningBroker::SecureClear(request.androidStorePassword);
                    EditorAndroidSigningBroker::SecureClear(request.androidKeyPassword);
                    signing.message = "Android signing failed while validating the isolated request.";
                }
                ApplyProtocolLine(std::string{ "DIAGNOSTIC|" } + (signing.succeeded ? "Info|" : "Error|") + signing.message);
                if (!signing.succeeded) static_cast<void>(TerminateJobObject(job, ERROR_ACCESS_DENIED));
                continue;
            }
            ApplyProtocolLine(line);
        }
    }
    CloseHandle(outputRead);
    static_cast<void>(WaitForSingleObject(process.hProcess, INFINITE));
    DWORD exitCode = ERROR_PROCESS_ABORTED;
    static_cast<void>(GetExitCodeProcess(process.hProcess, &exitCode));
    CloseHandle(process.hProcess);
    {
        std::scoped_lock lock{ mutex_ };
        processJob_ = nullptr;
    }
    CloseHandle(job);

    bool cancelled = false;
    bool hasResult = false;
    std::filesystem::path resultDirectory;
    {
        std::scoped_lock lock{ mutex_ };
        cancelled = cancelRequested_;
        hasResult = !snapshot_.resultDirectory.empty();
        resultDirectory = snapshot_.resultDirectory;
    }
    if (cancelled) {
        Finish(EditorPackageJobState::Cancelled, "Package job cancelled.");
    } else if (exitCode != 0U) {
        Finish(EditorPackageJobState::Failed, "Package process failed with exit code " + std::to_string(exitCode) + '.');
    } else if (targetSpec != nullptr && targetSpec->needsAndroidMetadata &&
        request.configuration == "Release" && !signingRequestSeen) {
        Finish(EditorPackageJobState::Failed, "Android Release completed without a signing request.");
    } else if (!hasResult) {
        Finish(EditorPackageJobState::Failed, "Package process completed without a RESULT directory.");
    } else if (!ResultMatchesRequest(request, resultDirectory)) {
        Finish(EditorPackageJobState::Failed, "Package process returned a different output directory than requested.");
    } else {
        Finish(EditorPackageJobState::Succeeded, "Package verified and published.");
    }
#endif
}

void EditorProjectPackageService::ApplyProtocolLine(std::string_view line) {
    const EditorPackageProtocolEvent event = ParseProtocolLine(line);
    std::scoped_lock lock{ mutex_ };
    switch (event.kind) {
    case EditorPackageProtocolEvent::Kind::Stage:
        snapshot_.stage = event.stage;
        snapshot_.progress = event.progress;
        snapshot_.status = event.message;
        break;
    case EditorPackageProtocolEvent::Kind::Diagnostic:
        if (snapshot_.diagnostics.size() == kMaximumDiagnosticCount) {
            snapshot_.diagnostics.erase(snapshot_.diagnostics.begin());
        }
        snapshot_.diagnostics.push_back(EditorPackageDiagnostic{ event.severity, event.message });
        break;
    case EditorPackageProtocolEvent::Kind::Result:
        snapshot_.resultDirectory = event.resultDirectory;
        break;
    case EditorPackageProtocolEvent::Kind::SigningRequest:
        break;
    case EditorPackageProtocolEvent::Kind::None:
        if (!line.empty()) {
            if (snapshot_.diagnostics.size() == kMaximumDiagnosticCount) {
                snapshot_.diagnostics.erase(snapshot_.diagnostics.begin());
            }
            snapshot_.diagnostics.push_back(EditorPackageDiagnostic{
                EditorPackageDiagnosticSeverity::Info, Bounded(line) });
        }
        break;
    }
}

void EditorProjectPackageService::Finish(EditorPackageJobState state, std::string status) {
    std::scoped_lock lock{ mutex_ };
    snapshot_.state = state;
    snapshot_.status = std::move(status);
    if (state == EditorPackageJobState::Succeeded) {
        snapshot_.progress = 100;
        snapshot_.stage = EditorPackageStage::Verify;
    }
}

void EditorProjectPackageService::JoinFinishedWorker() {
    bool finished = false;
    {
        std::scoped_lock lock{ mutex_ };
        finished = snapshot_.state != EditorPackageJobState::Running;
    }
    if (finished && worker_.joinable()) {
        worker_.join();
    }
}

} // namespace kb::editor
