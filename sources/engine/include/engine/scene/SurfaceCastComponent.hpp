#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

// The RegionShapeComponent on this entity is the canonical projection volume.
// A cast never owns a second volume or a list of receiver entities.
enum class SurfaceCastContent : std::uint8_t {
    Material = 0U,
    Detail = 1U,
};

struct SurfaceCastComponent {
    static constexpr std::string_view StableId = "kb21.draw.d3.surface-cast";
    static constexpr std::uint32_t SchemaVersion = 1U;

    // Both variants use a material asset. Detail is a material authored for
    // overlay/projection use; the variant remains explicit in scene data.
    std::uint64_t materialAssetId = 0U;
    std::uint32_t receiverLayerMask = 0xFFFFFFFFU;
    std::int32_t order = 0;
    SurfaceCastContent content = SurfaceCastContent::Material;
    bool enabled = false;
};

[[nodiscard]] constexpr bool IsSurfaceCastContentValid(SurfaceCastContent value) noexcept {
    return value == SurfaceCastContent::Material || value == SurfaceCastContent::Detail;
}

[[nodiscard]] constexpr bool IsSurfaceCastComponentValid(const SurfaceCastComponent& value) noexcept {
    return value.materialAssetId != 0U && value.receiverLayerMask != 0U && IsSurfaceCastContentValid(value.content);
}

[[nodiscard]] constexpr bool IsSurfaceCastComponentPersistable(const SurfaceCastComponent& value) noexcept {
    return IsSurfaceCastComponentValid(value) || (!value.enabled && value.receiverLayerMask != 0U && IsSurfaceCastContentValid(value.content));
}

} // namespace kb::scene
