#include "engine/project/ProjectSettings.hpp"

#include "engine/config/IniDocument.hpp"
#include "engine/scene/SceneTagCatalog.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace kb::project {
namespace {

constexpr std::string_view kIdentity = "Project.Identity";
constexpr std::string_view kProduct = "Project.Product";
constexpr std::string_view kAndroid = "Project.Android";
constexpr std::string_view kMaps = "Project.Maps";
constexpr std::string_view kBuildScenes = "Project.BuildScenes";
constexpr std::string_view kRendering = "Project.Rendering";
constexpr std::string_view kDisplay = "Project.Display";
constexpr std::string_view kInput = "Project.Input";
constexpr std::string_view kPhysics = "Project.Physics";
constexpr std::string_view kTime = "Project.Time";
constexpr std::string_view kAudio = "Project.Audio";
constexpr std::string_view kTags = "Project.Tags";
constexpr std::string_view kCrashReports = "Project.CrashReports";

[[nodiscard]] std::string_view LightingPathName(ProjectSceneLightingPath path) noexcept {
    switch (path) {
    case ProjectSceneLightingPath::Deferred: return "Deferred";
    case ProjectSceneLightingPath::ForwardPlus: return "ForwardPlus";
    case ProjectSceneLightingPath::Forward:
    default: return "Forward";
    }
}

[[nodiscard]] ProjectSceneLightingPath ParseLightingPath(std::string_view name, ProjectSceneLightingPath fallback) noexcept {
    if (name == "Forward") return ProjectSceneLightingPath::Forward;
    if (name == "Deferred") return ProjectSceneLightingPath::Deferred;
    if (name == "ForwardPlus") return ProjectSceneLightingPath::ForwardPlus;
    return fallback;
}

[[nodiscard]] std::string_view WindowModeName(ProjectWindowMode mode) noexcept {
    return mode == ProjectWindowMode::BorderlessFullscreen ? "BorderlessFullscreen" : "Windowed";
}

[[nodiscard]] ProjectWindowMode ParseWindowMode(std::string_view name, ProjectWindowMode fallback) noexcept {
    if (name == "Windowed") return ProjectWindowMode::Windowed;
    if (name == "BorderlessFullscreen") return ProjectWindowMode::BorderlessFullscreen;
    return fallback;
}

// An integer setting kept inside its bounds; a missing or unreadable key keeps the default.
[[nodiscard]] std::uint32_t ReadBounded(
    const config::IniDocument& document, std::string_view section, std::string_view key,
    std::uint32_t fallback, std::uint32_t minimum, std::uint32_t maximum) {
    const std::optional<std::int64_t> value = document.GetInt(section, key);
    return value.has_value()
        ? static_cast<std::uint32_t>(std::clamp<std::int64_t>(*value, minimum, maximum)) : fallback;
}

[[nodiscard]] float ReadBoundedFloat(
    const config::IniDocument& document, std::string_view section, std::string_view key,
    float fallback, float minimum, float maximum) {
    return std::clamp(document.GetFloat(section, key).value_or(fallback), minimum, maximum);
}

[[nodiscard]] std::string ReadString(const config::IniDocument& document, std::string_view section, std::string_view key, std::string fallback) {
    const std::optional<std::string_view> value = document.GetString(section, key);
    return value.has_value() ? std::string{*value} : std::move(fallback);
}

// A scene path added to a list unless it is empty or already there.
void AppendScene(std::vector<std::string>& scenes, const std::string& scene) {
    if (!scene.empty() && std::ranges::find(scenes, scene) == scenes.end()) {
        scenes.push_back(scene);
    }
}

// The tag a name stands for in the project's list, empty when it cannot be listed: not a valid tag name, a built-in
// tag, or one the list holds already.
[[nodiscard]] std::string ListableTag(const std::vector<std::string>& tags, std::string_view name) {
    std::string tag = kb::scene::SceneTagCatalog::CanonicalName(name);
    if (kb::scene::SceneTagCatalog::IsBuiltIn(tag) || std::ranges::find(tags, tag) != tags.end()) {
        return {};
    }
    return tag;
}

} // namespace

std::string_view ProjectQualityLevelName(ProjectQualityLevel level) noexcept {
    switch (level) {
    case ProjectQualityLevel::Low: return "Low";
    case ProjectQualityLevel::Medium: return "Medium";
    case ProjectQualityLevel::High: return "High";
    case ProjectQualityLevel::Ultra: return "Ultra";
    case ProjectQualityLevel::Auto:
    default: return "Auto";
    }
}

std::optional<ProjectQualityLevel> ParseProjectQualityLevel(std::string_view name) noexcept {
    const auto same = [name](std::string_view expected) noexcept {
        return std::ranges::equal(name, expected, [](char lhs, char rhs) noexcept {
            return (lhs >= 'A' && lhs <= 'Z' ? static_cast<char>(lhs + ('a' - 'A')) : lhs) == rhs;
        });
    };
    if (same("auto")) return ProjectQualityLevel::Auto;
    if (same("low")) return ProjectQualityLevel::Low;
    if (same("medium")) return ProjectQualityLevel::Medium;
    if (same("high")) return ProjectQualityLevel::High;
    if (same("ultra")) return ProjectQualityLevel::Ultra;
    return std::nullopt;
}

std::vector<std::string> ProjectBuildScenes(const ProjectSettings& settings) {
    std::vector<std::string> scenes;
    AppendScene(scenes, settings.defaultMap);
    for (const std::string& scene : settings.additionalScenes) {
        AppendScene(scenes, scene);
    }
    return scenes;
}

void SetProjectBuildScenes(ProjectSettings& settings, std::vector<std::string> scenes) {
    std::vector<std::string> unique;
    for (const std::string& scene : scenes) {
        AppendScene(unique, scene);
    }
    if (unique.empty()) {
        return;
    }
    settings.defaultMap = unique.front();
    settings.additionalScenes.assign(unique.begin() + 1, unique.end());
}

bool AddProjectTag(ProjectSettings& settings, std::string_view name) {
    std::string tag = ListableTag(settings.tags, name);
    if (tag.empty() || settings.tags.size() >= kProjectTagsMax) {
        return false;
    }
    settings.tags.push_back(std::move(tag));
    return true;
}

bool RemoveProjectTag(ProjectSettings& settings, std::string_view name) {
    const std::string tag = kb::scene::SceneTagCatalog::CanonicalName(name);
    return std::erase(settings.tags, tag) != 0U;
}

std::filesystem::path ProjectSettingsStore::FilePath(const std::filesystem::path& projectRoot) {
    return projectRoot / "Config" / "ProjectSettings.ini";
}

ProjectSettings ProjectSettingsStore::FromLegacy(
    const ProjectLegacySettings& legacy,
    const std::filesystem::path& projectFile) {
    ProjectSettings settings;
    if (!legacy.present) {
        const std::string stem = projectFile.stem().string();
        if (!stem.empty()) {
            settings.name = stem;
            settings.gameName = stem;
            settings.executableName = stem;
            settings.androidLabel = stem;
        }
        return settings;
    }
    settings.name = legacy.name;
    settings.gameName = legacy.name;
    settings.category = legacy.category;
    settings.description = legacy.description;
    settings.executableName = legacy.name;
    settings.androidLabel = legacy.name;
    settings.defaultMap = legacy.defaultScene;
    settings.lightingPath = legacy.lightingPath;
    settings.inputEnabled = legacy.inputEnabled;
    settings.inputMappingContext = legacy.inputMappingContext;
    settings.physicsLayersAsset = legacy.physicsLayersAsset;
    return settings;
}

ProjectSettingsLoadResult ProjectSettingsStore::Load(const std::filesystem::path& path) {
    ProjectSettingsLoadResult result;

    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError) || filesystemError) {
        return result;
    }

    config::IniDocument document;
    if (!document.Load(path, result.error)) {
        return result;
    }
    result.found = true;

    const ProjectSettings defaults;
    result.settings.name = ReadString(document, kIdentity, "Name", defaults.name);
    result.settings.gameName = ReadString(document, kIdentity, "GameName", defaults.gameName);
    result.settings.category = ReadString(document, kIdentity, "Category", defaults.category);
    result.settings.description = ReadString(document, kIdentity, "Description", defaults.description);
    result.settings.publisher = ReadString(document, kProduct, "Publisher", defaults.publisher);
    result.settings.version = ReadString(document, kProduct, "Version", defaults.version);
    result.settings.executableName = ReadString(document, kProduct, "ExecutableName", defaults.executableName);
    result.settings.applicationIcon = ReadString(document, kProduct, "ApplicationIcon", defaults.applicationIcon);
    result.settings.androidApplicationId = ReadString(document, kAndroid, "ApplicationId", defaults.androidApplicationId);
    result.settings.androidVersionCode = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        document.GetInt(kAndroid, "VersionCode").value_or(defaults.androidVersionCode), 1, 2'100'000'000));
    result.settings.androidLabel = ReadString(document, kAndroid, "Label", defaults.androidLabel);
    result.settings.defaultMap = ReadString(document, kMaps, "DefaultMap", defaults.defaultMap);
    result.settings.lastOpenMap = ReadString(document, kMaps, "LastOpenMap", defaults.lastOpenMap);
    const std::int64_t additionalCount = std::clamp<std::int64_t>(
        document.GetInt(kBuildScenes, "Count").value_or(0), 0, static_cast<std::int64_t>(kProjectAdditionalScenesMax));
    for (std::int64_t index = 0; index < additionalCount; ++index) {
        if (const auto scene = document.GetString(kBuildScenes, "Scene" + std::to_string(index)); scene.has_value()) {
            AppendScene(result.settings.additionalScenes, std::string{ *scene });
        }
    }
    // The startup scene leads the list; a scene named there as well is not packaged twice.
    std::erase(result.settings.additionalScenes, result.settings.defaultMap);
    result.settings.lightingPath = ParseLightingPath(
        ReadString(document, kRendering, "LightingPath", std::string{LightingPathName(defaults.lightingPath)}),
        defaults.lightingPath);
    const auto readQuality = [&](std::string_view key, ProjectQualityLevel fallback) {
        return ParseProjectQualityLevel(ReadString(document, kRendering, key, std::string{ProjectQualityLevelName(fallback)}))
            .value_or(fallback);
    };
    result.settings.shadowQuality = readQuality("ShadowQuality", defaults.shadowQuality);
    result.settings.shadowQualityMobile = readQuality("ShadowQualityMobile", defaults.shadowQualityMobile);
    result.settings.giQuality = readQuality("GiQuality", defaults.giQuality);
    result.settings.giQualityMobile = readQuality("GiQualityMobile", defaults.giQualityMobile);
    ProjectDisplaySettings& display = result.settings.display;
    display.windowWidth = ReadBounded(document, kDisplay, "WindowWidth", defaults.display.windowWidth,
        kProjectWindowExtentMin, kProjectWindowExtentMax);
    display.windowHeight = ReadBounded(document, kDisplay, "WindowHeight", defaults.display.windowHeight,
        kProjectWindowExtentMin, kProjectWindowExtentMax);
    display.windowMode = ParseWindowMode(
        ReadString(document, kDisplay, "WindowMode", std::string{WindowModeName(defaults.display.windowMode)}),
        defaults.display.windowMode);
    display.resizable = document.GetBool(kDisplay, "Resizable").value_or(defaults.display.resizable);
    display.vsync = document.GetBool(kDisplay, "VSync").value_or(defaults.display.vsync);
    display.frameRateCap = ReadBounded(document, kDisplay, "FrameRateCap", defaults.display.frameRateCap,
        0U, kProjectFrameRateCapMax);
    display.renderScale = ReadBoundedFloat(document, kDisplay, "RenderScale", defaults.display.renderScale,
        kProjectRenderScaleMin, kProjectRenderScaleMax);
    result.settings.masterVolume = ReadBoundedFloat(document, kAudio, "MasterVolume", defaults.masterVolume, 0.0F, 1.0F);
    result.settings.microphone = document.GetBool(kAudio, "Microphone").value_or(defaults.microphone);
    const std::int64_t tagCount = std::clamp<std::int64_t>(
        document.GetInt(kTags, "Count").value_or(0), 0, static_cast<std::int64_t>(kProjectTagsMax));
    for (std::int64_t index = 0; index < tagCount; ++index) {
        if (const auto tag = document.GetString(kTags, "Tag" + std::to_string(index)); tag.has_value()) {
            static_cast<void>(AddProjectTag(result.settings, *tag));
        }
    }
    result.settings.inputEnabled = document.GetBool(kInput, "Enabled").value_or(defaults.inputEnabled);
    result.settings.inputMappingContext = ReadString(document, kInput, "MappingContext", defaults.inputMappingContext);
    result.settings.physicsLayersAsset = ReadString(document, kPhysics, "LayersAsset", defaults.physicsLayersAsset);
    ProjectSimulationSettings& simulation = result.settings.simulation;
    simulation.fixedStepHz = ReadBounded(document, kPhysics, "FixedStepHz", defaults.simulation.fixedStepHz,
        kProjectFixedStepHzMin, kProjectFixedStepHzMax);
    constexpr std::array<std::string_view, 3U> gravityKeys{ "GravityX", "GravityY", "GravityZ" };
    for (std::size_t axis = 0U; axis < gravityKeys.size(); ++axis) {
        simulation.gravity[axis] = ReadBoundedFloat(document, kPhysics, gravityKeys[axis],
            defaults.simulation.gravity[axis], -kProjectGravityLimit, kProjectGravityLimit);
    }
    simulation.timeScale = ReadBoundedFloat(document, kTime, "TimeScale", defaults.simulation.timeScale,
        0.0F, kProjectTimeScaleMax);
    result.settings.crashReportUploadUrl = ReadString(document, kCrashReports, "UploadUrl", defaults.crashReportUploadUrl);
    return result;
}

bool ProjectSettingsStore::Save(
    const std::filesystem::path& path,
    const ProjectSettings& settings,
    std::string& error) {
    error.clear();

    // Read first, so anything a person added to the file by hand - a section this
    // build does not know, a comment's neighbouring key - survives the write.
    config::IniDocument document;
    std::error_code filesystemError;
    if (std::filesystem::exists(path, filesystemError) && !filesystemError) {
        std::string loadError;
        if (!document.Load(path, loadError)) {
            document.Clear();
        }
    }

    document.SetString(kIdentity, "Name", settings.name);
    document.SetString(kIdentity, "GameName", settings.gameName);
    document.SetString(kIdentity, "Category", settings.category);
    document.SetString(kIdentity, "Description", settings.description);
    document.SetString(kProduct, "Publisher", settings.publisher);
    document.SetString(kProduct, "Version", settings.version);
    document.SetString(kProduct, "ExecutableName", settings.executableName);
    document.SetString(kProduct, "ApplicationIcon", settings.applicationIcon);
    document.SetString(kAndroid, "ApplicationId", settings.androidApplicationId);
    document.SetInt(kAndroid, "VersionCode",
        static_cast<std::int64_t>(std::clamp<std::uint32_t>(settings.androidVersionCode, 1U, 2'100'000'000U)));
    document.SetString(kAndroid, "Label", settings.androidLabel);
    document.SetString(kMaps, "DefaultMap", settings.defaultMap);
    document.SetString(kMaps, "LastOpenMap", settings.lastOpenMap);
    // The additional scenes as a count and one key each; keys a shorter list leaves behind are removed.
    const std::size_t additionalCount = std::min(settings.additionalScenes.size(), kProjectAdditionalScenesMax);
    if (additionalCount == 0U) {
        document.Remove(kBuildScenes, "Count");
    } else {
        document.SetInt(kBuildScenes, "Count", static_cast<std::int64_t>(additionalCount));
    }
    for (std::size_t index = 0U; index < additionalCount; ++index) {
        document.SetString(kBuildScenes, "Scene" + std::to_string(index), settings.additionalScenes[index]);
    }
    for (std::size_t index = additionalCount; document.Remove(kBuildScenes, "Scene" + std::to_string(index)); ++index) {
    }
    document.SetString(kRendering, "LightingPath", std::string{LightingPathName(settings.lightingPath)});
    document.SetString(kRendering, "ShadowQuality", std::string{ProjectQualityLevelName(settings.shadowQuality)});
    document.SetString(kRendering, "ShadowQualityMobile", std::string{ProjectQualityLevelName(settings.shadowQualityMobile)});
    document.SetString(kRendering, "GiQuality", std::string{ProjectQualityLevelName(settings.giQuality)});
    document.SetString(kRendering, "GiQualityMobile", std::string{ProjectQualityLevelName(settings.giQualityMobile)});
    document.SetInt(kDisplay, "WindowWidth", settings.display.windowWidth);
    document.SetInt(kDisplay, "WindowHeight", settings.display.windowHeight);
    document.SetString(kDisplay, "WindowMode", std::string{WindowModeName(settings.display.windowMode)});
    document.SetBool(kDisplay, "Resizable", settings.display.resizable);
    document.SetBool(kDisplay, "VSync", settings.display.vsync);
    document.SetInt(kDisplay, "FrameRateCap", settings.display.frameRateCap);
    document.SetFloat(kDisplay, "RenderScale", settings.display.renderScale);
    document.SetFloat(kAudio, "MasterVolume", settings.masterVolume);
    document.SetBool(kAudio, "Microphone", settings.microphone);
    // The tags as a count and one key each; keys a shorter list leaves behind are removed.
    const std::size_t tagCount = std::min(settings.tags.size(), kProjectTagsMax);
    if (tagCount == 0U) {
        document.Remove(kTags, "Count");
    } else {
        document.SetInt(kTags, "Count", static_cast<std::int64_t>(tagCount));
    }
    for (std::size_t index = 0U; index < tagCount; ++index) {
        document.SetString(kTags, "Tag" + std::to_string(index), settings.tags[index]);
    }
    for (std::size_t index = tagCount; document.Remove(kTags, "Tag" + std::to_string(index)); ++index) {
    }
    document.SetBool(kInput, "Enabled", settings.inputEnabled);
    document.SetString(kInput, "MappingContext", settings.inputMappingContext);
    document.SetString(kPhysics, "LayersAsset", settings.physicsLayersAsset);
    document.SetInt(kPhysics, "FixedStepHz", settings.simulation.fixedStepHz);
    document.SetFloat(kPhysics, "GravityX", settings.simulation.gravity[0]);
    document.SetFloat(kPhysics, "GravityY", settings.simulation.gravity[1]);
    document.SetFloat(kPhysics, "GravityZ", settings.simulation.gravity[2]);
    document.SetFloat(kTime, "TimeScale", settings.simulation.timeScale);
    document.SetString(kCrashReports, "UploadUrl", settings.crashReportUploadUrl);
    return document.Save(path, error);
}

} // namespace kb::project
