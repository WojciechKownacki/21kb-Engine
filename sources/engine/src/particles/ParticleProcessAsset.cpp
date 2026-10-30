#include "engine/particles/ParticleProcessAsset.hpp"
#include <algorithm>
#include <stdexcept>

namespace kb::particles {
namespace {
constexpr std::uint8_t magic[]{'K','B','P','S','I','M','0','1'};
bool Profile(std::string_view profile) {
    constexpr std::string_view names[]{"dxbc","dxil","spirv","glsl","essl","metal","wgsl"};
    return std::find(std::begin(names),std::end(names),profile)!=std::end(names);
}
void UInt(std::vector<std::uint8_t>& out,std::uint32_t n) {for(unsigned i=0;i<4U;++i) out.push_back(static_cast<std::uint8_t>(n>>(i*8U)));}
}
std::shared_ptr<ParticleProcessAsset> DecodeParticleProcessAsset(std::span<const std::uint8_t> bytes,std::string& error) {
    try {
        error.clear();
        if(bytes.size()<20U||bytes.size()>32U*1024U*1024U||!std::equal(std::begin(magic),std::end(magic),bytes.begin())) throw std::runtime_error("Invalid particle process header");
        std::size_t offset=8U;
        const auto read=[&]() {if(bytes.size()-offset<4U) throw std::runtime_error("Truncated particle process");std::uint32_t v=0U;for(unsigned i=0;i<4U;++i) v|=static_cast<std::uint32_t>(bytes[offset++])<<(i*8U);return v;};
        const auto text=[&](std::uint32_t limit) {const auto size=read();if(size>limit||size>bytes.size()-offset) throw std::runtime_error("Invalid particle process string");std::string s(reinterpret_cast<const char*>(bytes.data()+offset),size);offset+=size;return s;};
        if(read()!=kParticleProcessContractVersion) throw std::runtime_error("Unsupported particle process data contract");
        const auto count=read();if(count==0U||count>7U) throw std::runtime_error("Invalid particle process variant count");
        auto asset=std::make_shared<ParticleProcessAsset>();asset->sourceName=text(1024U);
        for(std::uint32_t i=0U;i<count;++i) {
            auto profile=text(16U);const auto size=read();
            if(!Profile(profile)||asset->programs.contains(profile)||size<4U||size>16U*1024U*1024U||size>bytes.size()-offset)
                throw std::runtime_error("Invalid particle process shader variant");
            if(bytes[offset]!='C'||bytes[offset+1U]!='S'||bytes[offset+2U]!='H') throw std::runtime_error("Particle process variant is not compute bytecode");
            asset->programs.emplace(std::move(profile),std::vector<std::uint8_t>(bytes.begin()+offset,bytes.begin()+offset+size));offset+=size;
        }
        if(offset!=bytes.size()) throw std::runtime_error("Particle process has trailing bytes");
        return asset;
    } catch(const std::exception& e) {error=e.what();return {};}
}
bool EncodeParticleProcessAsset(const ParticleProcessAsset& asset,std::vector<std::uint8_t>& bytes,std::string& error) {
    bytes.assign(std::begin(magic),std::end(magic));UInt(bytes,kParticleProcessContractVersion);UInt(bytes,static_cast<std::uint32_t>(asset.programs.size()));
    const auto text=[&](std::string_view s){UInt(bytes,static_cast<std::uint32_t>(s.size()));bytes.insert(bytes.end(),s.begin(),s.end());};
    if(asset.sourceName.size()>1024U||asset.programs.size()>7U) {error="Particle process metadata limit exceeded";bytes.clear();return false;}
    text(asset.sourceName);
    for(const auto& [profile,data]:asset.programs) {
        if(profile.size()>16U||data.size()>16U*1024U*1024U||bytes.size()+data.size()>32U*1024U*1024U) {error="Particle process bytecode limit exceeded";bytes.clear();return false;}
        text(profile);UInt(bytes,static_cast<std::uint32_t>(data.size()));bytes.insert(bytes.end(),data.begin(),data.end());
    }
    if(!DecodeParticleProcessAsset(bytes,error)) {bytes.clear();return false;}return true;
}
kb::assets::AssetLoadResult ParticleProcessAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> bytes;std::string error;
    if(!request.ReadSourceBytes(bytes,error)) return {.asset={},.error=std::move(error)};
    auto asset=DecodeParticleProcessAsset(bytes,error);return {.asset=std::move(asset),.error=std::move(error)};
}
} // namespace kb::particles
