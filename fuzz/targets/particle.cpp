// .kbvfx particle effects.
#include "FuzzSupport.hpp"

#include "engine/scene/ParticleEffectAssetIO.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static_cast<void>(kb::scene::ParticleEffectAssetIO::Parse(kb::fuzz::Text(data, size)));
    return 0;
}
