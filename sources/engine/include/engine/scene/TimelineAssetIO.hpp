#pragma once

#include "engine/scene/TimelineAsset.hpp"

#include <filesystem>
#include <istream>
#include <optional>

namespace kb::scene {

inline constexpr const char* kTimelineAssetExtension = ".kbtimeline";
inline constexpr const char* kTimelineAssetType = "Timeline";

class TimelineAssetIO final {
public:
    TimelineAssetIO() = delete;
    [[nodiscard]] static std::optional<TimelineAsset> Load(
        const std::filesystem::path& path);
    [[nodiscard]] static std::optional<TimelineAsset> Load(std::istream& input);
    [[nodiscard]] static bool Save(
        const std::filesystem::path& path, const TimelineAsset& asset);
};

} // namespace kb::scene
