#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace kb::scene {

// LIB-129: a project-configurable set of up to 31 NAMED collision layers plus
// an interaction matrix governing which pairs actually generate a real
// physics response (contacts/triggers) in whichever backend is loaded.
// Deliberately capped at 31, not 32, matching kPhysicsAllLayers's own bit
// width exactly (PhysicsBackend.hpp's comment: the top bit is deliberately
// left clear so a layer bitmask stays representable as a positive signed int
// across the script boundary, where ScriptValueType::Int rejects negative
// values on write) - a 32nd named layer's bit would fall outside
// kPhysicsAllLayers's set bits, silently excluding it from every query that
// still uses the default mask.
inline constexpr std::uint32_t kPhysicsLayerCount = 31U;

// Every collider's OWN named layer is the LOWEST set bit of its (still raw,
// query-mask-shaped) ColliderComponent::layer - a collider only ever "is" one
// named layer for collision-response purposes, even though its bitmask can
// still name many layers for query-filtering purposes (Physics.*Cast/Overlap
// layerMask, unchanged by this). A collider that never set an explicit layer
// defaults to kPhysicsAllLayers (bit 0 set), so it resolves to layer 0
// ("Default") - matching every pre-LIB-129 collider's behavior exactly, as
// long as layer 0's interaction matrix row/column stays all-true (the
// PhysicsLayersAsset default below).
[[nodiscard]] inline std::uint32_t LowestSetPhysicsLayerIndex(std::uint32_t layerBitmask) noexcept {
    for (std::uint32_t index = 0U; index < kPhysicsLayerCount; ++index) {
        if ((layerBitmask & (1U << index)) != 0U) {
            return index;
        }
    }
    return 0U;
}

struct PhysicsLayersAsset {
    std::array<std::string, kPhysicsLayerCount> layerNames{};
    // Symmetric by convention (SetLayersInteract keeps both halves in sync);
    // defaults to every pair interacting, so a project that never configures
    // this asset - or configures it but leaves most layers alone - keeps
    // today's "everything collides with everything, static vs. dynamic is
    // purely a broad-phase detail" behavior unchanged.
    std::array<std::array<bool, kPhysicsLayerCount>, kPhysicsLayerCount> interactionMatrix{};

    PhysicsLayersAsset() {
        layerNames[0] = "Default";
        for (std::array<bool, kPhysicsLayerCount>& row : interactionMatrix) {
            row.fill(true);
        }
    }

    [[nodiscard]] int LayerIndex(std::string_view name) const noexcept {
        for (std::uint32_t index = 0U; index < kPhysicsLayerCount; ++index) {
            if (layerNames[index] == name) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    [[nodiscard]] bool LayersInteract(std::uint32_t layerA, std::uint32_t layerB) const noexcept {
        if (layerA >= kPhysicsLayerCount || layerB >= kPhysicsLayerCount) {
            return true;
        }
        return interactionMatrix[layerA][layerB];
    }

    void SetLayersInteract(std::uint32_t layerA, std::uint32_t layerB, bool interact) noexcept {
        if (layerA >= kPhysicsLayerCount || layerB >= kPhysicsLayerCount) {
            return;
        }
        interactionMatrix[layerA][layerB] = interact;
        interactionMatrix[layerB][layerA] = interact;
    }
};

} // namespace kb::scene
