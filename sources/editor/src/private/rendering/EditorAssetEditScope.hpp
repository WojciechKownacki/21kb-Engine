#pragma once

#include "localization/EditorLocalization.hpp"
#include <string_view>

namespace kb::editor {
inline constexpr int kAssetEditScopeHeight = 24;
[[nodiscard]] inline std::string_view AssetEditScopeText() {
    return EditorText("asset.editScope", "Shared asset: Save affects all instances. Duplicate for separate edits.");
}
} // namespace kb::editor
