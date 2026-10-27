#include "engine/scene/SceneAssetCopy.hpp"

#include "engine/scene/SceneDocumentService.hpp"
#include "engine/security/Crypto.hpp"
#include "scene/prefab/io/ScenePrefabAssetReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetWriter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <system_error>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

bool FreshGuid(std::string& guid) {
    std::array<std::uint8_t, 16U> bytes{};
    if (!kb::security::SecureRandom(bytes)) return false;
    guid = "copy-" + kb::security::ToHex(bytes);
    return true;
}

void Remap(std::string& guid, const std::map<std::string, std::string>& identities) {
    if (const auto found = identities.find(guid); found != identities.end()) guid = found->second;
}

void Remap(ScenePrefab& prefab, const std::map<std::string, std::string>& identities) {
    for (std::uint32_t index = 0U; index < prefab.NodeCount(); ++index) {
        Remap(prefab.TryGetMutableNode(index)->nestedPrefabGuid, identities);
    }
}

} // namespace

bool ReidentifyCopiedSceneAssets(const std::filesystem::path& copy, std::string& error) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (std::filesystem::is_directory(copy, ec)) {
        std::filesystem::recursive_directory_iterator iterator{ copy, ec };
        const std::filesystem::recursive_directory_iterator end;
        for (; !ec && iterator != end; iterator.increment(ec)) {
            const auto status = iterator->symlink_status(ec);
            if (ec) break;
            if (std::filesystem::is_symlink(status)) {
                error = "Copied scene assets must not contain symbolic links";
                return false;
            }
            if (std::filesystem::is_regular_file(status)) files.push_back(iterator->path());
        }
    } else if (!ec && std::filesystem::is_regular_file(copy, ec)) {
        files.push_back(copy);
    } else if (!ec) {
        error = "Copied scene asset does not exist";
        return false;
    }
    if (ec) {
        error = "Could not inspect copied scene assets: " + ec.message();
        return false;
    }
    std::ranges::sort(files);
    std::map<std::string, std::string> identities;
    std::vector<std::pair<std::filesystem::path, ScenePrefabAssetReadResult>> prefabs;
    std::vector<std::pair<std::filesystem::path, SceneDocument>> scenes;
    // Parse the whole copy before modifying anything. An ambiguous source GUID
    // cannot be repaired by guessing which copied prefab a reference meant.
    for (const auto& file : files) {
        std::string extension = file.extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".kbprefab") {
            ScenePrefabAssetReadResult prefab;
            if (!ScenePrefabAssetReader::Read(file, prefab)) {
                error = "Could not read copied prefab: " + file.filename().string();
                return false;
            }
            std::string guid;
            if (!FreshGuid(guid)) { error = "Could not generate a copied prefab identity"; return false; }
            if (!prefab.guid.empty() && !identities.emplace(prefab.guid, guid).second) {
                error = "Copied prefabs declare the same identity: " + prefab.guid;
                return false;
            }
            prefab.guid = std::move(guid);
            prefabs.emplace_back(file, std::move(prefab));
        } else if (extension == ".21kbscene") {
            auto loaded = SceneDocumentService::Load(file);
            if (!loaded.succeeded) { error = "Could not read copied scene: " + loaded.error; return false; }
            if (!FreshGuid(loaded.document.guid)) { error = "Could not generate a copied scene identity"; return false; }
            scenes.emplace_back(file, std::move(loaded.document));
        }
    }
    for (auto& [file, prefab] : prefabs) {
        Remap(prefab.baseGuid, identities);
        Remap(prefab.prefab, identities);
        for (auto& child : prefab.addedChildren) Remap(child.subtree, identities);
        if (!ScenePrefabAssetWriter::Write(file, {
            .kind = prefab.kind, .guid = prefab.guid, .name = prefab.name, .baseGuid = prefab.baseGuid,
            .prefab = &prefab.prefab, .overrides = &prefab.overrides, .addedChildren = &prefab.addedChildren })) {
            error = "Could not write copied prefab: " + file.filename().string();
            return false;
        }
    }
    for (auto& [file, document] : scenes) {
        Remap(document.worldPrefab, identities);
        if (!SceneDocumentService::Save(document, file)) {
            error = "Could not write copied scene: " + file.filename().string();
            return false;
        }
    }
    error.clear();
    return true;
}

} // namespace kb::scene
