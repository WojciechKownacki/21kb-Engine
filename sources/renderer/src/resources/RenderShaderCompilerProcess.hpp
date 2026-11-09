#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace kb::render::detail {
[[nodiscard]] int RunRenderShaderCompilerProcess(const std::filesystem::path& compiler,
    const std::vector<std::string>& arguments,const std::filesystem::path& diagnostics);
}
