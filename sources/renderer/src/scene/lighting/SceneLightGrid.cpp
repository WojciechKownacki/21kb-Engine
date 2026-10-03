#include "scene/lighting/SceneLightGrid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kb::render {

bool SceneLightGrid::Build(const RenderScene::LightProxyMap& lights,
    SceneRenderLightingConfig config, std::uint32_t cameraMask,
    std::uint64_t primaryLightId, std::uint32_t maxAtlasTexels) {
    atlas_.clear();
    lightCount_ = cellCount_ = 0U;
    dimensions_ = {};
    minimum_ = inverseCellSize_ = {};
    std::vector<std::pair<std::uint64_t, const LightRenderProxyDesc*>> selected;
    selected.reserve(lights.size());
    for (const auto& [id, proxy] : lights) {
        if (SceneLightShaderData::IsValid(proxy.desc) && (proxy.desc.layer & cameraMask) != 0U)
            selected.emplace_back(id, &proxy.desc);
    }
    std::ranges::sort(selected, [primaryLightId](const auto& lhs, const auto& rhs) {
        if ((lhs.first == primaryLightId) != (rhs.first == primaryLightId)) return lhs.first == primaryLightId;
        return lhs.first < rhs.first;
    });
    if (selected.size() > maxAtlasTexels / 5U) return false;
    for (const auto& [id, light] : selected) {
        static_cast<void>(id);
        const auto packed = SceneLightShaderData::Pack(*light);
        atlas_.insert(atlas_.end(), packed.texels.begin(), packed.texels.end());
    }
    if (const auto preview = SceneLightShaderData::EditorPreview(config))
        atlas_.insert(atlas_.end(), preview->texels.begin(), preview->texels.end());
    if (atlas_.size() > maxAtlasTexels) return false;
    for (const auto& texel : atlas_)
        for (const auto value : texel) if (!std::isfinite(value)) return false;
    lightCount_ = static_cast<std::uint32_t>(atlas_.size() / 5U);

    std::array<float, 3> maximum{};
    minimum_.fill(std::numeric_limits<float>::infinity());
    minimum_[3] = 0.0F;
    maximum.fill(-std::numeric_limits<float>::infinity());
    const auto radiusFor = [this](std::uint32_t index) {
        const auto base = index * 5U;
        const auto& size = atlas_[base + 3U];
        return atlas_[base + 1U][3] + (atlas_[base][3] > 2.5F ? (size[2] + size[3]) * 0.5F : 0.0F);
    };
    for (std::uint32_t index = 0U; index < lightCount_; ++index) {
        if (atlas_[index * 5U][3] == 0.0F) continue;
        const float radius = radiusFor(index);
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            const float center = atlas_[index * 5U + 1U][axis];
            minimum_[axis] = std::min(minimum_[axis], center - radius);
            maximum[axis] = std::max(maximum[axis], center + radius);
        }
    }
    cellCount_ = 1U;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const auto dimension = std::clamp<std::uint16_t>(config.clusterDimensions[axis], 1U, 64U);
        dimensions_[axis] = static_cast<float>(dimension);
        cellCount_ *= dimension;
        if (minimum_[axis] == std::numeric_limits<float>::infinity()) {
            minimum_[axis] = -1.0F;
            maximum[axis] = 1.0F;
        }
        minimum_[axis] = std::nextafter(minimum_[axis], -std::numeric_limits<float>::infinity());
        maximum[axis] = std::nextafter(maximum[axis], std::numeric_limits<float>::infinity());
        const double span = static_cast<double>(maximum[axis]) - minimum_[axis];
        if (span > std::numeric_limits<float>::max()) return false;
        inverseCellSize_[axis] = static_cast<float>(dimension / span);
        if (!std::isfinite(minimum_[axis]) || !std::isfinite(maximum[axis]) ||
            !(inverseCellSize_[axis] > 0.0F) || !std::isfinite(inverseCellSize_[axis])) return false;
    }
    if (static_cast<std::uint64_t>(HeaderOffset()) + cellCount_ + 1U > maxAtlasTexels) return false;
    counts_.assign(cellCount_ + 1U, 0U);
    const auto maxReferences = std::min<std::uint64_t>(1U << 24U,
        static_cast<std::uint64_t>(maxAtlasTexels - IndexOffset()) * 4U);
    // The same traversal counts and writes CSR entries, with no capped per-cell array.
    const auto visit = [&](auto&& accept) {
        std::uint64_t references = 0U;
        for (std::uint32_t index = 0U; index < lightCount_; ++index) {
            if (atlas_[index * 5U][3] == 0.0F) {
                references += cellCount_ + 1U;
                if (references > maxReferences) return false;
                for (std::uint32_t cell = 0U; cell <= cellCount_; ++cell) accept(cell, index);
                continue;
            }
            std::array<std::uint32_t, 3> first{}, last{};
            const float radius = radiusFor(index);
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                const auto dimension = static_cast<std::uint32_t>(dimensions_[axis]);
                const float center = atlas_[index * 5U + 1U][axis];
                const auto coordinate = [&](float bound) {
                    return static_cast<std::uint32_t>(std::clamp(std::floor(
                        (bound - minimum_[axis]) * inverseCellSize_[axis]), 0.0F, static_cast<float>(dimension - 1U)));
                };
                first[axis] = coordinate(center - radius);
                last[axis] = coordinate(center + radius);
            }
            references += static_cast<std::uint64_t>(last[0] - first[0] + 1U) *
                (last[1] - first[1] + 1U) * (last[2] - first[2] + 1U);
            if (references > maxReferences) return false;
            const auto nx = static_cast<std::uint32_t>(dimensions_[0]), ny = static_cast<std::uint32_t>(dimensions_[1]);
            for (auto z = first[2]; z <= last[2]; ++z)
                for (auto y = first[1]; y <= last[1]; ++y)
                    for (auto x = first[0]; x <= last[0]; ++x) accept(1U + x + nx * (y + ny * z), index);
        }
        return true;
    };
    if (!visit([this](std::uint32_t cell, std::uint32_t) { ++counts_[cell]; })) return false;
    const auto headerOffset = HeaderOffset(), indexOffset = IndexOffset();
    std::uint64_t entries = 0U;
    for (const auto count : counts_) entries += count;
    // Float indices/offsets are exact below 2^24, and the atlas has a real allocation budget.
    if (entries > (1U << 24U) || indexOffset + (entries + 3U) / 4U > maxAtlasTexels) return false;
    atlas_.resize(indexOffset + static_cast<std::size_t>((entries + 3U) / 4U));
    std::uint32_t offset = 0U;
    for (std::uint32_t cell = 0U; cell <= cellCount_; ++cell) {
        atlas_[headerOffset + cell] = {static_cast<float>(offset), static_cast<float>(counts_[cell]), 0.0F, 0.0F};
        offset += counts_[cell];
        counts_[cell] = 0U;
    }
    static_cast<void>(visit([&](std::uint32_t cell, std::uint32_t index) {
        const auto address = static_cast<std::uint32_t>(atlas_[headerOffset + cell][0]) + counts_[cell]++;
        atlas_[indexOffset + address / 4U][address % 4U] = static_cast<float>(index);
    }));
    dimensions_[3] = 1.0F;
    return true;
}

std::uint32_t SceneLightGrid::CellFor(const std::array<float, 3>& position) const noexcept {
    std::array<std::uint32_t, 3> cell{};
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const float value = std::floor((position[axis] - minimum_[axis]) * inverseCellSize_[axis]);
        if (!std::isfinite(value) || value < 0.0F || value >= dimensions_[axis]) return 0U;
        cell[axis] = static_cast<std::uint32_t>(value);
    }
    return 1U + cell[0] + static_cast<std::uint32_t>(dimensions_[0]) *
        (cell[1] + static_cast<std::uint32_t>(dimensions_[1]) * cell[2]);
}

} // namespace kb::render
