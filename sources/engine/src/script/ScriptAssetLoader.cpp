#include "engine/script/ScriptAssetLoader.hpp"

#include "engine/script/ScriptAsset.hpp"

#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>

namespace kb::script {
namespace {

[[nodiscard]] std::string ReadWholeFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        error = "Script asset file could not be opened";
        return {};
    }

    std::string content{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    if (!input.good() && !input.eof()) {
        error = "Script asset file could not be read";
        return {};
    }
    return content;
}

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool StartsWithComment(std::string_view line) noexcept {
    return line.starts_with('#') || line.starts_with("//");
}

[[nodiscard]] bool SplitKeyValue(std::string_view line, std::string_view& key, std::string_view& value) noexcept {
    const std::size_t equals = line.find('=');
    if (equals != std::string_view::npos) {
        key = Trim(line.substr(0, equals));
        value = Trim(line.substr(equals + 1));
        return !key.empty();
    }

    const std::size_t space = line.find_first_of(" \t");
    if (space == std::string_view::npos) {
        key = Trim(line);
        value = {};
        return !key.empty();
    }

    key = Trim(line.substr(0, space));
    value = Trim(line.substr(space + 1));
    return !key.empty();
}

[[nodiscard]] NativeBehaviourDescriptor ParseNativeBehaviourDescriptor(std::string_view source) {
    NativeBehaviourDescriptor descriptor;
    std::istringstream input{ std::string{ source } };
    std::string rawLine;
    while (std::getline(input, rawLine)) {
        const std::string_view line = Trim(rawLine);
        if (line.empty() || StartsWithComment(line)) {
            continue;
        }

        std::string_view key;
        std::string_view value;
        if (!SplitKeyValue(line, key, value)) {
            continue;
        }

        if (key == "name") {
            descriptor.name.assign(value);
        } else if (key == "symbol" || key == "type") {
            descriptor.symbol.assign(value);
        }
    }
    return descriptor;
}

} // namespace

std::string_view LuaScriptAssetLoader::Type() const noexcept {
    return ScriptAssetTypes::LuaScript;
}

std::type_index LuaScriptAssetLoader::PayloadType() const noexcept {
    return typeid(LuaScriptAsset);
}

std::vector<std::string> LuaScriptAssetLoader::Extensions() const {
    return { ".lua" };
}

kb::assets::AssetLoadResult LuaScriptAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string error;
    std::string source = ReadWholeFile(request.resolvedPath, error);
    if (!error.empty()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }

    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<LuaScriptAsset>(LuaScriptAsset{ .source = std::move(source) }),
        .error = {},
    };
}

std::string_view NativeBehaviourDescriptorAssetLoader::Type() const noexcept {
    return ScriptAssetTypes::NativeBehaviour;
}

std::type_index NativeBehaviourDescriptorAssetLoader::PayloadType() const noexcept {
    return typeid(NativeBehaviourDescriptor);
}

std::vector<std::string> NativeBehaviourDescriptorAssetLoader::Extensions() const {
    return { ".native", ".kbnative" };
}

kb::assets::AssetLoadResult NativeBehaviourDescriptorAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string error;
    std::string source = ReadWholeFile(request.resolvedPath, error);
    if (!error.empty()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }

    NativeBehaviourDescriptor descriptor = ParseNativeBehaviourDescriptor(source);
    if (descriptor.symbol.empty()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Native behaviour descriptor requires a non-empty symbol field" };
    }
    if (descriptor.name.empty()) {
        descriptor.name = request.metadata.name;
    }

    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<NativeBehaviourDescriptor>(std::move(descriptor)),
        .error = {},
    };
}

} // namespace kb::script
