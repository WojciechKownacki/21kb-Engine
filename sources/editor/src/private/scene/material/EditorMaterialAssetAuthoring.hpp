#pragma once

#include "scene/material/EditorMaterialAssetGateway.hpp"

#include <filesystem>

namespace kb::editor {

class EditorConsoleState;

class EditorMaterialAssetAuthoring {
public:
    EditorMaterialAssetAuthoring(kb::scene::Scene& scene, EditorAssetBrowserState& browser, EditorConsoleState& console) noexcept;

    [[nodiscard]] bool Create(const std::filesystem::path& virtualFolder);

private:
    EditorMaterialAssetGateway gateway_;
    EditorConsoleState& console_;
};

} // namespace kb::editor
