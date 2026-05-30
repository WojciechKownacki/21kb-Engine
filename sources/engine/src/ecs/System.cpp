#include "engine/ecs/System.hpp"

namespace kb::ecs {

void System::OnCreate(World& world) {
    static_cast<void>(world);
}

void System::OnUpdate(World& world, float deltaSeconds) {
    static_cast<void>(world);
    static_cast<void>(deltaSeconds);
}

void System::OnDestroy(World& world) {
    static_cast<void>(world);
}

} // namespace kb::ecs
