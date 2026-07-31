#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class CameraProjection {
    Perspective,
    Orthographic
};

// LIB-136: how much of a camera's own render target this camera's submission
// touches before drawing - mirrors Unity's CameraClearFlags minus Skybox
// (this engine has no skybox pass). Deferred/GBuffer rendering always fully
// clears regardless of this setting - that clear is a correctness
// requirement for reconstructing lighting from the G-buffer, not a stylistic
// choice, so it is intentionally NOT gated by clearMode.
enum class CameraClearMode {
    SolidColor,
    DepthOnly,
    DontClear,
};

struct CameraComponent {
    static constexpr std::string_view StableId = "kb21.view.frame";
    static constexpr std::uint32_t SchemaVersion = 1U;

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
    // LIB-136: which render layers this camera draws, mirroring
    // ColliderComponent::layer's bitmask convention (a deliberately SEPARATE
    // namespace from physics layers - rendering and physics never share bits).
    // All bits set by default so every camera authored before LIB-136 keeps
    // rendering every mesh exactly as before.
    std::uint32_t cullingMask = 0xFFFFFFFFU;
    CameraClearMode clearMode = CameraClearMode::SolidColor;
    Vec3 clearColor{ 0.0F, 0.0F, 0.0F };
};

[[nodiscard]] constexpr bool IsCameraProjectionValid(CameraProjection value) noexcept {
    return value == CameraProjection::Perspective || value == CameraProjection::Orthographic;
}

[[nodiscard]] constexpr bool IsCameraClearModeValid(CameraClearMode value) noexcept {
    return value == CameraClearMode::SolidColor || value == CameraClearMode::DepthOnly || value == CameraClearMode::DontClear;
}

[[nodiscard]] inline bool IsCameraComponentValid(const CameraComponent& value) noexcept {
    return IsCameraProjectionValid(value.projection) && IsCameraClearModeValid(value.clearMode)
        && std::isfinite(value.verticalFovDegrees) && value.verticalFovDegrees > 0.0F && value.verticalFovDegrees < 180.0F
        && std::isfinite(value.orthographicHeight) && value.orthographicHeight > 0.0F
        && std::isfinite(value.nearClip) && value.nearClip > 0.0F
        && std::isfinite(value.farClip) && value.farClip > value.nearClip
        && std::isfinite(value.clearColor.x) && std::isfinite(value.clearColor.y) && std::isfinite(value.clearColor.z);
}

} // namespace kb::scene
