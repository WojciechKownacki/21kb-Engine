#include "CliCommon.hpp"

#include "engine/assets/AssetImportPreset.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/math/ColorSpace.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#if KB_CLI_RENDERER
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#if KB_CLI_RENDERER
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#endif
#endif

#include <cmath>
#include <charconv>
#include <algorithm>
#include <memory>
#include <system_error>

namespace kb::cli {

ArgumentList::ArgumentList(std::span<const std::string> arguments, std::span<const std::string_view> flagNames) {
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        const std::string& token = arguments[index];
        if (token.rfind("--", 0U) != 0U) {
            positionals_.push_back(token);
            continue;
        }
        const bool isFlag = std::any_of(flagNames.begin(), flagNames.end(), [&token](std::string_view flag) {
            return token == flag;
        });
        if (isFlag) {
            flags_.push_back(token);
            continue;
        }
        if (index + 1U >= arguments.size()) {
            errors_.push_back("option " + token + " expects a value");
            continue;
        }
        options_.emplace_back(token, arguments[index + 1U]);
        ++index;
    }
}

std::optional<std::string> ArgumentList::Option(std::string_view name) const {
    for (const auto& [key, value] : options_) {
        if (key == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::vector<std::string> ArgumentList::Options(std::string_view name) const {
    std::vector<std::string> values;
    for (const auto& [key, value] : options_) {
        if (key == name) {
            values.push_back(value);
        }
    }
    return values;
}

bool ArgumentList::Flag(std::string_view name) const noexcept {
    return std::any_of(flags_.begin(), flags_.end(), [name](const std::string& flag) {
        return flag == name;
    });
}

const std::vector<std::string>& ArgumentList::Positionals() const noexcept {
    return positionals_;
}

const std::vector<std::string>& ArgumentList::Errors() const noexcept {
    return errors_;
}

std::vector<std::string> ArgumentList::UnknownOptions(std::span<const std::string_view> allowed) const {
    std::vector<std::string> unknown;
    const auto check = [&](const std::string& name) {
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) unknown.push_back(name);
    };
    for (const auto& option : options_) check(option.first);
    for (const auto& flag : flags_) check(flag);
    return unknown;
}

bool MountProjectAssets(
    kb::scene::Scene& scene,
    const std::filesystem::path& projectRoot,
    std::string& error,
    std::size_t* discoveredCount) {
    std::error_code errorCode;
    if (!std::filesystem::exists(projectRoot, errorCode) || errorCode) {
        error = "project root does not exist: " + projectRoot.string();
        return false;
    }
    if (!scene.Assets().MountProject(projectRoot)) {
        error = "could not mount project assets under: " + projectRoot.string();
        return false;
    }
#if KB_CLI_RENDERER
    // Tilemap asset IDs and collision sources must match the editor and packaged runtime.
    static_cast<void>(scene.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()));
    // Materials are typed by the renderer's loader, as in the editor and the game; without it
    // a .kbmat is an unknown file and MeshRenderer material= cannot reference it.
    static_cast<void>(scene.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));
    // Post-process profiles (.kbppfx) likewise, for scene-post-process.
    static_cast<void>(scene.Assets().Manager().RegisterLoader(std::make_unique<kb::render::PostProcessProfileAssetLoader>()));
    // Material instances (.kbmatinst) for material-instance-* and MeshRenderer material=, and
    // textures (images under Assets, imported textures) for the materials' texture slots.
    static_cast<void>(scene.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialInstanceAssetLoader>()));
    static_cast<void>(scene.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()));
#endif
    // Import presets (.kbimportpreset) for import --preset, as the editor's import dialog lists them.
    static_cast<void>(scene.Assets().Manager().RegisterLoader(std::make_unique<kb::assets::AssetImportPresetLoader>()));
    const std::size_t discovered = scene.Assets().Discover();
    if (discoveredCount != nullptr) {
        *discoveredCount = discovered;
    }
    return true;
}

std::filesystem::path ResolveInputPath(
    const std::filesystem::path& input,
    const std::filesystem::path& projectRoot) {
    if (input.is_absolute() || projectRoot.empty()) {
        return input;
    }
    std::error_code errorCode;
    const std::filesystem::path anchored = projectRoot / input;
    if (std::filesystem::exists(anchored, errorCode) && !errorCode) {
        return anchored;
    }
    return input;
}

std::filesystem::path ResolveProjectFile(
    const std::filesystem::path& input,
    const std::filesystem::path& projectRoot,
    std::string& error) {
    const std::filesystem::path resolved = ResolveInputPath(input, projectRoot);
    if (projectRoot.empty()) {
        return resolved;
    }
    std::error_code errorCode;
    const std::filesystem::path file = std::filesystem::weakly_canonical(std::filesystem::absolute(resolved, errorCode), errorCode);
    const std::filesystem::path root = std::filesystem::weakly_canonical(std::filesystem::absolute(projectRoot, errorCode), errorCode);
    const std::filesystem::path relative = file.lexically_relative(root);
    if (errorCode || relative.empty() || *relative.begin() == "..") {
        error = "the path " + input.generic_string() + " is outside the project " + projectRoot.generic_string() +
            ". Pass a path inside the project, e.g. Assets/Scenes/Main.21kbscene or /Game/Scenes/Main.21kbscene.";
        return {};
    }
    return resolved;
}

const kb::assets::AssetMetadata* FindAssetByFlexiblePath(
    const kb::assets::AssetRegistry& registry,
    const std::filesystem::path& input) {
    const std::string text = input.generic_string();
    if (!text.empty() && text.front() == '/') {
        if (const kb::assets::AssetMetadata* asset = registry.FindByPath(input)) {
            return asset;
        }
    }

    std::error_code errorCode;
    const std::filesystem::path canonicalInput = std::filesystem::weakly_canonical(input, errorCode);
    if (errorCode) {
        return nullptr;
    }
    for (const kb::assets::AssetMetadata& metadata : registry.All()) {
        const std::filesystem::path canonicalAsset = std::filesystem::weakly_canonical(metadata.physicalPath, errorCode);
        if (!errorCode && canonicalAsset == canonicalInput) {
            return &metadata;
        }
    }
    return nullptr;
}

std::optional<std::array<float, 4>> ParseColorText(std::string_view text, const std::size_t channels) {
    if (!text.empty() && text.front() == '#') {
        const std::optional<std::array<float, 4>> srgb = kb::math::ParseHexColor(text);
        if (!srgb.has_value() || (channels < 4U && text.size() != 7U)) return std::nullopt;
        return std::array<float, 4>{ kb::math::SrgbToLinear((*srgb)[0]), kb::math::SrgbToLinear((*srgb)[1]),
            kb::math::SrgbToLinear((*srgb)[2]), (*srgb)[3] };
    }
    std::array<float, 4> color{ 0.0F, 0.0F, 0.0F, 1.0F };
    std::size_t count = 0U;
    for (std::string_view rest = text; ; ++count) {
        const std::size_t comma = rest.find(',');
        const std::string_view part = rest.substr(0U, comma);
        float value = 0.0F;
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (count >= channels || error != std::errc{} || end != part.data() + part.size() ||
            !std::isfinite(value) || value < 0.0F || value > 1.0F) {
            return std::nullopt;
        }
        color[count] = value;
        if (comma == std::string_view::npos) break;
        rest = rest.substr(comma + 1U);
    }
    if (count + 1U != 3U && count + 1U != channels) return std::nullopt;
    return color;
}

} // namespace kb::cli
