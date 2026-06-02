#pragma once

#include "kb/render/ViewIdPolicy.hpp"
#include "kb/render/frame/RenderGraphResource.hpp"
#include "kb/render/frame/RenderPassKind.hpp"
#include "kb/render/scene/MeshPipeline.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace kb::render {

struct RenderPassId {
    std::uint16_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return value != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(RenderPassId lhs, RenderPassId rhs) noexcept = default;
};

struct RenderPassDesc {
    RenderPassId id{};
    RenderPassKind kind = RenderPassKind::SceneTargetSetup;
    std::uint16_t viewId = ViewId::Invalid;
    std::optional<MeshPassType> meshPass{};
    std::vector<RenderGraphResourceId> reads;
    std::vector<RenderGraphResourceId> writes;
    bool emitsBgfxView = false;
    bool enabled = true;
    bool neverCull = false;

    [[nodiscard]] static RenderPassDesc Logical(RenderPassKind kind) {
        return RenderPassDesc{
            .id = RenderPassId{ static_cast<std::uint16_t>(static_cast<std::uint8_t>(kind) + 1U) },
            .kind = kind,
            .viewId = ViewId::Invalid,
            .meshPass = std::nullopt,
            .emitsBgfxView = false,
            .enabled = true,
        };
    }

    [[nodiscard]] static RenderPassDesc BgfxView(RenderPassKind kind, std::uint16_t viewId) {
        return RenderPassDesc{
            .id = RenderPassId{ static_cast<std::uint16_t>(static_cast<std::uint8_t>(kind) + 1U) },
            .kind = kind,
            .viewId = viewId,
            .meshPass = MeshPassForRenderPassKind(kind),
            .emitsBgfxView = true,
            .enabled = true,
        };
    }

    [[nodiscard]] constexpr bool HasValidView() const noexcept {
        return !emitsBgfxView || ViewId::IsValid(viewId);
    }

    RenderPassDesc& Reads(RenderGraphResourceId resource) {
        reads.push_back(resource);
        return *this;
    }

    RenderPassDesc& Writes(RenderGraphResourceId resource) {
        writes.push_back(resource);
        return *this;
    }

    RenderPassDesc& NeverCull() noexcept {
        neverCull = true;
        return *this;
    }
};

} // namespace kb::render
