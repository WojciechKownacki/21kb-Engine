#include "engine/scene/ParticleEffectAssetSchema.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kb::scene {

bool IsRepeatableParticleModule(ParticleModuleType type) noexcept {
    return type == ParticleModuleType::Wind || type == ParticleModuleType::Drag ||
           type == ParticleModuleType::SubEmitter || type == ParticleModuleType::Noise ||
           type == ParticleModuleType::AxisRotation || type == ParticleModuleType::Attraction ||
           type == ParticleModuleType::TorusSwirl || type == ParticleModuleType::Separation ||
           type == ParticleModuleType::CollisionGeometry;
}

bool IsValidParticleCollisionGeometry(const ParticleCollisionGeometryModule& v) noexcept {
    if(static_cast<unsigned>(v.kind)>static_cast<unsigned>(ParticleCollisionGeometryKind::Heightfield)||
        !std::isfinite(v.radius)||v.radius<0.001F||v.radius>100||!std::isfinite(v.restitution)||v.restitution<0||v.restitution>1||
        !std::isfinite(v.friction)||v.friction<0||v.friction>1||v.maxEventsPerStep==0||v.maxEventsPerStep>kParticleEffectMaxEventsPerStep) return false;
    if(v.kind==ParticleCollisionGeometryKind::PhysicsWorld) return v.layerMask!=0&&v.samples.empty();
    for(float n:{v.origin.x,v.origin.y,v.origin.z}) if(!std::isfinite(n)||std::abs(n)>1000000) return false;
    for(float n:{v.cellSize.x,v.cellSize.y,v.cellSize.z}) if(!std::isfinite(n)||n<0.001F||n>100) return false;
    if(v.width<2||v.width>128||v.depth<2||v.depth>128) return false;
    if(v.kind==ParticleCollisionGeometryKind::Heightfield ? v.height!=1 : (v.height<2||v.height>128)) return false;
    const auto count=static_cast<std::uint64_t>(v.width)*v.height*v.depth;
    return count<=32768&&count==v.samples.size()&&std::all_of(v.samples.begin(),v.samples.end(),[](float n){return std::isfinite(n)&&std::abs(n)<=1000000;});
}

bool IsValidParticleEffectUtf8(std::string_view text) noexcept {
    std::size_t index = 0U;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index++]);
        if (first <= 0x7FU)
            continue;
        std::uint32_t codepoint = 0U;
        std::size_t continuation = 0U;
        if ((first & 0xE0U) == 0xC0U) {
            codepoint = first & 0x1FU;
            continuation = 1U;
            if (codepoint < 2U)
                return false;
        } else if ((first & 0xF0U) == 0xE0U) {
            codepoint = first & 0x0FU;
            continuation = 2U;
        } else if ((first & 0xF8U) == 0xF0U) {
            codepoint = first & 0x07U;
            continuation = 3U;
        } else {
            return false;
        }
        if (index + continuation > text.size())
            return false;
        for (std::size_t part = 0U; part < continuation; ++part) {
            const auto byte = static_cast<unsigned char>(text[index++]);
            if ((byte & 0xC0U) != 0x80U)
                return false;
            codepoint = (codepoint << 6U) | (byte & 0x3FU);
        }
        if ((continuation == 2U && codepoint < 0x800U) || (continuation == 3U && codepoint < 0x10000U) ||
            codepoint > 0x10FFFFU || (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
            return false;
    }
    return true;
}

bool IsValidParticleEffectString(std::string_view text) noexcept {
    if (!IsValidParticleEffectUtf8(text))
        return false;
    return std::none_of(text.begin(), text.end(), [](char value) {
        const auto byte = static_cast<unsigned char>(value);
        return byte < 0x20U && value != '\n' && value != '\r' && value != '\t';
    });
}

ParticleModulePayload DefaultParticleModulePayload(ParticleModuleType type) {
    switch (type) {
    case ParticleModuleType::InitialVelocity:
        return ParticleInitialVelocityModule{};
    case ParticleModuleType::Gravity:
        return ParticleGravityModule{};
    case ParticleModuleType::Wind:
        return ParticleWindModule{};
    case ParticleModuleType::Drag:
        return ParticleDragModule{};
    case ParticleModuleType::ColorOverLife:
        return ParticleColorOverLifeModule{.gradient = {.stops = {
            {.time = 0.0F, .color = {1.0F, 1.0F, 1.0F, 1.0F}},
            {.time = 1.0F, .color = {1.0F, 1.0F, 1.0F, 0.0F}},
        }}};
    case ParticleModuleType::SizeOverLife:
        return ParticleSizeOverLifeModule{};
    case ParticleModuleType::AlphaOverLife:
        return ParticleAlphaOverLifeModule{};
    case ParticleModuleType::CollisionPlane:
        return ParticleCollisionPlaneModule{};
    case ParticleModuleType::SubEmitter:
        return ParticleSubEmitterModule{};
    case ParticleModuleType::Noise:
        return ParticleNoiseModule{};
    case ParticleModuleType::AxisRotation:
        return ParticleAxisRotationModule{};
    case ParticleModuleType::Attraction:
        return ParticleAttractionModule{};
    case ParticleModuleType::TorusSwirl:
        return ParticleTorusSwirlModule{};
    case ParticleModuleType::Separation:
        return ParticleSeparationModule{};
    case ParticleModuleType::CollisionGeometry:
        return ParticleCollisionGeometryModule{};
    }
    throw std::invalid_argument{"particle module type is invalid"};
}

ParticleOutputPayload DefaultParticleOutputPayload(ParticleOutputType type) {
    switch (type) {
    case ParticleOutputType::Billboard:
        return ParticleBillboardOutput{};
    case ParticleOutputType::StretchedBillboard:
        return ParticleStretchedBillboardOutput{};
    case ParticleOutputType::PointSprite:
        return ParticlePointSpriteOutput{};
    case ParticleOutputType::Mesh:
        return ParticleMeshOutput{};
    case ParticleOutputType::Trail:
        return ParticleTrailOutput{};
    case ParticleOutputType::Ribbon:
        return ParticleRibbonOutput{};
    case ParticleOutputType::Beam:
        return ParticleBeamOutput{};
    case ParticleOutputType::Volumetric:
        return ParticleVolumetricOutput{};
    case ParticleOutputType::Decal:
        return ParticleDecalOutput{};
    }
    throw std::invalid_argument{"particle output type is invalid"};
}

} // namespace kb::scene
