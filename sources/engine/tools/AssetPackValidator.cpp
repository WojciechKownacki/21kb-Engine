#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"

#include <filesystem>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

int ValidateAssetPack(
    std::string_view expectedProfileId,
    const std::filesystem::path& packPath) {
    kb::assets::bake::BakeTargetProfile expectedProfile{};
    if (!kb::assets::bake::TryFindBakeTargetProfile(
            expectedProfileId, expectedProfile)) {
        std::cerr << "unknown target profile\n";
        return 2;
    }

    kb::assets::bake::AssetPackReader reader;
    const kb::assets::bake::AssetPackReadStatus mountStatus =
        reader.Mount(packPath, kb::assets::bake::AssetPackAccess::Ranged);
    if (mountStatus != kb::assets::bake::AssetPackReadStatus::Success) {
        std::cerr << "asset pack validation failed: "
                  << kb::assets::bake::ToString(mountStatus) << '\n';
        return 1;
    }
    if (!reader.MatchesTargetProfile(expectedProfile)) {
        std::cerr << "asset pack target profile mismatch\n";
        return 1;
    }
    std::vector<std::uint8_t> payload;
    for (const kb::assets::bake::AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        for (const kb::assets::bake::AssetPackBlockEntry& block : artifact.blocks) {
            const kb::assets::bake::AssetPackReadStatus status =
                reader.ReadBlock(artifact, block.name, payload);
            if (status != kb::assets::bake::AssetPackReadStatus::Success) {
                std::cerr << "asset pack payload validation failed: "
                          << kb::assets::bake::ToString(status) << '\n';
                return 1;
            }
        }
    }
    return 0;
}

#if defined(_WIN32)
[[nodiscard]] std::optional<std::string> NarrowAscii(std::wstring_view text) {
    std::string result;
    result.reserve(text.size());
    for (const wchar_t value : text) {
        if (value > 0x7F) {
            return std::nullopt;
        }
        result.push_back(static_cast<char>(value));
    }
    return result;
}
#endif

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::cerr << "usage: kb_asset_pack_validator <target-profile> <input.kbpack>\n";
        return 2;
    }
    const std::optional<std::string> profile = NarrowAscii(argv[1]);
    if (!profile.has_value()) {
        std::cerr << "target profile must be ASCII\n";
        return 2;
    }
    return ValidateAssetPack(*profile, std::filesystem::path{ argv[2] });
}
#else
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: kb_asset_pack_validator <target-profile> <input.kbpack>\n";
        return 2;
    }
    return ValidateAssetPack(argv[1], std::filesystem::path{ argv[2] });
}
#endif
