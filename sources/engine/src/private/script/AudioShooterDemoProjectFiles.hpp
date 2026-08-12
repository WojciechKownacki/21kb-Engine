#pragma once

#include "engine/script/ScriptAgentProjectFiles.hpp"

#include <filesystem>

namespace kb::script {

[[nodiscard]] bool WriteAudioShooterDemoProjectFiles(
    const std::filesystem::path& projectRoot,
    ScriptAgentProjectFilesResult& result);

} // namespace kb::script
