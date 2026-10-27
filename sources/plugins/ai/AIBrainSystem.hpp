#pragma once

#include "engine/scene/AIBrainComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace kb::scene { class Scene; }

namespace kb::ai_plugin {

// Thinks for every AI Brain a few times a second (its rules, top first) and moves it every frame:
// along the navigation mesh when the scene has one and the object a NavAgent, straight otherwise.
class AIBrainSystem final : public kb::scene::SceneSystem {
public:
    static constexpr float kThinkInterval = 0.2F;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;

    // Why a brain does what it does: the rule its last decision took, the condition and action of that rule, and the
    // enemy it was made against. `ruleIndex` is -1 when no rule held (the brain idles). Debug tools and tests read it;
    // nothing in the simulation depends on it. Empty for an object this system has not thought for yet.
    struct Decision {
        int ruleIndex = -1;
        kb::scene::AICondition condition = kb::scene::AICondition::Never;
        kb::scene::AIAction action = kb::scene::AIAction::Idle;
        float actionValue = 0.0F;
        kb::scene::SceneEntity enemy{};
    };
    [[nodiscard]] std::optional<Decision> LastDecision(kb::scene::SceneEntity entity) const;

private:
    struct Mind {
        kb::scene::SceneEntity entity{};
        kb::scene::Vec3 home{};
        kb::scene::Vec3 wanderPoint{};
        kb::scene::SceneEntity enemy{};
        kb::scene::AIAction action = kb::scene::AIAction::Idle;
        float actionValue = 0.0F;
        float thinkTimer = 0.0F;
        float wanderTimer = 0.0F;
        float plainHitTimer = 0.0F;
        std::uint32_t random = 0U;
        int ruleIndex = -1;
        kb::scene::AICondition ruleCondition = kb::scene::AICondition::Never;
        float ruleValue = 0.0F;
        bool thought = false;
        bool hasWanderPoint = false;
        bool moving = false;
        kb::scene::AIBrainComponent authored{};
    };

    void Think(kb::scene::Scene& scene, Mind& mind, const kb::scene::AIBrainComponent& brain);
    void Act(kb::scene::Scene& scene, Mind& mind, const kb::scene::AIBrainComponent& brain, float deltaSeconds);

    std::unordered_map<std::uint64_t, Mind> minds_;
};

} // namespace kb::ai_plugin
