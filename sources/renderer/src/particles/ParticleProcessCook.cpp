#include "kb/render/particles/ParticleProcessCook.hpp"
#include "engine/particles/ParticleProcessAsset.hpp"
#include "../resources/RenderShaderCompilerProcess.hpp"
#include "../resources/RenderMaterialAtomicFileWriter.hpp"
#include <fstream>
#include <iterator>
#include <sstream>

namespace kb::render {
namespace {
constexpr std::string_view prefix=R"SHADER(#include <bgfx_compute.sh>
BUFFER_RO(processBirths, vec4, 0);
BUFFER_RW(processInstances, vec4, 1);
BUFFER_RW(processMotion, vec4, 2);
BUFFER_RW(processCustom, vec4, 3);
uniform vec4 u_particleProcessFrame;
uniform mat4 u_particleProcessInverse;
struct ParticleProcess {
    vec3 position;
    vec3 velocity;
    vec4 color;
    vec4 custom;
    float size;
    float rotation;
    float age;
    float lifetime;
};
)SHADER";
constexpr std::string_view suffix=R"SHADER(
#line 1 "particle_process_contract"
NUM_THREADS(64, 1, 1)
void main() {
    uint slot=gl_GlobalInvocationID.x;
    if(slot>=uint(u_particleProcessFrame.y)) return;
    vec4 birth=processBirths[slot*2u];
    float lifetime=processBirths[slot*2u+1u].w;
    float now=u_particleProcessFrame.x;
    float age=now-birth.w;
    if(lifetime<=0.0 || age<0.0 || age>=lifetime || processMotion[slot*2u+1u].w<=0.0) return;
    uint base=slot*5u;
    vec4 meta=processCustom[slot*2u+1u];
    float dt=max(0.0,now-(meta.x>0.0 ? meta.y : birth.w));
    ParticleProcess p;
    p.position=processInstances[base].xyz;
    p.size=processInstances[base].w;
    p.velocity=processInstances[base+2u].xyz;
    p.rotation=processInstances[base+1u].w;
    p.color=processInstances[base+3u];
    p.custom=meta.x>0.0 ? processCustom[slot*2u] : vec4_splat(0.0);
    p.age=age;
    p.lifetime=lifetime;
    particle_process(p,dt,now);
    processInstances[base]=vec4(p.position,max(p.size,0.0));
    vec4 previous=processInstances[base+1u];
    processInstances[base+1u]=vec4(previous.xyz,p.rotation);
    processInstances[base+2u]=vec4(p.velocity,processInstances[base+2u].w);
    processInstances[base+3u]=p.color;
    bool local=u_particleProcessFrame.z>0.5;
    vec3 recordPosition=local ? mul(u_particleProcessInverse,vec4(p.position,1.0)).xyz : p.position;
    vec3 recordVelocity=local ? mul(u_particleProcessInverse,vec4(p.velocity,0.0)).xyz : p.velocity;
    processMotion[slot*2u]=vec4(recordPosition,now);
    processMotion[slot*2u+1u]=vec4(recordVelocity,birth.w+1.0);
    processCustom[slot*2u]=p.custom;
    processCustom[slot*2u+1u]=vec4(1.0,now,0.0,0.0);
}
)SHADER";
}
bool CookParticleProcess(const std::filesystem::path& source,const std::filesystem::path& output,
    const RenderMaterialGraphShaderToolchain& toolchain,std::span<const std::string> profiles,std::string& diagnostics) {
    diagnostics.clear();
    try {
        if(!toolchain.Available() || profiles.empty()) {diagnostics="Particle process cook needs shaderc, includes and at least one profile";return false;}
        if(std::filesystem::absolute(source).lexically_normal()==std::filesystem::absolute(output).lexically_normal()) {
            diagnostics="Particle process output must differ from source";return false;
        }
        std::ifstream input(source,std::ios::binary);
        if(!input) {diagnostics="Cannot read particle process source";return false;}
        std::string code{std::istreambuf_iterator<char>{input},{}};
        const auto filename=source.filename().string();
        if(code.size()>1024U*1024U || filename.find_first_of("\"\\\r\n")!=std::string::npos) {diagnostics="Invalid particle process source or filename";return false;}
        // A private staging directory; the output is replaced only after every variant succeeds.
        auto stage=detail::MaterialAtomicTempPath(output);stage+=".cook";
        if(!std::filesystem::create_directories(stage)) {diagnostics="Cannot create particle process staging directory";return false;}
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{stage};
        auto generated=stage/"process.sc";
        {std::ofstream file(generated,std::ios::binary);file<<prefix<<"#line 1 \""<<filename<<"\"\n"<<code<<suffix;if(!file) {diagnostics="Cannot write shader wrapper";return false;}}
        kb::particles::ParticleProcessAsset asset;asset.sourceName=filename;
        for(const auto& profile:profiles) {
            std::string platform,language;
            if(profile=="dxbc") {platform="windows";language="s_5_0";}
            else if(profile=="dxil") {platform="windows";language="s_6_0";}
            else if(profile=="spirv") {platform="linux";language="spirv";}
            else if(profile=="glsl") {platform="linux";language="430";}
            else if(profile=="essl") {platform="android";language="300_es";}
            else if(profile=="metal") {platform="osx";language="metal";}
            else {diagnostics="Unsupported particle process profile: "+profile;return false;}
            if(asset.programs.contains(profile)) continue;
            auto binary=stage/(profile+".bin"),log=stage/(profile+".log");
            std::vector<std::string> args{"-f",generated.string(),"-o",binary.string(),"--type","compute","--platform",platform,"--profile",language,"--varyingdef",toolchain.varyingDefPath};
            // Preserve authored includes relative to the original file, without embedding its absolute path in the asset.
            args.insert(args.end(),{"-i",source.parent_path().string()});
            for(const auto& include:toolchain.includeDirs) args.insert(args.end(),{"-i",include});
            int exit=detail::RunRenderShaderCompilerProcess(toolchain.shadercPath,args,log);
            std::ifstream diagnosticFile(log,std::ios::binary);std::string compilerLog{std::istreambuf_iterator<char>{diagnosticFile},{}};
            if(exit!=0) {
                // Only diagnostics opt into line directives: successful shader bytes stay free of
                // compiler-machine paths, and backends that reject filename directives keep the
                // original error plus an explicitly numbered authored source listing.
                args.emplace_back("--source-locations");
                const auto mappedLog=stage/(profile+".source.log");
                static_cast<void>(detail::RunRenderShaderCompilerProcess(toolchain.shadercPath,args,mappedLog));
                std::ifstream mappedFile(mappedLog,std::ios::binary);
                const std::string mapped{std::istreambuf_iterator<char>{mappedFile},{}};
                diagnostics=filename+" ["+profile+"]\n";
                std::istringstream authored(code);std::string line;std::uint32_t number=0U;
                while(std::getline(authored,line)) diagnostics+=filename+":"+std::to_string(++number)+": "+line+"\n";
                diagnostics+=compilerLog;
                if(!mapped.empty()) diagnostics+="\nSource-location diagnostics:\n"+mapped;
                return false;
            }
            std::ifstream compiled(binary,std::ios::binary);
            if(!compiled) {diagnostics="Shader compiler produced no output";return false;}
            asset.programs.emplace(profile,std::vector<std::uint8_t>{std::istreambuf_iterator<char>{compiled},{}});
        }
        std::vector<std::uint8_t> bytes;if(!kb::particles::EncodeParticleProcessAsset(asset,bytes,diagnostics)) return false;
        if(!detail::WriteMaterialFileAtomically(output,[&](std::ostream& stream){stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));})) {
            diagnostics="Cannot publish compiled particle process";return false;
        }
        return true;
    } catch(const std::exception& e) {diagnostics=e.what();return false;}
}
} // namespace kb::render
