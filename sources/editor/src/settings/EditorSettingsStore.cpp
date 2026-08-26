#include "settings/EditorSettingsStore.hpp"

#include <cmath>
#include <fstream>
#include <locale>
#include <sstream>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {
namespace {

constexpr std::string_view kCurrentHeader = "21kb Editor Settings 2";
constexpr std::string_view kLegacyHeader = "21kb Editor Settings 1";

[[nodiscard]] bool Replace(const std::filesystem::path& source, const std::filesystem::path& target) noexcept {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(source, target, error);
    return !error;
#endif
}

[[nodiscard]] bool BoolValue(int value) noexcept {
    return value == 0 || value == 1;
}

[[nodiscard]] bool Valid(const EditorSettingsDocument& value) noexcept {
    return value.saving.autosaveIntervalMinutes >= 1U &&
        value.saving.autosaveIntervalMinutes <= 120U;
}

[[nodiscard]] bool AtEnd(std::istringstream& stream) {
    stream >> std::ws;
    return stream.eof();
}

[[nodiscard]] bool ParseCurrent(
    std::istringstream& stream,
    EditorSettingsDocument& value) {
    std::string key;
    int autosave = 0;
    unsigned autosaveMinutes = 0U;
    const bool parsed =
        (stream >> key >> autosave) && key == "autosave" &&
        (stream >> key >> autosaveMinutes) && key == "autosave_minutes";
    if (!parsed || !AtEnd(stream) || !BoolValue(autosave)) return false;

    value.saving.autosaveEnabled = autosave != 0;
    value.saving.autosaveIntervalMinutes = autosaveMinutes;
    return Valid(value);
}

[[nodiscard]] bool ParseLegacy(
    std::istringstream& stream,
    EditorSettingsDocument& value) {
    std::string key;
    unsigned backend = 0U;
    unsigned antiAliasing = 0U;
    unsigned samples = 0U;
    unsigned autosaveMinutes = 0U;
    unsigned viewMode = 0U;
    unsigned sortMode = 0U;
    int shadows = 0;
    int postProcess = 0;
    int bloom = 0;
    int outline = 0;
    int gpuDriven = 0;
    int autosave = 0;
    int grid = 0;
    int snap = 0;
    int recursive = 0;
    int folders = 0;
    int templates = 0;
    float gridSpacing = 0.0F;
    float snapStep = 0.0F;
    float rotationSnap = 0.0F;
    float thumbnailScale = 0.0F;

    const bool parsed =
        (stream >> key >> backend) && key == "render_backend" &&
        (stream >> key >> shadows) && key == "shadows" &&
        (stream >> key >> postProcess) && key == "post_process" &&
        (stream >> key >> antiAliasing) && key == "anti_aliasing" &&
        (stream >> key >> samples) && key == "msaa_samples" &&
        (stream >> key >> bloom) && key == "bloom" &&
        (stream >> key >> outline) && key == "selection_outline" &&
        (stream >> key >> gpuDriven) && key == "gpu_driven" &&
        (stream >> key >> autosave) && key == "autosave" &&
        (stream >> key >> autosaveMinutes) && key == "autosave_minutes" &&
        (stream >> key >> grid) && key == "grid" &&
        (stream >> key >> gridSpacing) && key == "grid_spacing" &&
        (stream >> key >> snap) && key == "snap" &&
        (stream >> key >> snapStep) && key == "snap_step" &&
        (stream >> key >> rotationSnap) && key == "rotation_snap" &&
        (stream >> key >> recursive) && key == "asset_recursive" &&
        (stream >> key >> viewMode) && key == "asset_view" &&
        (stream >> key >> sortMode) && key == "asset_sort" &&
        (stream >> key >> folders) && key == "asset_folders" &&
        (stream >> key >> templates) && key == "asset_templates" &&
        (stream >> key >> thumbnailScale) && key == "asset_thumbnail_scale";
    const bool validSamples =
        samples == 0U || samples == 2U || samples == 4U ||
        samples == 8U || samples == 16U;
    if (!parsed || !AtEnd(stream) ||
        backend > 2U || antiAliasing > 3U || viewMode > 1U || sortMode > 2U ||
        !validSamples || (antiAliasing == 3U && samples == 0U) ||
        !BoolValue(shadows) || !BoolValue(postProcess) || !BoolValue(bloom) ||
        !BoolValue(outline) || !BoolValue(gpuDriven) || !BoolValue(autosave) ||
        !BoolValue(grid) || !BoolValue(snap) || !BoolValue(recursive) ||
        !BoolValue(folders) || !BoolValue(templates) ||
        !std::isfinite(gridSpacing) || gridSpacing < 0.001F || gridSpacing > 1000.0F ||
        !std::isfinite(snapStep) || snapStep < 0.001F || snapStep > 1000.0F ||
        !std::isfinite(rotationSnap) || rotationSnap < 0.0F || rotationSnap > 180.0F ||
        !std::isfinite(thumbnailScale) || thumbnailScale < 0.65F || thumbnailScale > 1.75F) {
        return false;
    }

    value.saving.autosaveEnabled = autosave != 0;
    value.saving.autosaveIntervalMinutes = autosaveMinutes;
    return Valid(value);
}

} // namespace

EditorSettingsLoadResult EditorSettingsStore::Load(const std::filesystem::path& path) {
    EditorSettingsLoadResult result;
    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError)) {
        if (filesystemError) result.error = "Editor settings file could not be inspected.";
        return result;
    }

    const std::uintmax_t byteCount = std::filesystem::file_size(path, filesystemError);
    if (filesystemError || byteCount == 0U || byteCount > MaximumBytes) {
        result.error = "Editor settings file has an invalid size.";
        return result;
    }

    std::ifstream input{path, std::ios::binary};
    std::string source(static_cast<std::size_t>(byteCount), '\0');
    input.read(source.data(), static_cast<std::streamsize>(source.size()));
    if (!input) {
        result.error = "Editor settings file could not be read.";
        return result;
    }

    std::istringstream stream{source};
    stream.imbue(std::locale::classic());
    std::string header;
    std::getline(stream, header);

    EditorSettingsDocument value;
    const bool parsed =
        header == kCurrentHeader ? ParseCurrent(stream, value) :
        header == kLegacyHeader ? ParseLegacy(stream, value) :
        false;
    if (!parsed) {
        result.error = "Editor settings file is malformed or uses an unsupported version.";
        return result;
    }

    result.settings = value;
    result.found = true;
    return result;
}

bool EditorSettingsStore::Save(
    const std::filesystem::path& path,
    const EditorSettingsDocument& settings,
    std::string& error) {
    error.clear();
    if (!Valid(settings)) {
        error = "Editor settings contain unsupported values.";
        return false;
    }

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << kCurrentHeader << '\n'
           << "autosave " << settings.saving.autosaveEnabled << '\n'
           << "autosave_minutes " << settings.saving.autosaveIntervalMinutes << '\n';
    const std::string bytes = stream.str();
    if (bytes.empty() || bytes.size() > MaximumBytes) {
        error = "Editor settings exceed their bounded size.";
        return false;
    }

    std::error_code filesystemError;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), filesystemError);
    }
    if (filesystemError) {
        error = "Editor settings directory could not be created.";
        return false;
    }

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, filesystemError);
            error = "Editor settings temporary file could not be written.";
            return false;
        }
    }

    if (!Replace(temporary, path)) {
        std::filesystem::remove(temporary, filesystemError);
        error = "Editor settings atomic replacement failed.";
        return false;
    }
    return true;
}

} // namespace kb::editor
