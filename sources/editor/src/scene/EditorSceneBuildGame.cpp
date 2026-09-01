#include "scene/EditorSceneContext.hpp"

#include "packaging/EditorPackageInputValidation.hpp"
#include "packaging/EditorProjectIconTransaction.hpp"
#include "packaging/EditorProjectPackageService.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/BuildGamePanelModel.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <system_error>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const std::vector<BuildGameField>& fields, BuildGameField field) noexcept {
    return std::ranges::find(fields, field) != fields.end();
}

[[nodiscard]] std::filesystem::path AbsoluteNormalized(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

void SecureClear(std::string& value) noexcept {
    if (value.capacity() > value.size()) value.resize(value.capacity(), '\0');
    volatile char* bytes = value.empty() ? nullptr : value.data();
    for (std::size_t index = 0U; index < value.size(); ++index) bytes[index] = '\0';
    value.clear();
    value.shrink_to_fit();
}

[[nodiscard]] std::string SafeUnitComponent(std::string_view value, bool windowsTarget) {
    if (windowsTarget && package_input::IsWindowsReservedDeviceName(value)) return {};
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0 || character == '-' || character == '_') result.push_back(character);
        else if ((character == ' ' || character == '.') && !result.empty() && result.back() != '_') result.push_back('_');
        else return {};
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result;
}

[[nodiscard]] std::string Trimmed(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1U);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1U);
    return std::string{ value };
}

} // namespace

kb::packaging::PackagingTarget EditorSceneContext::BuildGameTarget() const noexcept {
    const auto targets = kb::packaging::PackagingTargets();
    return targets[static_cast<std::size_t>(std::clamp(
        buildGameSelectedTarget_, 0, static_cast<int>(targets.size()) - 1))].target;
}

const EditorBuildGameSettings& EditorSceneContext::BuildGameSettings() const noexcept {
    return buildGameSettings_;
}

EditorPackageSnapshot EditorSceneContext::BuildGamePackageSnapshot() const {
    return buildGamePackageService_ != nullptr ? buildGamePackageService_->Snapshot() : EditorPackageSnapshot{};
}

bool EditorSceneContext::IsBuildGameTextEditing() const noexcept {
    return buildGameEditingField_ != BuildGameField::None;
}

BuildGameField EditorSceneContext::BuildGameEditingField() const noexcept {
    return buildGameEditingField_;
}

std::string_view EditorSceneContext::BuildGameEditBuffer() const noexcept {
    return buildGameEditBuffer_;
}

bool EditorSceneContext::HasBuildGameStorePassword() const noexcept { return !buildGameStorePassword_.empty(); }
bool EditorSceneContext::HasBuildGameKeyPassword() const noexcept { return !buildGameKeyPassword_.empty(); }
void EditorSceneContext::ClearBuildGameSigningPasswords() noexcept {
    SecureClear(buildGameStorePassword_);
    SecureClear(buildGameKeyPassword_);
}

bool EditorSceneContext::BeginBuildGameTextEdit(BuildGameField field) {
    const std::vector<BuildGameField> order = BuildGamePanelModel::TextFocusOrder(
        BuildGameTarget(), buildGameSelectedProfile_ == 1);
    if (!Contains(order, field)) return false;
    buildGameEditingField_ = field;
    if (field == BuildGameField::AndroidStorePassword) buildGameEditBuffer_ = buildGameStorePassword_;
    else if (field == BuildGameField::AndroidKeyPassword) buildGameEditBuffer_ = buildGameKeyPassword_;
    else buildGameEditBuffer_ = BuildGamePanelModel::Value(field, BuildGameTarget(), projectConfig_, buildGameSettings_);
    if (field == BuildGameField::AndroidKeyAlias && buildGameEditBuffer_ == "Development only") {
        buildGameEditBuffer_.clear();
    }
    buildGameEditOriginal_ = buildGameEditBuffer_;
    buildGameEditSelectAll_ = true;
    return true;
}

bool EditorSceneContext::AppendBuildGameText(wchar_t character) {
    if (character < 32 || character > 126) return false;
    const char byte = static_cast<char>(character);
    return InsertBuildGameText(std::string_view{ &byte, 1U });
}

bool EditorSceneContext::InsertBuildGameText(std::string_view text) {
    const bool sensitive = buildGameEditingField_ == BuildGameField::AndroidStorePassword ||
        buildGameEditingField_ == BuildGameField::AndroidKeyPassword;
    const std::size_t maximumLength = sensitive ? 512U : 4096U;
    return IsBuildGameTextEditing() && BuildGamePanelModel::InsertPrintableText(
        buildGameEditBuffer_, buildGameEditSelectAll_, text, maximumLength);
}

bool EditorSceneContext::BackspaceBuildGameText() {
    if (!IsBuildGameTextEditing()) return false;
    if (buildGameEditSelectAll_) {
        const bool changed = !buildGameEditBuffer_.empty();
        buildGameEditBuffer_.clear();
        buildGameEditSelectAll_ = false;
        return changed;
    }
    if (buildGameEditBuffer_.empty()) return false;
    buildGameEditBuffer_.pop_back();
    return true;
}

bool EditorSceneContext::SelectAllBuildGameText() noexcept {
    if (!IsBuildGameTextEditing()) return false;
    buildGameEditSelectAll_ = true;
    return true;
}

bool EditorSceneContext::CommitBuildGameTextEdit() {
    if (!IsBuildGameTextEditing()) return false;
    if (buildGameEditBuffer_ == buildGameEditOriginal_) {
        CancelBuildGameTextEdit();
        return true;
    }
    kb::project::ProjectSettings projectCandidate = projectConfig_;
    EditorBuildGameSettings localCandidate = buildGameSettings_;
    std::string error;
    const bool passwordField = buildGameEditingField_ == BuildGameField::AndroidStorePassword ||
        buildGameEditingField_ == BuildGameField::AndroidKeyPassword;
    if (passwordField) {
        if (buildGameEditBuffer_.empty() || buildGameEditBuffer_.size() > 512U ||
            std::ranges::any_of(buildGameEditBuffer_, [](char character) {
                const unsigned char byte = static_cast<unsigned char>(character);
                return byte < 0x20U || byte == 0x7fU;
            })) {
            console_.Error("Packaging", "Signing passwords must contain 1 to 512 printable characters.");
            return false;
        }
        std::string& destination = buildGameEditingField_ == BuildGameField::AndroidStorePassword
            ? buildGameStorePassword_ : buildGameKeyPassword_;
        SecureClear(destination);
        destination = buildGameEditBuffer_;
    } else if (!BuildGamePanelModel::ApplyText(buildGameEditingField_, buildGameEditBuffer_, BuildGameTarget(),
                   projectCandidate, localCandidate, error)) {
        console_.Error("Packaging", error);
        return false;
    }

    const bool projectChanged = projectCandidate != projectConfig_;
    const bool localChanged = localCandidate != buildGameSettings_;
    if (projectChanged) {
        const kb::project::ProjectSettings original = projectConfig_;
        projectConfig_ = std::move(projectCandidate);
        if (!SaveProjectConfiguration()) {
            projectConfig_ = original;
            return false;
        }
    }
    if (localChanged) {
        const std::filesystem::path path = EditorBuildGameSettingsStore::FilePath(EditorProjectPaths::ProjectRoot());
        if (!EditorBuildGameSettingsStore::Save(path, localCandidate, error)) {
            console_.Error("Packaging", error.empty() ? "Packaging settings could not be saved." : error);
            return false;
        }
        buildGameSettings_ = std::move(localCandidate);
    }
    CancelBuildGameTextEdit();
    return true;
}

void EditorSceneContext::CancelBuildGameTextEdit() noexcept {
    const bool sensitive = buildGameEditingField_ == BuildGameField::AndroidStorePassword ||
        buildGameEditingField_ == BuildGameField::AndroidKeyPassword;
    buildGameEditingField_ = BuildGameField::None;
    if (sensitive) {
        SecureClear(buildGameEditBuffer_);
        SecureClear(buildGameEditOriginal_);
    } else {
        buildGameEditBuffer_.clear();
        buildGameEditOriginal_.clear();
    }
    buildGameEditSelectAll_ = false;
}

bool EditorSceneContext::FocusAdjacentBuildGameTextField(bool backwards) {
    if (!IsBuildGameTextEditing()) return false;
    const BuildGameField current = buildGameEditingField_;
    if (!CommitBuildGameTextEdit()) return false;
    const std::vector<BuildGameField> order = BuildGamePanelModel::TextFocusOrder(
        BuildGameTarget(), buildGameSelectedProfile_ == 1);
    const auto found = std::ranges::find(order, current);
    if (found == order.end() || order.empty()) return false;
    const std::size_t index = static_cast<std::size_t>(found - order.begin());
    const std::size_t next = backwards ? (index + order.size() - 1U) % order.size() : (index + 1U) % order.size();
    return BeginBuildGameTextEdit(order[next]);
}

bool EditorSceneContext::SetBuildGameOutputDirectory(const std::filesystem::path& path) {
    if (path.empty()) return false;
    EditorBuildGameSettings candidate = buildGameSettings_;
    candidate.For(BuildGameTarget()).outputDirectory = AbsoluteNormalized(path);
    std::string error;
    if (!EditorBuildGameSettingsStore::Save(EditorBuildGameSettingsStore::FilePath(EditorProjectPaths::ProjectRoot()), candidate, error)) {
        console_.Error("Packaging", error.empty() ? "Packaging settings could not be saved." : error);
        return false;
    }
    buildGameSettings_ = std::move(candidate);
    return true;
}

bool EditorSceneContext::SetBuildGameBuildRoot(const std::filesystem::path& path) {
    if (path.empty()) return false;
    EditorBuildGameSettings candidate = buildGameSettings_;
    candidate.buildRoot = AbsoluteNormalized(path);
    std::string error;
    if (!EditorBuildGameSettingsStore::Save(EditorBuildGameSettingsStore::FilePath(EditorProjectPaths::ProjectRoot()), candidate, error)) {
        console_.Error("Packaging", error.empty() ? "Packaging settings could not be saved." : error);
        return false;
    }
    buildGameSettings_ = std::move(candidate);
    return true;
}

bool EditorSceneContext::SetBuildGameToolchainDirectory(BuildGameField field, const std::filesystem::path& path) {
    if (path.empty() || field != BuildGameField::EmsdkRoot) return false;
    std::error_code filesystemError;
    if (!std::filesystem::is_directory(path, filesystemError) || filesystemError) {
        console_.Error("Packaging", "The selected toolchain directory does not exist.");
        return false;
    }
    EditorBuildGameSettings candidate = buildGameSettings_;
    candidate.emsdkRoot = AbsoluteNormalized(path);
    std::string error;
    if (!EditorBuildGameSettingsStore::Save(EditorBuildGameSettingsStore::FilePath(EditorProjectPaths::ProjectRoot()), candidate, error)) {
        console_.Error("Packaging", error.empty() ? "Packaging settings could not be saved." : error);
        return false;
    }
    buildGameSettings_ = std::move(candidate);
    return true;
}

bool EditorSceneContext::SetBuildGameLocalFile(BuildGameField field, const std::filesystem::path& path) {
    if (path.empty() || (field != BuildGameField::AndroidKeystore && field != BuildGameField::LinuxIdentity)) return false;
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(path, filesystemError) || filesystemError) {
        console_.Error("Packaging", "The selected file does not exist.");
        return false;
    }
    EditorBuildGameSettings candidate = buildGameSettings_;
    if (field == BuildGameField::AndroidKeystore) {
        candidate.For(BuildGameTarget()).androidKeystore = AbsoluteNormalized(path);
    } else {
        candidate.linuxIdentity = AbsoluteNormalized(path);
    }
    std::string error;
    if (!EditorBuildGameSettingsStore::Save(EditorBuildGameSettingsStore::FilePath(EditorProjectPaths::ProjectRoot()), candidate, error)) {
        console_.Error("Packaging", error.empty() ? "Packaging settings could not be saved." : error);
        return false;
    }
    buildGameSettings_ = std::move(candidate);
    return true;
}

bool EditorSceneContext::ImportBuildGameApplicationIcon(const std::filesystem::path& path) {
    const std::filesystem::path projectRoot = EditorProjectPaths::ProjectRoot();
    EditorProjectIconTransaction iconTransaction;
    std::string iconError;
    if (!iconTransaction.Publish(path, projectRoot, iconError)) {
        console_.Error("Packaging", iconError);
        return false;
    }
    kb::project::ProjectSettings candidate = projectConfig_;
    candidate.applicationIcon = "Branding/ApplicationIcon.png";
    const kb::project::ProjectSettings original = projectConfig_;
    projectConfig_ = std::move(candidate);
    if (!SaveProjectConfiguration()) {
        projectConfig_ = original;
        if (!iconTransaction.Rollback(iconError)) console_.Error("Packaging", iconError);
        return false;
    }
    iconTransaction.Commit();
    return true;
}

bool EditorSceneContext::ToggleBuildGameLaunchAfterBuild() {
    EditorBuildGameSettings candidate = buildGameSettings_;
    EditorBuildGameTargetSettings& target = candidate.For(BuildGameTarget());
    target.launchAfterBuild = !target.launchAfterBuild;
    std::string error;
    if (!EditorBuildGameSettingsStore::Save(EditorBuildGameSettingsStore::FilePath(EditorProjectPaths::ProjectRoot()), candidate, error)) {
        console_.Error("Packaging", error.empty() ? "Packaging settings could not be saved." : error);
        return false;
    }
    buildGameSettings_ = std::move(candidate);
    return true;
}

bool EditorSceneContext::StartBuildGamePackage() {
    if (buildGamePackageService_ == nullptr) return false;
    if (IsBuildGameTextEditing() && !CommitBuildGameTextEdit()) return false;
    const bool release = buildGameSelectedProfile_ == 1;
    const BuildGameValidation validation = BuildGamePanelModel::Validate(
        BuildGameTarget(), projectConfig_, buildGameSettings_, release, buildGamePackageService_->IsRunning(),
        HasBuildGameStorePassword(), HasBuildGameKeyPassword());
    if (!validation.canBuild) {
        console_.Error("Packaging", validation.reason);
        return false;
    }
    if (!SaveCurrentScene() || !SaveProjectConfiguration()) {
        console_.Error("Packaging", "The current map and project settings must be saved before packaging.");
        return false;
    }

#if !defined(KB_EDITOR_ENGINE_ROOT)
    console_.Error("Packaging", "The editor package root is unavailable.");
    return false;
#else
    const auto targets = kb::packaging::PackagingTargets();
    if (buildGameSelectedTarget_ < 0 || buildGameSelectedTarget_ >= static_cast<int>(targets.size())) {
        console_.Error("Packaging", "The selected package target is invalid.");
        return false;
    }
    const kb::packaging::PackagingTargetSpec& targetSpec = targets[static_cast<std::size_t>(buildGameSelectedTarget_)];
    const EditorBuildGameTargetSettings& targetSettings = buildGameSettings_.For(targetSpec.target);
    const std::filesystem::path engineRoot = KB_EDITOR_ENGINE_ROOT;
    EditorPackageRequest request;
    request.builderExecutable = buildGameSettings_.builderExecutable;
    request.packageScript = engineRoot / "scripts" / "package_game.py";
    request.projectFile = AbsoluteNormalized(projectFile_);
    request.targetId = targetSpec.bakeId;
    request.configuration = release ? "Release" : "Development";
    const std::filesystem::path outputBase = AbsoluteNormalized(targetSettings.outputDirectory);
    const std::filesystem::path projectRoot = AbsoluteNormalized(EditorProjectPaths::ProjectRoot());
    const std::filesystem::path buildRoot = AbsoluteNormalized(buildGameSettings_.buildRoot);
    if (package_input::IsPathInsideOrEqual(projectRoot, outputBase) ||
        package_input::IsPathInsideOrEqual(projectRoot, buildRoot)) {
        console_.Error("Packaging", "Package output and build directories must be outside the project.");
        return false;
    }
    const std::string unitComponent = SafeUnitComponent(
        Trimmed(targetSpec.needsExecutableName ? projectConfig_.executableName : projectConfig_.gameName),
        targetSpec.target == kb::packaging::PackagingTarget::WindowsX64);
    if (unitComponent.empty()) {
        console_.Error("Packaging", "Product or executable name cannot form a safe package directory name.");
        return false;
    }
    request.outputDirectory = (outputBase /
        (unitComponent + '-' + request.targetId + '-' + request.configuration)).lexically_normal();
    if (request.outputDirectory.parent_path() != outputBase) {
        console_.Error("Packaging", "Package directory escaped the selected output directory.");
        return false;
    }
    request.engineRoot = AbsoluteNormalized(engineRoot);
    request.buildRoot = buildRoot;
    request.productName = Trimmed(projectConfig_.gameName);
    request.publisher = Trimmed(projectConfig_.publisher);
    request.version = Trimmed(projectConfig_.version);
    request.executableName = targetSpec.needsExecutableName
        ? Trimmed(projectConfig_.executableName) : Trimmed(projectConfig_.gameName);
    if (!projectConfig_.applicationIcon.empty()) {
        const std::filesystem::path iconRelative{ projectConfig_.applicationIcon };
        if (!package_input::IsNormalProjectRelativePath(iconRelative)) {
            console_.Error("Packaging", "Application icon must be a normal project-relative PNG path.");
            return false;
        }
        request.applicationIcon = AbsoluteNormalized(projectRoot / iconRelative);
        if (!package_input::IsValidProjectPngIcon(request.projectFile, request.applicationIcon)) {
            console_.Error("Packaging", "Application icon must be an existing PNG inside the project.");
            return false;
        }
    }
    if (targetSpec.needsAndroidMetadata) {
        request.androidApplicationId = Trimmed(projectConfig_.androidApplicationId);
        request.androidVersionCode = projectConfig_.androidVersionCode;
        request.androidLabel = Trimmed(projectConfig_.androidLabel);
        request.androidKeystore = targetSettings.androidKeystore;
        request.androidKeyAlias = Trimmed(targetSettings.androidKeyAlias);
        request.androidStorePassword = buildGameStorePassword_;
        request.androidKeyPassword = buildGameKeyPassword_;
    }
    if (targetSpec.family == kb::packaging::PackagingTargetFamily::Web) {
        request.emsdkRoot = AbsoluteNormalized(buildGameSettings_.emsdkRoot);
    }
    if (targetSpec.target == kb::packaging::PackagingTarget::LinuxX64) {
        request.linuxHost = Trimmed(buildGameSettings_.linuxHost);
        request.linuxUser = Trimmed(buildGameSettings_.linuxUser);
        request.linuxHostKey = Trimmed(buildGameSettings_.linuxHostKey);
        request.linuxPort = buildGameSettings_.linuxPort;
        request.linuxEngineRoot = Trimmed(buildGameSettings_.linuxEngineRoot);
        request.linuxDisplay = Trimmed(buildGameSettings_.linuxDisplay);
        request.linuxIdentity = buildGameSettings_.linuxIdentity.empty()
            ? std::filesystem::path{} : AbsoluteNormalized(buildGameSettings_.linuxIdentity);
    }
    request.launch = targetSettings.launchAfterBuild;
    std::string error;
    if (!buildGamePackageService_->Start(std::move(request), error)) {
        console_.Error("Packaging", error);
        return false;
    }
    ClearBuildGameSigningPasswords();
    console_.Info("Packaging", "Package job started for " + std::string{ targetSpec.displayName } + '.');
    return true;
#endif
}

void EditorSceneContext::CancelBuildGamePackage() noexcept {
    if (buildGamePackageService_ != nullptr) buildGamePackageService_->Cancel();
}

} // namespace kb::editor
