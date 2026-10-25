#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"
#include "engine/library/EngineLibraryParsing.hpp"
#include "engine/project/ProjectManager.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace kb::cli {
namespace {

[[nodiscard]] std::string Utf8Text(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::filesystem::path ProjectRoot(const ArgumentList& arguments, CommandIo io) {
    const auto project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: audio-mixer requires --project <dir>\n";
        return {};
    }
    std::string error;
    const auto file = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (file.empty()) io.err << "error: " << error << '\n';
    return file.empty() ? std::filesystem::path{} : file.parent_path();
}

struct MixerFile final {
    std::filesystem::path physical;
    std::string virtualPath;
};

[[nodiscard]] std::optional<MixerFile> LocateMixer(const ArgumentList& arguments,
    const std::filesystem::path& root, CommandIo io) {
    const auto mixer = arguments.Option("--mixer");
    if (!mixer.has_value()) {
        io.err << "error: audio-mixer requires --mixer /Game/<path>.kbmixer\n";
        return std::nullopt;
    }
    auto input = kb::assets::Utf8Path(*mixer);
    if (mixer->starts_with("/Game/")) input = std::filesystem::path{"Assets"} / kb::assets::Utf8Path(mixer->substr(6U));
    std::string error;
    const auto file = ResolveProjectFile(input, root, error);
    const auto relative = file.lexically_relative(root / "Assets");
    if (file.empty() || relative.empty() || relative.is_absolute() || *relative.begin() == ".."
        || file.extension() != kb::audio::kAudioMixerAssetExtension || !std::filesystem::is_regular_file(file)) {
        io.err << "error: the project has no audio mixer '" << *mixer << "'\n";
        return std::nullopt;
    }
    return MixerFile{file, "/Game/" + Utf8Text(relative)};
}

[[nodiscard]] std::optional<float> ParseGain(std::string_view value) {
    double gain = 0.0;
    if (!kb::library::TryParseDouble(value, gain) || !std::isfinite(gain) || gain < 0.0
        || gain > static_cast<double>(std::numeric_limits<float>::max())) return std::nullopt;
    return static_cast<float>(gain);
}

[[nodiscard]] std::string ApplySnapshotEdits(kb::audio::AudioMixerAsset& mixer, const ArgumentList& arguments) {
    for (const auto& name : arguments.Options("--add-snapshot")) {
        mixer.snapshots.push_back(kb::audio::AudioMixerSnapshot{.name = name});
    }
    for (const auto& name : arguments.Options("--remove-snapshot")) {
        if (std::erase_if(mixer.snapshots, [&](const auto& snapshot) { return snapshot.name == name; }) == 0U) {
            return "audio snapshot does not exist: " + name;
        }
    }
    const auto selected = arguments.Option("--snapshot");
    const auto settings = arguments.Options("--set-snapshot");
    const auto removals = arguments.Options("--clear-snapshot-bus");
    if (settings.empty() && removals.empty()) {
        return selected.has_value() ? "--snapshot requires --set-snapshot or --clear-snapshot-bus" : "";
    }
    if (!selected.has_value()) return "snapshot edits require --snapshot <Name>";
    const auto snapshot = std::find_if(mixer.snapshots.begin(), mixer.snapshots.end(),
        [&](const auto& item) { return item.name == *selected; });
    if (snapshot == mixer.snapshots.end()) return "audio snapshot does not exist: " + *selected;
    for (const auto& bus : removals) {
        if (std::erase_if(snapshot->busVolumes, [&](const auto& item) { return item.bus == bus; }) == 0U) {
            return "snapshot bus override does not exist: " + bus;
        }
    }
    for (const auto& setting : settings) {
        const auto equals = setting.rfind('=');
        if (equals == std::string::npos || equals == 0U) return "--set-snapshot requires Bus=volume: " + setting;
        const auto bus = std::string_view{setting}.substr(0U, equals);
        const auto gain = ParseGain(std::string_view{setting}.substr(equals + 1U));
        if (!gain.has_value()) return "snapshot volume must be finite and nonnegative: " + setting;
        const auto existing = std::find_if(snapshot->busVolumes.begin(), snapshot->busVolumes.end(),
            [&](const auto& item) { return item.bus == bus; });
        if (existing == snapshot->busVolumes.end()) {
            snapshot->busVolumes.push_back({.bus = std::string{bus}, .volume = *gain});
        } else {
            existing->volume = *gain;
        }
    }
    return "";
}

[[nodiscard]] std::string ApplyEdits(kb::audio::AudioMixerAsset& mixer, const ArgumentList& arguments) {
    for (const auto& name : arguments.Options("--bus")) {
        mixer.buses.push_back(kb::audio::AudioMixerBus{.name = name});
    }
    for (const auto& name : arguments.Options("--remove-bus")) {
        if (std::erase_if(mixer.buses, [&](const auto& bus) { return bus.name == name; }) == 0U) {
            return "audio bus does not exist: " + name;
        }
    }
    for (const auto& setting : arguments.Options("--set")) {
        const auto equals = setting.find('=');
        const auto dot = std::string_view{setting}.substr(0U, equals).rfind('.');
        if (equals == std::string::npos || dot == std::string_view::npos || dot == 0U) {
            return "--set requires Bus.parent|volume|mute=value: " + setting;
        }
        const auto name = std::string_view{setting}.substr(0U, dot);
        const auto field = std::string_view{setting}.substr(dot + 1U, equals - dot - 1U);
        const auto value = std::string_view{setting}.substr(equals + 1U);
        const auto bus = std::find_if(mixer.buses.begin(), mixer.buses.end(), [&](const auto& b) { return b.name == name; });
        if (bus == mixer.buses.end()) return "audio bus does not exist: " + std::string{name};
        if (field == "parent") {
            bus->parentBus = value == "-" ? std::string{} : std::string{value};
        } else if (field == "volume") {
            const auto gain = ParseGain(value);
            if (!gain.has_value()) {
                return "audio bus volume must be finite and nonnegative: " + setting;
            }
            bus->volume = *gain;
        } else if (field == "mute") {
            if (value != "true" && value != "false" && value != "0" && value != "1") {
                return "audio bus mute requires true, false, 0 or 1: " + setting;
            }
            bus->mute = value == "true" || value == "1";
        } else {
            return "unknown audio bus field (parent, volume, mute): " + setting;
        }
    }
    if (const auto error = ApplySnapshotEdits(mixer, arguments); !error.empty()) return error;
    return kb::audio::ValidateAudioMixerAsset(mixer);
}

void ShowMixer(const MixerFile& file, const kb::audio::AudioMixerAsset& mixer, CommandIo io) {
    io.out << file.virtualPath << " buses=" << mixer.buses.size() << " snapshots=" << mixer.snapshots.size() << '\n';
    for (const auto& bus : mixer.buses) {
        io.out << "bus " << bus.name << " parent=" << (bus.parentBus.empty() ? "-" : bus.parentBus)
            << " volume=" << bus.volume << " mute=" << (bus.mute ? "true" : "false") << '\n';
    }
    for (const auto& snapshot : mixer.snapshots) {
        io.out << "snapshot " << snapshot.name << '\n';
        for (const auto& bus : snapshot.busVolumes) io.out << "  bus " << bus.bus << " volume=" << bus.volume << '\n';
    }
}

} // namespace

int RunAudioMixerCommand(const ArgumentList& arguments, CommandIo io) {
    if (!arguments.Errors().empty()) {
        io.err << "error: " << arguments.Errors().front() << '\n';
        return 1;
    }
    if (arguments.Positionals().size() != 1U) {
        io.err << "error: audio-mixer requires new, set, show or assign\n";
        return 1;
    }
    const auto& action = arguments.Positionals().front();
    constexpr std::array<std::string_view, 8U> newOptions{"--project", "--name", "--folder", "--bus", "--set",
        "--add-snapshot", "--snapshot", "--set-snapshot"};
    constexpr std::array<std::string_view, 10U> setOptions{"--project", "--mixer", "--bus", "--remove-bus", "--set",
        "--add-snapshot", "--remove-snapshot", "--snapshot", "--set-snapshot", "--clear-snapshot-bus"};
    constexpr std::array<std::string_view, 2U> showOptions{"--project", "--mixer"};
    constexpr std::array<std::string_view, 4U> assignOptions{"--project", "--scene", "--mixer", "--snapshot"};
    std::span<const std::string_view> allowed;
    if (action == "new") allowed = newOptions;
    else if (action == "set") allowed = setOptions;
    else if (action == "show") allowed = showOptions;
    else if (action == "assign") allowed = assignOptions;
    else {
        io.err << "error: unknown audio-mixer action '" << action << "'\n";
        return 1;
    }
    const auto unknown = arguments.UnknownOptions(allowed);
    if (!unknown.empty()) {
        io.err << "error: unknown audio-mixer option '" << unknown.front() << "'\n";
        return 1;
    }
    if (action == "assign") return RunSceneAudioMixerCommand(arguments, io);
    const auto root = ProjectRoot(arguments, io);
    if (root.empty()) return 1;
    MixerFile file;
    kb::audio::AudioMixerAsset mixer;
    if (action == "new") {
        const auto name = arguments.Option("--name");
        if (!name.has_value()) {
            io.err << "error: audio-mixer new requires --name <Name>\n";
            return 1;
        }
        if (const auto error = kb::assets::AssetFileNameError(*name); !error.empty()) {
            io.err << "error: " << error << '\n';
            return 1;
        }
        const auto folder = kb::assets::ResolveProjectAssetFolder(root, arguments.Option("--folder").value_or("Assets/Audio"));
        if (!folder.error.empty()) {
            io.err << "error: " << folder.error << '\n';
            return 1;
        }
        const auto filename = *name + kb::audio::kAudioMixerAssetExtension;
        file = MixerFile{folder.folder / kb::assets::Utf8Path(filename), folder.virtualPath + "/" + filename};
        if (std::filesystem::exists(file.physical)) {
            io.err << "error: audio mixer already exists: " << file.virtualPath << '\n';
            return 1;
        }
    } else {
        const auto located = LocateMixer(arguments, root, io);
        if (!located.has_value()) return 1;
        file = *located;
        auto loaded = kb::audio::AudioMixerAssetIO::Load(file.physical);
        if (!loaded.has_value()) {
            io.err << "error: audio mixer is invalid: " << file.virtualPath << '\n';
            return 1;
        }
        mixer = std::move(*loaded);
    }
    if (action == "show") {
        ShowMixer(file, mixer, io);
        return 0;
    }
    if (action == "set") {
        constexpr std::array<std::string_view, 7U> edits{"--set", "--bus", "--remove-bus", "--add-snapshot",
            "--remove-snapshot", "--set-snapshot", "--clear-snapshot-bus"};
        if (std::none_of(edits.begin(), edits.end(), [&](auto option) { return !arguments.Options(option).empty(); })) {
            io.err << "error: audio-mixer set requires a bus or snapshot edit\n";
            return 1;
        }
    }
    if (const auto error = ApplyEdits(mixer, arguments); !error.empty()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    std::error_code error;
    std::filesystem::create_directories(file.physical.parent_path(), error);
    if (error || !kb::audio::AudioMixerAssetIO::Save(file.physical, mixer)) {
        io.err << "error: audio mixer could not be saved: " << file.virtualPath << '\n';
        return 1;
    }
    ShowMixer(file, mixer, io);
    return 0;
}

} // namespace kb::cli
