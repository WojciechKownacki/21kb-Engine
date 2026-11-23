#include "scene/EditorSceneContext.hpp"
#include "scene/EditorEngineInstallation.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "localization/EditorLocalization.hpp"
#include "packaging/EditorAndroidSigningBroker.hpp"
#include "packaging/EditorPackageInputValidation.hpp"
#include "packaging/EditorPackageRequestBuilder.hpp"
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
#include <array>
#include <cmath>
#include <cctype>
#include <string>
#include <system_error>
#include <vector>

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
    if (!value.empty()) {
        volatile char* bytes = value.data();
        for (std::size_t index = 0U; index < value.size(); ++index) bytes[index] = '\0';
    }
    value.clear();
    value.shrink_to_fit();
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
bool EditorSceneContext::HasBuildGameSecret(BuildGameField field) const noexcept {
    switch (field) {
    case BuildGameField::AndroidStorePassword: return !buildGameStorePassword_.empty();
    case BuildGameField::AndroidKeyPassword: return !buildGameKeyPassword_.empty();
    case BuildGameField::WindowsCertificatePassword: return !buildGameCertificatePassword_.empty();
    default: return false;
    }
}
void EditorSceneContext::ClearBuildGameSigningPasswords() noexcept {
    SecureClear(buildGameStorePassword_);
    SecureClear(buildGameKeyPassword_);
    SecureClear(buildGameCertificatePassword_);
}

bool EditorSceneContext::BeginBuildGameTextEdit(BuildGameField field) {
    const std::vector<BuildGameField> order = BuildGamePanelModel::TextFocusOrder(
        BuildGameTarget(), buildGameSelectedProfile_ == 1);
    if (!Contains(order, field)) return false;
    buildGameEditingField_ = field;
    if (field == BuildGameField::AndroidStorePassword) buildGameEditBuffer_ = buildGameStorePassword_;
    else if (field == BuildGameField::AndroidKeyPassword) buildGameEditBuffer_ = buildGameKeyPassword_;
    else if (field == BuildGameField::WindowsCertificatePassword) buildGameEditBuffer_ = buildGameCertificatePassword_;
    else buildGameEditBuffer_ = BuildGamePanelModel::Value(field, BuildGameTarget(), projectConfig_, buildGameSettings_);
    buildGameEditOriginal_ = buildGameEditBuffer_;
    buildGameEditHighSurrogate_ = 0;
    buildGameEditSelectAll_ = true;
    return true;
}

bool EditorSceneContext::AppendBuildGameText(wchar_t character) {
    // Typed characters arrive as UTF-16 units: a character outside the first plane comes as two.
    char32_t codePoint = static_cast<char32_t>(character);
    if (character >= 0xD800 && character <= 0xDBFF) {
        buildGameEditHighSurrogate_ = character;
        return false;
    }
    if (character >= 0xDC00 && character <= 0xDFFF) {
        if (buildGameEditHighSurrogate_ == 0) return false;
        codePoint = 0x10000U + ((static_cast<char32_t>(buildGameEditHighSurrogate_) - 0xD800U) << 10U) +
            (static_cast<char32_t>(character) - 0xDC00U);
    }
    buildGameEditHighSurrogate_ = 0;
    if (codePoint < 32U) return false;
    std::string utf8;
    if (codePoint < 0x80U) {
        utf8.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800U) {
        utf8.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        utf8.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else if (codePoint < 0x10000U) {
        utf8.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
        utf8.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        utf8.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else {
        utf8.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
        utf8.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
        utf8.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        utf8.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
    return InsertBuildGameText(utf8);
}

bool EditorSceneContext::InsertBuildGameText(std::string_view text) {
    const bool sensitive = BuildGamePanelModel::IsSecret(buildGameEditingField_);
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
    // A whole character, never half of one.
    while (!buildGameEditBuffer_.empty() && (static_cast<unsigned char>(buildGameEditBuffer_.back()) & 0xC0U) == 0x80U) {
        buildGameEditBuffer_.pop_back();
    }
    if (!buildGameEditBuffer_.empty()) buildGameEditBuffer_.pop_back();
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
    const bool passwordField = BuildGamePanelModel::IsSecret(buildGameEditingField_);
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
            ? buildGameStorePassword_
            : buildGameEditingField_ == BuildGameField::AndroidKeyPassword ? buildGameKeyPassword_ : buildGameCertificatePassword_;
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
    const bool sensitive = BuildGamePanelModel::IsSecret(buildGameEditingField_);
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
    if (path.empty() || (field != BuildGameField::AndroidKeystore && field != BuildGameField::LinuxIdentity &&
            field != BuildGameField::WindowsCertificateFile)) return false;
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(path, filesystemError) || filesystemError) {
        console_.Error("Packaging", "The selected file does not exist.");
        return false;
    }
    EditorBuildGameSettings candidate = buildGameSettings_;
    if (field == BuildGameField::AndroidKeystore) {
        candidate.For(BuildGameTarget()).androidKeystore = AbsoluteNormalized(path);
    } else if (field == BuildGameField::WindowsCertificateFile) {
        candidate.For(BuildGameTarget()).windowsCertificateFile = AbsoluteNormalized(path);
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

bool EditorSceneContext::SetBuildGameAndroidFormat(std::string_view format) {
    kb::project::ProjectSettings project = projectConfig_;
    EditorBuildGameSettings candidate = buildGameSettings_;
    std::string error;
    if (!BuildGamePanelModel::ApplyText(BuildGameField::AndroidFormat, format, BuildGameTarget(), project, candidate, error)) {
        console_.Error("Packaging", error);
        return false;
    }
    if (candidate == buildGameSettings_) return false;
    if (!EditorBuildGameSettingsStore::Save(EditorBuildGameSettingsStore::FilePath(EditorProjectPaths::ProjectRoot()), candidate, error)) {
        console_.Error("Packaging", error.empty() ? "Packaging settings could not be saved." : error);
        return false;
    }
    buildGameSettings_ = std::move(candidate);
    return true;
}

bool EditorSceneContext::CreateBuildGameUploadKey(const std::filesystem::path& keystore) {
    if (buildGameStorePassword_.empty()) {
        console_.Error("Packaging", "Type the keystore password (at least 6 characters) in the Release profile first; "
            "it becomes the new key's password.");
        return false;
    }
    // A signing key never lives in the project: projects are shared and put under version control.
    const std::filesystem::path target = AbsoluteNormalized(keystore);
    if (package_input::IsPathInsideOrEqual(AbsoluteNormalized(EditorProjectPaths::ProjectRoot()), target)) {
        console_.Error("Packaging", "Save the upload key outside the project folder, where it is not shared with the project.");
        return false;
    }
    std::string error;
    if (!EditorAndroidSigningBroker::CreateUploadKey(target, "upload", Trimmed(projectConfig_.publisher),
            buildGameStorePassword_, error)) {
        console_.Error("Packaging", error);
        return false;
    }
    EditorBuildGameSettings candidate = buildGameSettings_;
    candidate.For(BuildGameTarget()).androidKeystore = target;
    candidate.For(BuildGameTarget()).androidKeyAlias = "upload";
    if (!EditorBuildGameSettingsStore::Save(EditorBuildGameSettingsStore::FilePath(EditorProjectPaths::ProjectRoot()), candidate, error)) {
        console_.Error("Packaging", error.empty() ? "Packaging settings could not be saved." : error);
        return false;
    }
    buildGameSettings_ = std::move(candidate);
    SecureClear(buildGameKeyPassword_);
    buildGameKeyPassword_ = buildGameStorePassword_;
    console_.Info("Packaging", "Upload key created at " + target.generic_string() +
        ". Keep a copy of this file and its password: every update of the game on Google Play must be signed with it.");
    return true;
}

bool EditorSceneContext::ImportBuildGameLoadingScreenImage(const std::filesystem::path& path) {
    // The picture becomes a texture of the project, so every package carries it, and the loading screen of every
    // target shows it (the same setting as Project Settings > Display > Loading screen).
    const std::filesystem::path folder{ "/Game/Branding" };
    const std::array<std::filesystem::path, 1U> files{ path };
    if (!ImportAssetFiles(files, folder)) {
        return false;
    }
    const std::string wanted = kb::assets::NormalizeAssetPath(folder / path.filename());
    std::string imported;
    for (const std::string& option : ProjectSplashImageOptions()) {
        if (!option.empty() && kb::assets::NormalizeAssetPath(std::filesystem::path{ option }.replace_extension()) ==
                kb::assets::NormalizeAssetPath(std::filesystem::path{ wanted }.replace_extension())) {
            imported = option;
        }
    }
    if (imported.empty()) {
        console_.Error("Packaging", "The loading screen picture was imported but is not a texture: " + path.filename().string());
        return false;
    }
    kb::project::ProjectSettings candidate = projectConfig_;
    candidate.display.splash.image = imported;
    const kb::project::ProjectSettings original = projectConfig_;
    projectConfig_ = std::move(candidate);
    if (!SaveProjectConfiguration()) {
        projectConfig_ = original;
        return false;
    }
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

std::vector<std::string> EditorSceneContext::ProjectScenePaths() const {
    std::vector<std::string> scenes;
    for (const kb::assets::AssetMetadata& asset : scene_->Assets().Manager().Registry().All()) {
        const std::string path = asset.virtualPath.generic_string();
        if (asset.virtualPath.extension() == ".21kbscene" && path.rfind("/Game/", 0) == 0) {
            scenes.push_back(path);
        }
    }
    std::sort(scenes.begin(), scenes.end());
    scenes.erase(std::unique(scenes.begin(), scenes.end()), scenes.end());
    return scenes;
}

bool EditorSceneContext::SetStartupScene(const std::string& virtualPath) {
    const std::vector<std::string> scenes = ProjectScenePaths();
    if (std::find(scenes.begin(), scenes.end(), virtualPath) == scenes.end()) {
        console_.Error("Project", "The game starts in a scene of this project; " + virtualPath + " is not one.");
        return false;
    }
    // The scene leads the list of scenes the package holds, in place of the previous startup scene.
    std::vector<std::string> packaged{ virtualPath };
    packaged.insert(packaged.end(), projectConfig_.additionalScenes.begin(), projectConfig_.additionalScenes.end());
    kb::project::SetProjectBuildScenes(projectConfig_, std::move(packaged));
    if (!SaveProjectConfiguration()) {
        return false;
    }
    console_.Info("Project", "The game now starts in " + virtualPath + ".");
    return true;
}

std::string EditorSceneContext::StartupSceneProblem() const {
    if (projectConfig_.defaultMap.empty()) {
        return {};
    }
    const std::vector<std::string> scenes = ProjectScenePaths();
    if (std::find(scenes.begin(), scenes.end(), projectConfig_.defaultMap) != scenes.end()) {
        return {};
    }
    return EditorLocalization::Format("buildGame.invalid.startupMapMissing",
        "Startup map {map} is not a scene of this project. Choose one from the list.",
        { { "map", projectConfig_.defaultMap } });
}

bool EditorSceneContext::StartBuildGamePackage(EditorDirtySceneResolution dirtyResolution) {
    if (buildGamePackageService_ == nullptr) return false;
    if (IsBuildGameTextEditing() && !CommitBuildGameTextEdit()) return false;
    const bool release = buildGameSelectedProfile_ == 1;
    const BuildGameValidation validation = BuildGamePanelModel::Validate(
        BuildGameTarget(), projectConfig_, buildGameSettings_, release, buildGamePackageService_->IsRunning(),
        HasBuildGameStorePassword(), HasBuildGameKeyPassword(), HasBuildGameSecret(BuildGameField::WindowsCertificatePassword));
    if (!validation.canBuild) {
        console_.Error("Packaging", validation.reason);
        return false;
    }
    if (const std::string problem = StartupSceneProblem(); !problem.empty()) {
        console_.Error("Packaging", problem);
        return false;
    }
    // Save writes the edited scene first, Don't Save packages the saved one.
    if (dirtyResolution == EditorDirtySceneResolution::Save && !SaveDirtySceneDocument("building the game")) {
        return false;
    }
    if (!SaveProjectConfiguration()) {
        console_.Error("Packaging", "The project settings must be saved before packaging.");
        return false;
    }

    const auto installation = ResolveEditorEngineInstallation();
    if (installation.root.empty()) {
        console_.Error("Packaging", "The editor package root is unavailable.");
        return false;
    }
    const auto targets = kb::packaging::PackagingTargets();
    if (buildGameSelectedTarget_ < 0 || buildGameSelectedTarget_ >= static_cast<int>(targets.size())) {
        console_.Error("Packaging", "The selected package target is invalid.");
        return false;
    }
    const kb::packaging::PackagingTargetSpec& targetSpec = targets[static_cast<std::size_t>(buildGameSelectedTarget_)];
    EditorPackageRequest request;
    std::string requestError;
    if (!BuildPackageRequest({ .project = projectConfig_, .local = buildGameSettings_, .target = targetSpec, .release = release,
            .projectFile = projectFile_, .projectRoot = EditorProjectPaths::ProjectRoot(), .engineRoot = installation.root,
            .androidStorePassword = buildGameStorePassword_, .androidKeyPassword = buildGameKeyPassword_,
            .windowsCertificatePassword = buildGameCertificatePassword_ }, request, requestError)) {
        console_.Error("Packaging", requestError);
        return false;
    }
    std::string error;
    if (!buildGamePackageService_->Start(std::move(request), error)) {
        console_.Error("Packaging", error);
        return false;
    }
    ClearBuildGameSigningPasswords();
    console_.Info("Packaging", "Package job started for " + std::string{ targetSpec.displayName } + '.');
    return true;
}

void EditorSceneContext::CancelBuildGamePackage() noexcept {
    if (buildGamePackageService_ != nullptr) buildGamePackageService_->Cancel();
}

} // namespace kb::editor
