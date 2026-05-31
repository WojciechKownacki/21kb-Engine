#include "project/EditorProjectPaths.hpp"

#include <cctype>

namespace kb::editor {
namespace {

[[nodiscard]] std::string Sanitize(std::string name) {
    for (char& character : name) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '-' && character != '_') {
            character = '_';
        }
    }
    return name.empty() ? std::string{ "Prefab" } : name;
}

} // namespace

std::filesystem::path EditorProjectPaths::AssetsRoot() {
    return std::filesystem::current_path() / "Project" / "Assets";
}

std::filesystem::path EditorProjectPaths::PrefabsRoot() {
    return AssetsRoot() / "Prefabs";
}

std::filesystem::path EditorProjectPaths::UniquePrefabPath(std::string name) {
    std::filesystem::path root = PrefabsRoot();
    std::filesystem::create_directories(root);

    const std::string base = Sanitize(std::move(name));
    std::filesystem::path candidate = root / (base + ".kbprefab");
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = root / (base + "_" + std::to_string(suffix++) + ".kbprefab");
    }
    return candidate;
}

} // namespace kb::editor
