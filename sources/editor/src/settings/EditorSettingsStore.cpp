#include "settings/EditorSettingsStore.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {
namespace {

constexpr std::string_view kHeader = "21kb Editor Settings 1";

[[nodiscard]] bool Replace(const std::filesystem::path& source, const std::filesystem::path& target) noexcept {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(source, target, error);
    return !error;
#endif
}

[[nodiscard]] bool BoolValue(int value) noexcept { return value == 0 || value == 1; }

[[nodiscard]] bool Valid(const EditorSettingsDocument& value) noexcept {
    const auto backend = static_cast<unsigned>(value.renderer.renderBackend);
    const auto aa = static_cast<unsigned>(value.renderer.antiAliasingMode);
    const auto view = static_cast<unsigned>(value.workspace.assetViewMode);
    const auto sort = static_cast<unsigned>(value.workspace.assetSortMode);
    const bool validSamples = value.renderer.msaaSamples == 0U || value.renderer.msaaSamples == 2U ||
        value.renderer.msaaSamples == 4U || value.renderer.msaaSamples == 8U || value.renderer.msaaSamples == 16U;
    return backend <= static_cast<unsigned>(EditorRenderBackend::Vulkan) &&
        aa <= static_cast<unsigned>(EditorAntiAliasingMode::Msaa) &&
        view <= static_cast<unsigned>(EditorAssetViewMode::Tiles) &&
        sort <= static_cast<unsigned>(EditorAssetSortMode::Path) && validSamples &&
        (value.renderer.antiAliasingMode != EditorAntiAliasingMode::Msaa || value.renderer.msaaSamples != 0U) &&
        value.workspace.autosaveIntervalMinutes >= 1U && value.workspace.autosaveIntervalMinutes <= 120U &&
        std::isfinite(value.workspace.gridSpacing) && value.workspace.gridSpacing >= 0.001F && value.workspace.gridSpacing <= 1000.0F &&
        std::isfinite(value.workspace.snapStep) && value.workspace.snapStep >= 0.001F && value.workspace.snapStep <= 1000.0F &&
        std::isfinite(value.workspace.rotationSnapDegrees) && value.workspace.rotationSnapDegrees >= 0.0F && value.workspace.rotationSnapDegrees <= 180.0F &&
        std::isfinite(value.workspace.assetThumbnailScale) && value.workspace.assetThumbnailScale >= 0.65F && value.workspace.assetThumbnailScale <= 1.75F;
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
    std::string key;
    unsigned backend = 0U;
    unsigned aa = 0U;
    unsigned samples = 0U;
    unsigned autosaveMinutes = 0U;
    unsigned viewMode = 0U;
    unsigned sortMode = 0U;
    int shadows = 0, postProcess = 0, bloom = 0, outline = 0, gpuDriven = 0;
    int autosave = 0, grid = 0, snap = 0, recursive = 0, folders = 0, templates = 0;
    const bool parsed = header == kHeader &&
        (stream >> key >> backend) && key == "render_backend" &&
        (stream >> key >> shadows) && key == "shadows" &&
        (stream >> key >> postProcess) && key == "post_process" &&
        (stream >> key >> aa) && key == "anti_aliasing" &&
        (stream >> key >> samples) && key == "msaa_samples" &&
        (stream >> key >> bloom) && key == "bloom" &&
        (stream >> key >> outline) && key == "selection_outline" &&
        (stream >> key >> gpuDriven) && key == "gpu_driven" &&
        (stream >> key >> autosave) && key == "autosave" &&
        (stream >> key >> autosaveMinutes) && key == "autosave_minutes" &&
        (stream >> key >> grid) && key == "grid" &&
        (stream >> key >> value.workspace.gridSpacing) && key == "grid_spacing" &&
        (stream >> key >> snap) && key == "snap" &&
        (stream >> key >> value.workspace.snapStep) && key == "snap_step" &&
        (stream >> key >> value.workspace.rotationSnapDegrees) && key == "rotation_snap" &&
        (stream >> key >> recursive) && key == "asset_recursive" &&
        (stream >> key >> viewMode) && key == "asset_view" &&
        (stream >> key >> sortMode) && key == "asset_sort" &&
        (stream >> key >> folders) && key == "asset_folders" &&
        (stream >> key >> templates) && key == "asset_templates" &&
        (stream >> key >> value.workspace.assetThumbnailScale) && key == "asset_thumbnail_scale";
    stream >> std::ws;
    if (!parsed || !stream.eof() || !BoolValue(shadows) || !BoolValue(postProcess) ||
        !BoolValue(bloom) || !BoolValue(outline) || !BoolValue(gpuDriven) || !BoolValue(autosave) ||
        !BoolValue(grid) || !BoolValue(snap) || !BoolValue(recursive) || !BoolValue(folders) || !BoolValue(templates)) {
        result.error = "Editor settings file is malformed.";
        return result;
    }
    value.renderer.renderBackend = static_cast<EditorRenderBackend>(backend);
    value.renderer.shadowsEnabled = shadows != 0;
    value.renderer.postProcessEnabled = postProcess != 0;
    value.renderer.antiAliasingMode = static_cast<EditorAntiAliasingMode>(aa);
    value.renderer.msaaSamples = static_cast<std::uint8_t>(samples);
    value.renderer.bloomEnabled = bloom != 0;
    value.renderer.selectionOutlineEnabled = outline != 0;
    value.renderer.gpuDrivenEnabled = gpuDriven != 0;
    value.workspace.autosaveEnabled = autosave != 0;
    value.workspace.autosaveIntervalMinutes = autosaveMinutes;
    value.workspace.gridVisible = grid != 0;
    value.workspace.snapEnabled = snap != 0;
    value.workspace.assetBrowserRecursive = recursive != 0;
    value.workspace.assetViewMode = static_cast<EditorAssetViewMode>(viewMode);
    value.workspace.assetSortMode = static_cast<EditorAssetSortMode>(sortMode);
    value.workspace.assetShowFolders = folders != 0;
    value.workspace.assetShowTemplates = templates != 0;
    if (!Valid(value)) {
        result.error = "Editor settings file contains unsupported values.";
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
    stream << std::setprecision(9) << kHeader << '\n'
           << "render_backend " << static_cast<unsigned>(settings.renderer.renderBackend) << '\n'
           << "shadows " << settings.renderer.shadowsEnabled << '\n'
           << "post_process " << settings.renderer.postProcessEnabled << '\n'
           << "anti_aliasing " << static_cast<unsigned>(settings.renderer.antiAliasingMode) << '\n'
           << "msaa_samples " << static_cast<unsigned>(settings.renderer.msaaSamples) << '\n'
           << "bloom " << settings.renderer.bloomEnabled << '\n'
           << "selection_outline " << settings.renderer.selectionOutlineEnabled << '\n'
           << "gpu_driven " << settings.renderer.gpuDrivenEnabled << '\n'
           << "autosave " << settings.workspace.autosaveEnabled << '\n'
           << "autosave_minutes " << settings.workspace.autosaveIntervalMinutes << '\n'
           << "grid " << settings.workspace.gridVisible << '\n'
           << "grid_spacing " << settings.workspace.gridSpacing << '\n'
           << "snap " << settings.workspace.snapEnabled << '\n'
           << "snap_step " << settings.workspace.snapStep << '\n'
           << "rotation_snap " << settings.workspace.rotationSnapDegrees << '\n'
           << "asset_recursive " << settings.workspace.assetBrowserRecursive << '\n'
           << "asset_view " << static_cast<unsigned>(settings.workspace.assetViewMode) << '\n'
           << "asset_sort " << static_cast<unsigned>(settings.workspace.assetSortMode) << '\n'
           << "asset_folders " << settings.workspace.assetShowFolders << '\n'
           << "asset_templates " << settings.workspace.assetShowTemplates << '\n'
           << "asset_thumbnail_scale " << settings.workspace.assetThumbnailScale << '\n';
    const std::string bytes = stream.str();
    if (bytes.empty() || bytes.size() > MaximumBytes) {
        error = "Editor settings exceed their bounded size.";
        return false;
    }
    std::error_code filesystemError;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), filesystemError);
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
