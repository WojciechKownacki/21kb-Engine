#pragma once

#include "engine/ecs/SystemSchedulerTrace.hpp"

#include <filesystem>

namespace kb::ecs {

void ExportSystemSchedulerTraceToJsonFile(const SystemSchedulerTrace& trace, const std::filesystem::path& path);

} // namespace kb::ecs
