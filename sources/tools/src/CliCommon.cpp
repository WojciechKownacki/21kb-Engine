#include "CliCommon.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <algorithm>
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

const kb::assets::AssetMetadata* FindAssetByFlexiblePath(
    const kb::assets::AssetRegistry& registry,
    const std::filesystem::path& input) {
    const std::string text = input.generic_string();
    if (!text.empty() && text.front() == '/') {
        return registry.FindByPath(input);
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

} // namespace kb::cli
