#include "CliCommands.hpp"

#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/AssetPackSeal.hpp"
#include "engine/assets/bake/AssetPackSet.hpp"
#include "engine/assets/bake/AssetPackTools.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/security/Crypto.hpp"
#include "engine/security/ReleaseKeys.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace kb::cli {
namespace {

namespace bake = kb::assets::bake;

[[nodiscard]] int Fail(CommandIo io, const std::string& message) {
    io.err << "error: " << message << '\n';
    return 1;
}

[[nodiscard]] bool ParseLevel(const std::optional<std::string>& text, int fallback, int& level, std::string& error) {
    if (!text.has_value()) {
        level = fallback;
        return true;
    }
    const auto [end, parseError] = std::from_chars(text->data(), text->data() + text->size(), level);
    if (parseError != std::errc{} || end != text->data() + text->size() || level < 0 || level > 19) {
        error = "--level expects 0 (no compression) to 19";
        return false;
    }
    return true;
}

void PrintReport(CommandIo io, const std::filesystem::path& path, const bake::AssetPackToolReport& report) {
    io.out << path.string() << ": " << report.artifacts << " artifacts, " << report.blocks << " blocks ("
           << report.compressedBlocks << " compressed), " << report.payloadBytes << " payload bytes stored in "
           << report.storedBytes << " bytes, file " << report.fileBytes << " bytes\n";
}

[[nodiscard]] bool LoadTrust(const ArgumentList& arguments, bake::AssetPackTrust& trust, std::string& error) {
    trust = {};
    if (const std::optional<std::string> anchorPath = arguments.Option("--anchor"); anchorPath.has_value()) {
        std::ifstream input{ *anchorPath, std::ios::binary };
        const std::string bytes{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        kb::security::TrustAnchor anchor{};
        if (bytes.empty() ||
            !kb::security::DecodeTrustAnchor(
                std::span{ reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() }, anchor, error)) {
            if (error.empty()) {
                error = "could not read " + *anchorPath;
            }
            return false;
        }
        trust.requiredSigner = anchor.releaseKey;
        trust.contentKey = std::move(anchor.packContentKey);
        return true;
    }
    if (const std::optional<std::string> text = arguments.Option("--public-key"); text.has_value()) {
        trust.requiredSigner.emplace();
        if (!kb::security::TryParseHex(*text, *trust.requiredSigner)) {
            error = "--public-key expects 64 hexadecimal digits";
            return false;
        }
    }
    return true;
}

} // namespace

int RunPackInfoCommand(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() != 2U) {
        return Fail(io, "pack info expects <pack.kbpack>");
    }
    const std::filesystem::path path{ arguments.Positionals()[1] };
    bake::AssetPackReader reader;
    if (const bake::AssetPackReadStatus status = reader.Mount(path); status != bake::AssetPackReadStatus::Success) {
        return Fail(io, "pack refused: " + std::string{ bake::ToString(status) });
    }
    const bake::AssetPackHeader& header = reader.Header();
    const bake::AssetPackToolReport report = bake::DescribeAssetPack(reader);
    io.out << "format " << header.formatVersion << '\n'
           << "profile " << header.targetProfileId << '\n'
           << "role " << bake::ToString(header.role) << '\n'
           << "label " << header.label << '\n'
           << "patch-level " << header.patchLevel << '\n'
           << "identity " << reader.CatalogIdentity().ToString() << '\n'
           << "base " << ((header.baseIdentity.high == 0U && header.baseIdentity.low == 0U) ? std::string{ "-" }
                                                                                        : header.baseIdentity.ToString())
           << '\n'
           << "artifacts " << report.artifacts << '\n'
           << "blocks " << report.blocks << '\n'
           << "compressed-blocks " << report.compressedBlocks << '\n'
           << "payload-bytes " << report.payloadBytes << '\n'
           << "stored-bytes " << report.storedBytes << '\n'
           << "sealed " << (reader.Seal() != nullptr ? "yes" : "no") << '\n';
    return 0;
}

int RunPackCompressCommand(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() != 3U) {
        return Fail(io, "pack compress expects [--level <1-19>] <input.kbpack> <output.kbpack>");
    }
    std::string error;
    int level = 0;
    if (!ParseLevel(arguments.Option("--level"), 9, level, error)) {
        return Fail(io, error);
    }
    const std::filesystem::path input{ arguments.Positionals()[1] };
    const std::filesystem::path output{ arguments.Positionals()[2] };
    bake::AssetPackReader source;
    if (const bake::AssetPackReadStatus status = source.Mount(input); status != bake::AssetPackReadStatus::Success) {
        return Fail(io, "pack refused: " + std::string{ bake::ToString(status) });
    }
    bake::AssetPackWriterOptions options{};
    options.compression = level == 0 ? bake::AssetPackBlockCompression::None : bake::AssetPackBlockCompression::Zstd;
    options.compressionLevel = level == 0 ? 9 : level;
    options.role = source.Header().role;
    options.label = source.Header().label;
    options.patchLevel = source.Header().patchLevel;
    options.baseIdentity = source.Header().baseIdentity;
    source.Unmount();
    bake::AssetPackToolReport report{};
    if (!bake::RepackAssetPack(input, output, options, report, error)) {
        return Fail(io, error);
    }
    PrintReport(io, output, report);
    return 0;
}

int RunPackSplitCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> base = arguments.Option("--base");
    const std::vector<std::string> chunkSpecs = arguments.Options("--chunk");
    if (arguments.Positionals().size() != 2U || !base.has_value() || chunkSpecs.empty()) {
        return Fail(io, "pack split expects --base <base.kbpack> --chunk <label>=<prefix>[,<prefix>...] [--chunk ...] "
                        "[--level <0-19>] [--index <Game.kbpackset>] <cooked.kbpack>");
    }
    std::string error;
    int level = 0;
    if (!ParseLevel(arguments.Option("--level"), 9, level, error)) {
        return Fail(io, error);
    }
    const std::filesystem::path basePath{ *base };
    std::vector<bake::AssetPackChunkRule> rules;
    for (const std::string& spec : chunkSpecs) {
        const std::size_t equals = spec.find('=');
        if (equals == std::string::npos || equals == 0U || equals + 1U >= spec.size()) {
            return Fail(io, "--chunk expects <label>=<prefix>[,<prefix>...]: " + spec);
        }
        bake::AssetPackChunkRule rule{};
        rule.label = spec.substr(0U, equals);
        for (std::size_t start = equals + 1U; start <= spec.size();) {
            const std::size_t comma = std::min(spec.find(',', start), spec.size());
            if (comma > start) {
                rule.virtualPathPrefixes.push_back(spec.substr(start, comma - start));
            }
            start = comma + 1U;
        }
        rule.output = basePath.parent_path() / (basePath.stem().string() + "." + rule.label + ".kbpack");
        rules.push_back(std::move(rule));
    }
    bake::AssetPackSplitReport report{};
    if (!bake::SplitRuntimeAssetPack(std::filesystem::path{ arguments.Positionals()[1] }, basePath, rules,
            level == 0 ? bake::AssetPackBlockCompression::None : bake::AssetPackBlockCompression::Zstd,
            level == 0 ? 9 : level, report, error)) {
        return Fail(io, error);
    }
    PrintReport(io, basePath, report.base);
    bake::AssetPackSetIndex index{};
    index.packs.push_back({ .role = bake::AssetPackRole::Base, .path = basePath.filename().generic_string() });
    for (std::size_t chunk = 0U; chunk < rules.size(); ++chunk) {
        PrintReport(io, rules[chunk].output, report.chunks[chunk]);
        index.packs.push_back({ .role = bake::AssetPackRole::Chunk, .label = rules[chunk].label,
            .path = rules[chunk].output.filename().generic_string() });
    }
    if (const std::optional<std::string> indexPath = arguments.Option("--index"); indexPath.has_value()) {
        const std::string text = bake::EncodeAssetPackSetIndex(index);
        std::ofstream output{ *indexPath, std::ios::binary | std::ios::trunc };
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (text.empty() || !output.good()) {
            return Fail(io, "the pack set index could not be written: " + *indexPath);
        }
        io.out << "wrote " << *indexPath << '\n';
    }
    return 0;
}

int RunPackPatchCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> current = arguments.Option("--current");
    const std::optional<std::string> output = arguments.Option("--output");
    const std::optional<std::string> levelText = arguments.Option("--patch-level");
    if (arguments.Positionals().size() != 2U || !current.has_value() || !output.has_value() || !levelText.has_value()) {
        return Fail(io, "pack patch expects --current <Game.kbpackset | Game.kbpack> --patch-level <n> "
                        "--output <patch.kbpack> [--label <label>] [--level <0-19>] <new-cook.kbpack>");
    }
    std::string error;
    bake::AssetPackPatchRequest request{};
    const auto [end, parseError] = std::from_chars(levelText->data(), levelText->data() + levelText->size(), request.patchLevel);
    if (parseError != std::errc{} || end != levelText->data() + levelText->size() || request.patchLevel == 0U) {
        return Fail(io, "--patch-level expects a whole number of at least 1");
    }
    int level = 0;
    if (!ParseLevel(arguments.Option("--level"), 9, level, error)) {
        return Fail(io, error);
    }
    request.current = *current;
    request.next = arguments.Positionals()[1];
    request.output = *output;
    request.label = arguments.Option("--label").value_or("patch-" + std::to_string(request.patchLevel));
    request.compression = level == 0 ? bake::AssetPackBlockCompression::None : bake::AssetPackBlockCompression::Zstd;
    request.compressionLevel = level == 0 ? 9 : level;
    bake::AssetPackPatchReport report{};
    if (!bake::BuildAssetPackPatch(request, report, error)) {
        return Fail(io, error);
    }
    io.out << "patch " << request.patchLevel << " (" << request.label << "): " << report.changedAssets << " changed, "
           << report.addedAssets << " added, " << report.changedFiles << " files"
           << (report.settingsChanged ? ", project settings" : "") << '\n';
    if (report.removedAssets != 0U) {
        io.out << "note: " << report.removedAssets
               << " assets of the current content are gone from the new cook; a patch cannot remove them\n";
    }
    PrintReport(io, request.output, report.pack);
    return 0;
}

int RunPackSetVerifyCommand(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().size() != 2U) {
        return Fail(io, "pack set-verify expects [--anchor <file> | --public-key <hex>] <Game.kbpackset>");
    }
    bake::AssetPackTrust trust{};
    std::string error;
    if (!LoadTrust(arguments, trust, error)) {
        return Fail(io, error);
    }
    const std::filesystem::path indexPath{ arguments.Positionals()[1] };
    bake::AssetPackSetIndex index{};
    if (const bake::AssetPackSetStatus status = bake::ReadAssetPackSetIndex(indexPath, index);
        status != bake::AssetPackSetStatus::Success) {
        return Fail(io, "pack set index refused: " + std::string{ bake::ToString(status) });
    }
    bake::AssetPackReader base;
    if (base.Mount(bake::ResolveAssetPackSetPath(indexPath, index.packs.front().path)) != bake::AssetPackReadStatus::Success) {
        return Fail(io, "the base pack does not mount");
    }
    bake::BakeTargetProfile profile{};
    if (!bake::TryFindBakeTargetProfile(base.Header().targetProfileId, profile)) {
        return Fail(io, "the base pack names an unknown target profile");
    }
    base.Unmount();
    bake::RuntimeAssetPack set;
    if (const bake::RuntimeAssetPackStatus status = set.MountSetIndex(indexPath, profile, bake::AssetPackAccess::Ranged, trust);
        status != bake::RuntimeAssetPackStatus::Success) {
        return Fail(io, "pack set refused: " + std::string{ bake::ToString(status) } + " (" +
                std::string{ bake::ToString(set.ContainerStatus()) } + ", pack " + std::to_string(set.RefusedContainer()) + ")");
    }
    std::size_t blocks = 0U;
    std::vector<std::uint8_t> bytes;
    for (std::uint32_t container = 0U; container < set.ContainerCount(); ++container) {
        if (trust.requiredSigner.has_value() && set.ContainerSeal(container) == nullptr) {
            return Fail(io, "pack " + set.ContainerPath(container).string() + " is not signed");
        }
        for (const bake::AssetPackArtifactEntry& artifact : set.ContainerArtifacts(container)) {
            for (const bake::AssetPackBlockEntry& block : artifact.blocks) {
                if (const bake::AssetPackReadStatus status = set.ReadContainerBlock(container, artifact, block.name, bytes);
                    status != bake::AssetPackReadStatus::Success) {
                    return Fail(io, set.ContainerPath(container).string() + ": block " + artifact.key.ToString() + "/" +
                            block.name + " refused: " + std::string{ bake::ToString(status) });
                }
                ++blocks;
            }
        }
    }
    io.out << "OK " << indexPath.string() << ": " << set.ContainerCount() << " packs, " << set.Manifest().assets.size()
           << " assets, " << blocks << " blocks"
           << (trust.requiredSigner.has_value() ? "" : " (signers not checked against an expected key)") << '\n';
    return 0;
}

} // namespace kb::cli
