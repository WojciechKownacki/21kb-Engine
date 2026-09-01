#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace kb::editor {

#if defined(_WIN32)
inline constexpr std::uintptr_t kEditorPackageStatusTimer = 0x21B6U;
#endif

enum class EditorPackageStage : std::uint8_t {
    None,
    Validate,
    Cook,
    Stage,
    Verify,
};

enum class EditorPackageDiagnosticSeverity : std::uint8_t {
    Info,
    Warning,
    Error,
};

enum class EditorPackageJobState : std::uint8_t {
    Idle,
    Running,
    Succeeded,
    Failed,
    Cancelled,
};

struct EditorPackageDiagnostic {
    EditorPackageDiagnosticSeverity severity = EditorPackageDiagnosticSeverity::Info;
    std::string message;
};

struct EditorPackageProtocolEvent {
    enum class Kind : std::uint8_t { None, Stage, Diagnostic, Result, SigningRequest } kind = Kind::None;
    EditorPackageStage stage = EditorPackageStage::None;
    EditorPackageDiagnosticSeverity severity = EditorPackageDiagnosticSeverity::Info;
    int progress = 0;
    std::string message;
    std::filesystem::path resultDirectory;
    std::filesystem::path signingRequestFile;
    std::filesystem::path signingResponseFile;
};

struct EditorPackageRequest {
    std::filesystem::path builderExecutable;
    std::filesystem::path packageScript;
    std::filesystem::path projectFile;
    std::string targetId;
    std::string configuration;
    std::filesystem::path outputDirectory;
    std::filesystem::path engineRoot;
    std::filesystem::path buildRoot;
    std::string productName;
    std::string publisher;
    std::string version;
    std::string executableName;
    std::filesystem::path applicationIcon;
    std::string androidApplicationId;
    std::uint32_t androidVersionCode = 1U;
    std::string androidLabel;
    std::filesystem::path androidKeystore;
    std::string androidKeyAlias;
    std::string androidStorePassword;
    std::string androidKeyPassword;
    std::filesystem::path emsdkRoot;
    std::string linuxHost;
    std::string linuxUser;
    std::string linuxHostKey;
    std::uint16_t linuxPort = 22U;
    std::string linuxEngineRoot;
    std::string linuxDisplay;
    std::filesystem::path linuxIdentity;
    bool launch = false;
};

struct EditorPackageSnapshot {
    EditorPackageJobState state = EditorPackageJobState::Idle;
    EditorPackageStage stage = EditorPackageStage::None;
    int progress = 0;
    std::string status;
    std::filesystem::path resultDirectory;
    std::vector<EditorPackageDiagnostic> diagnostics;
};

class EditorProjectPackageService final {
public:
    EditorProjectPackageService() = default;
    ~EditorProjectPackageService();

    EditorProjectPackageService(const EditorProjectPackageService&) = delete;
    EditorProjectPackageService& operator=(const EditorProjectPackageService&) = delete;

    [[nodiscard]] bool Start(EditorPackageRequest request, std::string& error);
    void Cancel() noexcept;
    [[nodiscard]] EditorPackageSnapshot Snapshot() const;
    [[nodiscard]] bool IsRunning() const noexcept;

    [[nodiscard]] static EditorPackageProtocolEvent ParseProtocolLine(std::string_view line);
    [[nodiscard]] static std::vector<std::wstring> BuildArguments(const EditorPackageRequest& request);
    [[nodiscard]] static bool ResultMatchesRequest(
        const EditorPackageRequest& request,
        const std::filesystem::path& resultDirectory) noexcept;

private:
    void Run(EditorPackageRequest request);
    void ApplyProtocolLine(std::string_view line);
    void Finish(EditorPackageJobState state, std::string status);
    void JoinFinishedWorker();

    mutable std::mutex mutex_;
    EditorPackageSnapshot snapshot_{};
    std::thread worker_;
    void* processJob_ = nullptr;
    bool cancelRequested_ = false;
};

} // namespace kb::editor
