#pragma once

#include <cstdint>

namespace kb::scene {

enum class CameraProjection {
    Perspective,
    Orthographic
};

struct CameraComponent {
    CameraProjection projection = CameraProjection::Perspective;
    float verticalFovDegrees = 60.0F;
    float orthographicHeight = 10.0F;
    float nearClip = 0.01F;
    float farClip = 1000.0F;
    bool primary = false;
    // LIB-135: which render viewport this camera targets, matching
    // kb::render::RenderViewportId::value (kb::scene stays renderer-agnostic,
    // so this is carried as a plain stable id, not the kb::render type itself
    // - mirrors how AssetRef/EntityHandle cross module boundaries). 0 means
    // "any viewport" - every camera authored before LIB-135 keeps rendering
    // to every viewport exactly as before.
    std::uint32_t viewportId = 0;
    // LIB-135: breaks ties when more than one primary camera targets the same
    // viewport - higher wins, mirrors InputComponent::priority's exact
    // convention. Replaces RenderScene::BuildPrimaryCamera's previous
    // unordered_map-iteration-order tie-break, which was non-deterministic.
    std::int32_t priority = 0;
};

} // namespace kb::scene
