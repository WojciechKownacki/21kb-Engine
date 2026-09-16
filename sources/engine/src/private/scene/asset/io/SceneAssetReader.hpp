#pragma once

#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/ScenePrefab.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace kb::scene {

class SceneAssetReader {
public:
    SceneAssetReader() = delete;

    [[nodiscard]] static SceneDocumentLoadResult Read(const std::filesystem::path& path);
    [[nodiscard]] static SceneDocumentLoadResult Read(std::vector<std::uint8_t> bytes);
    // Before v36 a dropdown's options were its child objects and the text each showed. They become the
    // dropdown's own option list, in child order, carrying that text; the children lose their UI
    // components so they no longer draw over the control, but stay in the hierarchy with their names
    // for the author to delete. A child without text becomes an option with an empty label, keeping
    // indices - and so the stored selection - pointing at the same choice.
    static void ConvertChildDropdownOptions(ScenePrefab& prefab);
};

} // namespace kb::scene
