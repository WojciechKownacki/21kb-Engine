#pragma once

#include "engine/scene/SceneDocument.hpp"

#include <filesystem>
#include <string>

namespace kb::scene {

class Scene;

struct SceneDocumentLoadResult {
    bool succeeded = false;
    SceneDocument document{};
    std::string error;
};

class SceneDocumentService {
public:
    SceneDocumentService() = delete;

    [[nodiscard]] static SceneDocument Capture(Scene& scene, std::string name);
    [[nodiscard]] static bool Save(Scene& scene, const std::filesystem::path& path, std::string name);
    [[nodiscard]] static bool Save(const SceneDocument& document, const std::filesystem::path& path);
    [[nodiscard]] static SceneDocumentLoadResult Load(const std::filesystem::path& path);
    [[nodiscard]] static bool LoadIntoScene(Scene& scene, const SceneDocument& document);
    [[nodiscard]] static bool LoadFileIntoScene(Scene& scene, const std::filesystem::path& path);
};

} // namespace kb::scene
