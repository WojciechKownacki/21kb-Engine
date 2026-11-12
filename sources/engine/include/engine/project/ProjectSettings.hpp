#pragma once

#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/SceneNavigation.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::project {

// How much a project spends on shadows or global illumination. Auto leaves the choice to the engine;
// the other levels pin one, from the cheapest (Low) to the most detailed (Ultra). A project sets a
// level for computers and another for phones and tablets.
enum class ProjectQualityLevel : std::uint8_t {
    Auto = 0,
    Low,
    Medium,
    High,
    Ultra,
};

inline constexpr std::uint8_t kProjectQualityLevelCount = 5U;

// The level's name as the settings file and the command line spell it ("Auto", "Low", ...).
[[nodiscard]] std::string_view ProjectQualityLevelName(ProjectQualityLevel level) noexcept;
// The level a name stands for, whatever its letter case; nullopt for anything else.
[[nodiscard]] std::optional<ProjectQualityLevel> ParseProjectQualityLevel(std::string_view name) noexcept;

enum class ProjectWindowMode : std::uint8_t {
    Windowed = 0,
    // A frameless window covering the primary monitor at its native resolution.
    BorderlessFullscreen,
};

// Bounds a settings file may use; a value outside them is brought back inside when it is read.
inline constexpr std::uint32_t kProjectWindowExtentMin = 64U;
inline constexpr std::uint32_t kProjectWindowExtentMax = 8192U;
inline constexpr std::uint32_t kProjectFixedStepHzMin = 10U;
inline constexpr std::uint32_t kProjectFixedStepHzMax = 480U;
inline constexpr std::uint32_t kProjectFrameRateCapMax = 1000U;
inline constexpr float kProjectRenderScaleMin = 0.5F;
inline constexpr float kProjectRenderScaleMax = 1.0F;
inline constexpr float kProjectGravityLimit = 1000.0F;
inline constexpr float kProjectTimeScaleMax = 100.0F;
// Bounds of the crowd level-of-detail settings: distances in metres, the far path interval in steps.
inline constexpr float kProjectNavigationDistanceMax = 100000.0F;
inline constexpr std::uint32_t kProjectNavigationFarPathIntervalMax = 600U;
// Most scenes a project lists for its package besides the startup scene.
inline constexpr std::size_t kProjectAdditionalScenesMax = 256U;
// Most tags a project defines of its own: the scene's tag catalogue holds 256 names, six of them built in.
inline constexpr std::size_t kProjectTagsMax = 250U;

// The graphics API an Android package renders with. Auto leaves the choice to the renderer: Vulkan where
// the build offers it, otherwise OpenGL ES. A named API that the device cannot start falls back to the other.
enum class ProjectAndroidGraphicsApi : std::uint8_t {
    Auto = 0,
    Vulkan,
    OpenGLES,
};

inline constexpr std::uint8_t kProjectAndroidGraphicsApiCount = 3U;

// How the launch screen fits its image into the window.
enum class ProjectSplashScale : std::uint8_t {
    // The whole image, in proportion, inside the window.
    Fit = 0,
    // The image in proportion, covering the whole window.
    Fill,
    // The image at its own size in the middle of the window.
    Center,
};

inline constexpr std::uint8_t kProjectSplashScaleCount = 3U;
inline constexpr std::uint32_t kProjectSplashMinimumMillisecondsMax = 60'000U;

// The screen a shipped game shows while its scene loads: at startup, and again whenever a script changes scene.
struct ProjectSplashSettings {
    // Project asset path of the image ("/Game/..."); empty shows the engine's own mark.
    std::string image;
    // Colour behind the image, red, green and blue from 0 to 1.
    std::array<float, 3U> backgroundColor{ 0.06F, 0.07F, 0.09F };
    ProjectSplashScale scale = ProjectSplashScale::Fit;
    // The least time the screen stays up once shown, in milliseconds. 0 hides it as soon as the scene is ready.
    std::uint32_t minimumDisplayMilliseconds = 0U;
    // Whether a bar under the image shows how far the load has got.
    bool showProgress = true;

    [[nodiscard]] bool operator==(const ProjectSplashSettings&) const noexcept = default;
};

// How the shipped game opens its window and paces its frames. The defaults are what the game did
// before these were settings.
struct ProjectDisplaySettings {
    std::uint32_t windowWidth = 1280U;
    std::uint32_t windowHeight = 720U;
    ProjectWindowMode windowMode = ProjectWindowMode::Windowed;
    bool resizable = true;
    bool vsync = true;
    // Frames per second the game does not exceed; 0 leaves the pace to vsync (or nothing, without it).
    std::uint32_t frameRateCap = 0U;
    // Fraction of the output resolution the 3D scene renders at before it is upscaled; the UI stays at
    // full resolution. 1 renders at the output resolution with no upscale pass.
    float renderScale = 1.0F;
    // The graphics API of the Android package; the other platforms ignore it.
    ProjectAndroidGraphicsApi androidGraphicsApi = ProjectAndroidGraphicsApi::Auto;
    ProjectSplashSettings splash;

    [[nodiscard]] bool operator==(const ProjectDisplaySettings&) const noexcept = default;
};

// The simulation's pace and gravity: the fixed step the physics advances by (in steps per second), the
// scene gravity in metres per second squared, and the time scale a scene starts with.
struct ProjectSimulationSettings {
    std::uint32_t fixedStepHz = 60U;
    std::array<float, 3U> gravity{ 0.0F, -9.81F, 0.0F };
    float timeScale = 1.0F;

    [[nodiscard]] bool operator==(const ProjectSimulationSettings&) const noexcept = default;
};

// The project's settings, as opposed to its structure. The descriptor answers
// what a project is made of - engine association, content root, modules, plugins,
// target platforms - and changes when the project is built differently. These
// answer how it is configured, and change while working on it, which is why they
// live in a file a person can open and read.
struct ProjectSettings {
    // Identity
    std::string name = "Project";
    // The name the finished game ships under. The project may be called something
    // else entirely while it is being made.
    std::string gameName;
    std::string category;
    std::string description;

    // Shipping identity. These values are consumed by every platform packager and
    // deliberately live in one project file instead of per-platform editor state.
    std::string publisher;
    std::string version = "1.0.0";
    std::string executableName;
    // Project-relative path. Importing an icon copies it into the project before
    // this value is committed, so builds never depend on a workstation path.
    std::string applicationIcon;

    // Android package metadata. The human-readable version is `version` above.
    std::string androidApplicationId;
    std::uint32_t androidVersionCode = 1U;
    std::string androidLabel;

    // Maps
    // The scene the game starts from. A new project points at the scene the editor
    // creates for it, which is where the default used to live before the descriptor
    // stopped carrying settings.
    std::string defaultMap = "/Game/Scenes/Main.21kbscene";
    // The scenes a package holds besides defaultMap, in order. Empty packages the startup scene alone, plus
    // whatever the scenes reference. See ProjectBuildScenes.
    std::vector<std::string> additionalScenes;
    // The scene the editor had open when it was last closed. Deliberately separate
    // from defaultMap: where an author left off is not where the game begins.
    std::string lastOpenMap;

    // Rendering
    // Auto by default: Deferred (any number of lights, GI, fog and contact shadows) on computers, Forward on
    // phones and tablets.
    ProjectSceneLightingPath lightingPath = ProjectSceneLightingPath::Auto;
    // Shadow and global illumination quality, for computers and for phones and tablets.
    ProjectQualityLevel shadowQuality = ProjectQualityLevel::Auto;
    ProjectQualityLevel shadowQualityMobile = ProjectQualityLevel::Auto;
    ProjectQualityLevel giQuality = ProjectQualityLevel::Auto;
    ProjectQualityLevel giQualityMobile = ProjectQualityLevel::Auto;

    // Display
    ProjectDisplaySettings display;

    // Audio
    // The master gain of the game, 0 (silent) to 1 (full); it scales every sound after the mixer.
    float masterVolume = 1.0F;
    // Whether the game records from the microphone. An Android package declares the record-audio permission
    // only when this is on.
    bool microphone = false;

    // Tags
    // The tags the project defines, in order, besides the built-in ones. Every scene of the project starts
    // from this list; a tag only a scene file defines is added here when the editor opens that scene.
    std::vector<std::string> tags;

    // Input
    bool inputEnabled = true;
    std::string inputMappingContext;

    // Physics and time
    std::string physicsLayersAsset;
    ProjectSimulationSettings simulation;

    // Navigation
    // The named navigation areas; an area's id is its position in the list. Scenes store the ids.
    std::vector<kb::scene::NavAreaDefinition> navigationAreas = kb::scene::DefaultNavAreaDefinitions();
    // The crowd's level of detail around the camera and the Stream Focus entities (see
    // kb::scene::NavigationCrowdSettings): agents within the near distance avoid each other fully,
    // within the mid distance they only keep apart, and farther ones follow their paths, shortening
    // them every `navigationFarPathInterval` steps.
    float navigationNearDistance = kb::scene::NavigationCrowdSettings{}.nearDistance;
    float navigationMidDistance = kb::scene::NavigationCrowdSettings{}.midDistance;
    std::uint32_t navigationFarPathInterval = kb::scene::NavigationCrowdSettings{}.farPathOptimizationInterval;

    // Crash reports
    // Where a shipped Windows game sends the crash reports its player agreed to
    // send. Empty sends nothing; only HTTPS, or HTTP to this machine, is accepted.
    std::string crashReportUploadUrl;

    [[nodiscard]] bool operator==(const ProjectSettings&) const noexcept = default;

    // The crowd settings the project's scenes play with: its level of detail over the engine defaults.
    [[nodiscard]] kb::scene::NavigationCrowdSettings NavigationCrowd() const noexcept {
        kb::scene::NavigationCrowdSettings crowd;
        crowd.nearDistance = navigationNearDistance;
        crowd.midDistance = navigationMidDistance;
        crowd.farPathOptimizationInterval = navigationFarPathInterval;
        return crowd;
    }
};

// The scenes a package holds, in order: the startup scene (defaultMap) first, then the additional scenes.
// An empty defaultMap contributes nothing and a scene listed twice counts once.
[[nodiscard]] std::vector<std::string> ProjectBuildScenes(const ProjectSettings& settings);
// Sets the scenes a package holds from an ordered list: its first scene becomes the startup scene. Empty
// and repeated entries are dropped; an empty list leaves the settings as they are.
void SetProjectBuildScenes(ProjectSettings& settings, std::vector<std::string> scenes);

// Adds a tag to the project's list. False, and nothing changes, for a name that is not a valid tag, is built in,
// is listed already or would pass kProjectTagsMax.
[[nodiscard]] bool AddProjectTag(ProjectSettings& settings, std::string_view name);
// Takes a tag out of the list; false when it was not there.
[[nodiscard]] bool RemoveProjectTag(ProjectSettings& settings, std::string_view name);

// The settings an older project file carried before the settings file existed.
struct ProjectLegacySettings {
    bool present = false;
    std::string name;
    std::string category;
    std::string description;
    std::string defaultScene;
    ProjectSceneLightingPath lightingPath = ProjectSceneLightingPath::Forward;
    bool inputEnabled = true;
    std::string inputMappingContext;
    std::string physicsLayersAsset;
};

struct ProjectSettingsLoadResult {
    ProjectSettings settings{};
    bool found = false;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept { return error.empty(); }
};

class ProjectSettingsStore final {
public:
    ProjectSettingsStore() = delete;

    // <project root>/Config/ProjectSettings.ini
    [[nodiscard]] static std::filesystem::path FilePath(const std::filesystem::path& projectRoot);

    // A missing file is not an error: it reports found=false with default settings,
    // so a caller can seed the file from whatever the project already knows.
    [[nodiscard]] static ProjectSettingsLoadResult Load(const std::filesystem::path& path);
    [[nodiscard]] static bool Save(
        const std::filesystem::path& path,
        const ProjectSettings& settings,
        std::string& error);

    // The settings a project carried before it had a settings file, so an existing
    // project keeps its configuration when the file is first created. A project with
    // nothing to carry gets the defaults, named after its own file.
    [[nodiscard]] static ProjectSettings FromLegacy(
        const ProjectLegacySettings& legacy,
        const std::filesystem::path& projectFile);
};

} // namespace kb::project
