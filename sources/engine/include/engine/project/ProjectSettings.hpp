#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

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
inline constexpr float kProjectGravityLimit = 1000.0F;
inline constexpr float kProjectTimeScaleMax = 100.0F;

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
    // The scene the editor had open when it was last closed. Deliberately separate
    // from defaultMap: where an author left off is not where the game begins.
    std::string lastOpenMap;

    // Rendering
    // Deferred by default: any number of lights, GI, fog and contact shadows.
    ProjectSceneLightingPath lightingPath = ProjectSceneLightingPath::Deferred;
    // Shadow and global illumination quality, for computers and for phones and tablets.
    ProjectQualityLevel shadowQuality = ProjectQualityLevel::Auto;
    ProjectQualityLevel shadowQualityMobile = ProjectQualityLevel::Auto;
    ProjectQualityLevel giQuality = ProjectQualityLevel::Auto;
    ProjectQualityLevel giQualityMobile = ProjectQualityLevel::Auto;

    // Display
    ProjectDisplaySettings display;

    // Input
    bool inputEnabled = true;
    std::string inputMappingContext;

    // Physics and time
    std::string physicsLayersAsset;
    ProjectSimulationSettings simulation;

    // Crash reports
    // Where a shipped Windows game sends the crash reports its player agreed to
    // send. Empty sends nothing; only HTTPS, or HTTP to this machine, is accepted.
    std::string crashReportUploadUrl;

    [[nodiscard]] bool operator==(const ProjectSettings&) const noexcept = default;
};

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
