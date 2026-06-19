#pragma once

#include "engine/ecs/WorldTelemetry.hpp"

#include <filesystem>
#include <string>

namespace kb::ecs {

[[nodiscard]] std::string WorldTelemetrySnapshotToJson(const WorldTelemetrySnapshot& snapshot);
void ExportWorldTelemetrySnapshotToJsonFile(const WorldTelemetrySnapshot& snapshot, const std::filesystem::path& path);

} // namespace kb::ecs
