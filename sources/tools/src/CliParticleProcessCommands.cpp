#include "CliCommands.hpp"
#include "CliCommon.hpp"
#include "kb/render/particles/ParticleProcessCook.hpp"
namespace kb::cli {
int RunParticleProcessCookCommand(const ArgumentList& arguments,CommandIo io) {
    const auto source=arguments.Option("--source"),output=arguments.Option("--output");
    constexpr std::string_view allowed[]{"--source","--output","--profile","--shaderc"};
    if(!source || !output || !arguments.Errors().empty() || !arguments.UnknownOptions(allowed).empty() || !arguments.Positionals().empty()) {
        io.err<<"error: particle-process-cook requires --source <file.particle.sc> --output <file.kbpsim> [--profile dxbc|dxil|spirv|glsl|essl|metal|all] [--shaderc <executable>]\n";return 1;
    }
    auto toolchain=kb::render::ResolveRenderMaterialGraphShaderToolchain({});
    if(auto compiler=arguments.Option("--shaderc")) toolchain.shadercPath=std::string{*compiler};
    std::vector<std::string> profiles;
    const auto profile=arguments.Option("--profile").value_or("all");
    if(profile=="all") profiles={"dxbc","dxil","spirv","glsl","essl","metal"};
    else profiles.emplace_back(profile);
    std::string error;
    if(!kb::render::CookParticleProcess(std::filesystem::path{*source},std::filesystem::path{*output},toolchain,profiles,error)) {io.err<<error<<'\n';return 1;}
    io.out<<"{\"schema\":\"21kb.particle-process/v1\",\"variants\":"<<profiles.size()<<",\"cooked\":true}\n";return 0;
}
}
