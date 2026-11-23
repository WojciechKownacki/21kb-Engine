#include "CliCommands.hpp"
#include "CliWorkshop.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/project/ProjectManager.hpp"

#if KB_CLI_RENDERER
#include <bimg/bimg.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/readerwriter.h>
#endif

#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// image new / sound new: a generated picture or sound written into the project's Assets and imported, so the
// result is an asset at once. The output folder is always inside the project's Assets.
namespace kb::cli {
namespace {

[[nodiscard]] std::string Utf8Text(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string lower;
    for (const char c : text) lower += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    return lower;
}

struct Target {
    std::filesystem::path projectRoot;
    std::filesystem::path file;
    std::string name;
    std::string folderVirtual; // /Game/Images
    std::string virtualPath;   // /Game/Images/Heart.png
    std::string project;       // --project as given
};

// The file this command writes: <Assets>/<folder>/<name><extension>. The name is a plain name, the folder lies inside
// the project's Assets (links followed), and an existing file is kept unless --replace says so.
[[nodiscard]] std::optional<Target> ResolveTarget(const ArgumentList& arguments, std::string_view command, std::string_view defaultFolder,
    std::string_view extension, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    std::optional<std::string> name = arguments.Option("--name");
    if (!project.has_value() || !name.has_value()) {
        io.err << "error: " << command << " new requires --project <dir> --name <Name> --preset <preset>\n";
        return std::nullopt;
    }
    if (Lower(*name).ends_with(extension)) name->resize(name->size() - extension.size());
    const bool plain = !name->empty() && name->size() <= 64U && std::ranges::all_of(*name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
    if (!plain) {
        io.err << "error: --name is a plain name of letters, digits, - and _ (for example Heart), not a path, got '" << *name << "'\n";
        return std::nullopt;
    }
    // Names Windows keeps for devices (CON, NUL, COM1 ...) cannot be files there, with any extension.
    const std::string lowerName = Lower(*name);
    const bool device = lowerName == "con" || lowerName == "prn" || lowerName == "aux" || lowerName == "nul" ||
        ((lowerName.starts_with("com") || lowerName.starts_with("lpt")) && lowerName.size() == 4U && lowerName[3] >= '0' && lowerName[3] <= '9');
    if (device) {
        io.err << "error: --name '" << *name << "' is a reserved system name; pick another, for example " << *name << "Sound\n";
        return std::nullopt;
    }
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    Target target;
    target.projectRoot = projectFile.parent_path();
    target.project = *project;
    target.name = *name;
    const kb::assets::ProjectAssetFolder folder =
        kb::assets::ResolveProjectAssetFolder(target.projectRoot, arguments.Option("--folder").value_or(std::string{ defaultFolder }));
    if (!folder.error.empty()) {
        io.err << "error: " << folder.error << '\n';
        return std::nullopt;
    }
    std::error_code ec;
    const std::filesystem::path assetsRoot = std::filesystem::weakly_canonical(target.projectRoot / "Assets", ec);
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(folder.folder, ec);
    const std::filesystem::path relative = resolved.lexically_relative(assetsRoot);
    if (ec || relative.empty() || *relative.begin() == "..") {
        io.err << "error: refused for safety: the folder must lie inside the project's Assets\n";
        return std::nullopt;
    }
    target.folderVirtual = folder.virtualPath;
    target.file = folder.folder / kb::assets::Utf8Path(*name + std::string{ extension });
    target.virtualPath = folder.virtualPath + "/" + *name + std::string{ extension };
    if (std::filesystem::exists(target.file, ec) && !arguments.Flag("--replace")) {
        io.err << "error: " << target.virtualPath << " already exists; pick another --name, or add --replace to draw it again\n";
        return std::nullopt;
    }
    return target;
}

[[nodiscard]] bool WriteFile(const Target& target, const void* bytes, std::size_t size, CommandIo io) {
    std::error_code ec;
    std::filesystem::create_directories(target.file.parent_path(), ec);
    std::ofstream file(target.file, std::ios::binary | std::ios::trunc);
    file.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    file.close();
    if (!file) {
        io.err << "error: could not write " << target.virtualPath << '\n';
        return false;
    }
    return true;
}

// The same import the editor's Import... does, into the folder the file is in: a new asset, or the one it
// replaces brought up to date. Returns the imported asset's path.
[[nodiscard]] std::optional<std::string> ImportResult(const Target& target, bool replaced, CommandIo io) {
    const std::string asset = target.folderVirtual + "/" + target.name + ".21kb";
    std::vector<std::string> words{ "--project", target.project };
    if (replaced) {
        words.insert(words.end(), { "--reimport", asset, Utf8Text(target.file) });
    } else {
        words.insert(words.end(), { "--destination", target.folderVirtual, Utf8Text(target.file) });
    }
    std::ostringstream out;
    std::ostringstream err;
    const ArgumentList arguments{ words };
    // A first import of a name that was imported before (--replace after the .21kb was deleted) simply imports again.
    int exitCode = RunImportCommand(arguments, CommandIo{ .out = out, .err = err });
    if (exitCode != 0 && replaced) {
        const std::vector<std::string> fresh{ "--project", target.project, "--destination", target.folderVirtual, Utf8Text(target.file) };
        std::ostringstream out2;
        std::ostringstream err2;
        exitCode = RunImportCommand(ArgumentList{ fresh }, CommandIo{ .out = out2, .err = err2 });
        if (exitCode != 0) err << err2.str();
    }
    if (exitCode != 0) {
        io.err << "error: the file was written to " << target.virtualPath << " but could not be imported: " << err.str();
        return std::nullopt;
    }
    return asset;
}

[[nodiscard]] std::optional<int> IntOption(const ArgumentList& arguments, std::string_view name, CommandIo io, bool& bad) {
    const std::optional<std::string> text = arguments.Option(name);
    if (!text.has_value()) return std::nullopt;
    int value = 0;
    const auto [end, code] = std::from_chars(text->data(), text->data() + text->size(), value);
    if (code != std::errc{} || end != text->data() + text->size()) {
        io.err << "error: " << name << " takes a whole number, got '" << *text << "'\n";
        bad = true;
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<double> NumberOption(const ArgumentList& arguments, std::string_view name, CommandIo io, bool& bad) {
    const std::optional<std::string> text = arguments.Option(name);
    if (!text.has_value()) return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod(text->c_str(), &end);
    if (text->empty() || end != text->c_str() + text->size() || !std::isfinite(value)) {
        io.err << "error: " << name << " takes a number, got '" << *text << "'\n";
        bad = true;
        return std::nullopt;
    }
    return value;
}

} // namespace

#if KB_CLI_RENDERER
namespace {

// A PNG file (RGBA8) of the bitmap, through the encoder the engine's screenshots use.
[[nodiscard]] std::vector<std::uint8_t> EncodePng(const workshop::Bitmap& bitmap) {
    bx::DefaultAllocator allocator;
    bx::MemoryBlock block(&allocator);
    bx::MemoryWriter writer(&block);
    bx::Error error;
    bimg::imageWritePng(&writer, bitmap.width, bitmap.height, bitmap.width * 4U, bitmap.rgba.data(), bimg::TextureFormat::RGBA8, false, &error);
    // The block is allocated in larger steps than it is written: the file is what the writer wrote, not the block.
    const auto size = static_cast<std::size_t>(bx::seek(&writer, 0, bx::Whence::Current));
    if (!error.isOk() || size == 0U) return {};
    const auto* data = static_cast<const std::uint8_t*>(block.more(0U));
    return std::vector<std::uint8_t>(data, data + size);
}

} // namespace

int RunImageCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().size() == 1U ? arguments.Positionals().front() : std::string{};
    const std::optional<std::string> preset = arguments.Option("--preset");
    if (action != "new" || !preset.has_value()) {
        io.err << "usage: kb_cli image new --project <dir> --name <Name> --preset <preset> [--style flat|soft|neon|pixel]\n"
                  "       [--color #RRGGBB] [--color2 #RRGGBB] [--size 256] [--folder Assets/Images] [--replace]\n"
                  "presets: " << workshop::ImagePresetList() << '\n';
        return 1;
    }
    workshop::ImageRequest request;
    request.preset = Lower(*preset);
    request.style = Lower(arguments.Option("--style").value_or("flat"));
    for (const auto& [flag, slot] : { std::pair<std::string_view, std::optional<workshop::Rgb>*>{ "--color", &request.color },
             std::pair<std::string_view, std::optional<workshop::Rgb>*>{ "--color2", &request.color2 } }) {
        if (const std::optional<std::string> text = arguments.Option(flag)) {
            workshop::Rgb color;
            if (!workshop::ParseHexColor(*text, color)) {
                io.err << "error: " << flag << " takes a colour as #RRGGBB, got '" << *text << "'\n";
                return 1;
            }
            *slot = color;
        }
    }
    bool bad = false;
    if (const std::optional<int> size = IntOption(arguments, "--size", io, bad)) request.size = *size;
    if (bad) return 1;
    std::string error;
    const std::optional<workshop::Bitmap> bitmap = workshop::RenderImage(request, error);
    if (!bitmap.has_value()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const std::optional<Target> target = ResolveTarget(arguments, "image", "Assets/Images", ".png", io);
    if (!target.has_value()) return 1;
    const bool replaced = std::filesystem::exists(target->file);
    const std::vector<std::uint8_t> png = EncodePng(*bitmap);
    if (png.empty()) {
        io.err << "error: the picture could not be encoded as PNG\n";
        return 1;
    }
    if (!WriteFile(*target, png.data(), png.size(), io)) return 1;
    const std::optional<std::string> asset = ImportResult(*target, replaced, io);
    if (!asset.has_value()) return 1;
    io.out << "image " << target->virtualPath << " (" << bitmap->width << 'x' << bitmap->height << ", " << request.preset << ", " << request.style
           << "), imported as " << *asset << '\n'
           << "use it: component set --type Image --set imageAssetId=" << target->virtualPath << " on a UI Image (entity add --create Image), or --type Sprite2D"
           << (request.preset == "flame" ? " with cellWidth=" + std::to_string(request.size) + " frameCount=8" : std::string{}) << '\n';
    if (io.WantsData()) {
        kb::core::JsonValue data = DataObject("21kb.cli.image/v1");
        data.Set("asset", kb::core::JsonValue::MakeString(target->virtualPath));
        data.Set("imported", kb::core::JsonValue::MakeString(*asset));
        data.Set("width", kb::core::JsonValue::MakeNumber(static_cast<double>(bitmap->width)));
        data.Set("height", kb::core::JsonValue::MakeNumber(static_cast<double>(bitmap->height)));
        data.Set("preset", kb::core::JsonValue::MakeString(request.preset));
        data.Set("style", kb::core::JsonValue::MakeString(request.style));
        PublishData(io, std::move(data));
    }
    return 0;
}
#endif

int RunSoundCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().size() == 1U ? arguments.Positionals().front() : std::string{};
    const std::optional<std::string> preset = arguments.Option("--preset");
    if (action != "new" || !preset.has_value()) {
        io.err << "usage: kb_cli sound new --project <dir> --name <Name> --preset <preset> [--pitch <semitones>] [--length <seconds>]\n"
                  "       [--folder Assets/Audio] [--replace]\n"
                  "presets: " << workshop::SoundPresetList() << '\n';
        return 1;
    }
    workshop::SoundRequest request;
    request.preset = *preset;
    bool bad = false;
    if (const std::optional<double> pitch = NumberOption(arguments, "--pitch", io, bad)) request.pitchSemitones = *pitch;
    request.lengthSeconds = NumberOption(arguments, "--length", io, bad);
    if (bad) return 1;
    std::string error;
    const std::optional<workshop::Sound> sound = workshop::SynthSound(request, error);
    if (!sound.has_value()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const std::optional<Target> target = ResolveTarget(arguments, "sound", "Assets/Audio", ".wav", io);
    if (!target.has_value()) return 1;
    const bool replaced = std::filesystem::exists(target->file);
    const std::vector<std::uint8_t> wav = workshop::EncodeWav(*sound);
    if (!WriteFile(*target, wav.data(), wav.size(), io)) return 1;
    const std::optional<std::string> asset = ImportResult(*target, replaced, io);
    if (!asset.has_value()) return 1;
    const double seconds = static_cast<double>(sound->samples.size()) / static_cast<double>(sound->sampleRate);
    io.out << "sound " << target->virtualPath << " (" << FormatCliNumber(seconds) << " s, " << workshop::CanonicalSoundPreset(request.preset)
           << (sound->loops ? ", loops" : "") << "), imported as " << *asset << '\n'
           << "use it: component set --type AudioSource --set clipAssetId=" << target->virtualPath
           << (sound->loops ? " --set loop=true --set autoplay=true" : "") << " on an Audio Source, or in a script Audio.Play(\"" << target->virtualPath << "\")\n";
    if (io.WantsData()) {
        kb::core::JsonValue data = DataObject("21kb.cli.sound/v1");
        data.Set("asset", kb::core::JsonValue::MakeString(target->virtualPath));
        data.Set("imported", kb::core::JsonValue::MakeString(*asset));
        data.Set("seconds", kb::core::JsonValue::MakeNumber(seconds));
        data.Set("loops", kb::core::JsonValue::MakeBool(sound->loops));
        data.Set("preset", kb::core::JsonValue::MakeString(workshop::CanonicalSoundPreset(request.preset)));
        PublishData(io, std::move(data));
    }
    return 0;
}

} // namespace kb::cli
