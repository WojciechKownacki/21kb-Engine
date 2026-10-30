#pragma once
#include "engine/assets/IAssetLoader.hpp"
#include <map>
#include <span>
#include <string>
#include <vector>

namespace kb::particles {
inline constexpr std::string_view kParticleProcessAssetType="ParticleProcess";
inline constexpr std::uint32_t kParticleProcessContractVersion=1U;
// Version 1 processes billboard state after built-in integration. One persistent
// vec4 belongs to each live slot; births/restarts reset it before user code runs.
struct ParticleProcessAsset {
    std::string sourceName;
    std::map<std::string,std::vector<std::uint8_t>> programs;
};
[[nodiscard]] std::shared_ptr<ParticleProcessAsset> DecodeParticleProcessAsset(
    std::span<const std::uint8_t> bytes,std::string& error);
[[nodiscard]] bool EncodeParticleProcessAsset(const ParticleProcessAsset& asset,
    std::vector<std::uint8_t>& bytes,std::string& error);
class ParticleProcessAssetLoader final:public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override {return kParticleProcessAssetType;}
    [[nodiscard]] std::type_index PayloadType() const noexcept override {return typeid(ParticleProcessAsset);}
    [[nodiscard]] std::vector<std::string> Extensions() const override {return {".kbpsim"};}
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
};
} // namespace kb::particles
