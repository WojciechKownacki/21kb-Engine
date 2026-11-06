#pragma once
#include "kb/render/resources/RenderMaterialGraphShaderToolchain.hpp"
#include <filesystem>
#include <span>
#include <string>
namespace kb::render {
[[nodiscard]] bool CookParticleProcess(const std::filesystem::path& source,const std::filesystem::path& output,
    const RenderMaterialGraphShaderToolchain& tools,std::span<const std::string> profiles,std::string& diagnostics);
}
