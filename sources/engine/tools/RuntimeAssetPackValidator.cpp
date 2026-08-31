#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "PackagedRuntimeModuleContract.hpp"
#include "kb/render/bake/RuntimeAssetPackValidation.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

int ValidateRuntimePack(
    std::string_view expectedProfileId,
    const std::filesystem::path& packPath) {
    kb::assets::bake::BakeTargetProfile profile{};
    if (!kb::assets::bake::TryFindBakeTargetProfile(expectedProfileId, profile)) {
        std::cerr << "unknown target profile\n";
        return 2;
    }

    auto pack = std::make_shared<kb::assets::bake::RuntimeAssetPack>();
    const kb::assets::bake::RuntimeAssetPackStatus mount = pack->Mount(packPath, profile);
    if (mount != kb::assets::bake::RuntimeAssetPackStatus::Success) {
        std::cerr << "runtime asset pack validation failed: "
                  << kb::assets::bake::ToString(mount) << '\n';
        return 1;
    }
    if (const std::optional<std::string_view> unsupported =
            kb::game::FirstUnsupportedPackagedRuntimeModule(
                profile.identifier, pack->Manifest().descriptor);
        unsupported.has_value()) {
        std::cerr << "target runtime does not provide configured module: "
                  << *unsupported << '\n';
        return 1;
    }

    const kb::render::RuntimeAssetPackValidationResult validation =
        kb::render::ValidateRuntimeAssetPack(pack, profile);
    if (!validation.Succeeded()) {
        std::cerr << "runtime asset pack validation failed: "
                  << validation.error << '\n';
        return 1;
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
        std::cerr << "usage: kb_runtime_asset_pack_validator <target-profile> <input.kbpack>\n";
        return 2;
    }
    const std::optional<std::string> profile = NarrowAscii(argv[1]);
    if (!profile.has_value()) {
        std::cerr << "target profile must be ASCII\n";
        return 2;
    }
    return ValidateRuntimePack(*profile, std::filesystem::path{ argv[2] });
}
#else
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: kb_runtime_asset_pack_validator <target-profile> <input.kbpack>\n";
        return 2;
    }
    return ValidateRuntimePack(argv[1], std::filesystem::path{ argv[2] });
}
#endif
