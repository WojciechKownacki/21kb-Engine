#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

enum class InspectorPropertyId : std::uint16_t;

struct InspectorAudioScrubState {
    kb::scene::SceneEntity entity{};
    InspectorPropertyId property{};
    float startFloat = 0.0F;
    std::int64_t startInteger = 0;
    bool integer = false;
    bool active = false;
};

enum class InspectorAudioScrubUpdate : std::uint8_t {
    Unchanged,
    Changed,
    Invalid,
    LostTarget,
};

class InspectorAudioScrubController {
public:
    InspectorAudioScrubController() = delete;

    template <typename Transactions>
    [[nodiscard]] static bool Begin(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        InspectorPropertyId property,
        Transactions& transactions,
        InspectorAudioScrubState& state) {
        if (!Prepare(scene, entity, property, state)) {
            return false;
        }
        const std::string_view label = state.integer ? std::string_view{ "Edit Audio Listener" } : std::string_view{ "Edit Audio Component" };
        if (!transactions.Begin(std::string{ label })) {
            Reset(state);
            return false;
        }
        return true;
    }

    [[nodiscard]] static InspectorAudioScrubUpdate Update(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity selectedEntity,
        std::int64_t pixelDelta,
        InspectorAudioScrubState& state) noexcept;

    // Numeric audio fields are horizontal scrub controls. A click or vertical
    // pointer jitter must not mutate the authored value.
    [[nodiscard]] static std::optional<std::int64_t> ResolveHorizontalDragDelta(
        int startX,
        int startY,
        int x,
        int y,
        bool dragActive) noexcept;

    template <typename Transactions>
    [[nodiscard]] static bool Finish(
        const kb::scene::Scene& scene,
        kb::scene::SceneEntity selectedEntity,
        bool pointerMoved,
        Transactions& transactions,
        InspectorAudioScrubState& state) {
        const bool shouldCommit = pointerMoved && HasNetChange(scene, selectedEntity, state);
        bool committed = false;
        if (shouldCommit) {
            committed = transactions.Commit();
            if (!committed) {
                transactions.Cancel();
            }
        } else {
            transactions.Cancel();
        }
        Reset(state);
        return committed;
    }

    template <typename Transactions>
    static void Cancel(Transactions& transactions, InspectorAudioScrubState& state) {
        if (state.active) {
            transactions.Cancel();
        }
        Reset(state);
    }

    [[nodiscard]] static bool HasNetChange(
        const kb::scene::Scene& scene,
        kb::scene::SceneEntity selectedEntity,
        const InspectorAudioScrubState& state) noexcept;
    static void Reset(InspectorAudioScrubState& state) noexcept;

private:
    [[nodiscard]] static bool Prepare(
        const kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        InspectorPropertyId property,
        InspectorAudioScrubState& state) noexcept;
};

} // namespace kb::editor
