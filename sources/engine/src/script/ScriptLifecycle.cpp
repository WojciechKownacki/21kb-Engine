#include "engine/script/ScriptLifecycle.hpp"

namespace kb::script {

const char* ToString(ScriptLifecycleEvent event) noexcept {
    switch (event) {
    case ScriptLifecycleEvent::Created:
        return "Created";
    case ScriptLifecycleEvent::Activated:
        return "Activated";
    case ScriptLifecycleEvent::Ready:
        return "Ready";
    case ScriptLifecycleEvent::FixedTick:
        return "FixedTick";
    case ScriptLifecycleEvent::Tick:
        return "Tick";
    case ScriptLifecycleEvent::LateTick:
        return "LateTick";
    case ScriptLifecycleEvent::BeforeRender:
        return "BeforeRender";
    case ScriptLifecycleEvent::AfterRender:
        return "AfterRender";
    case ScriptLifecycleEvent::Deactivated:
        return "Deactivated";
    case ScriptLifecycleEvent::Destroyed:
        return "Destroyed";
    }
    return "Tick";
}

kb::visual::VisualGraphLifecycleEvent ToVisualGraphLifecycleEvent(ScriptLifecycleEvent event) noexcept {
    switch (event) {
    case ScriptLifecycleEvent::Created:
        return kb::visual::VisualGraphLifecycleEvent::Created;
    case ScriptLifecycleEvent::Activated:
        return kb::visual::VisualGraphLifecycleEvent::Activated;
    case ScriptLifecycleEvent::Ready:
        return kb::visual::VisualGraphLifecycleEvent::Ready;
    case ScriptLifecycleEvent::FixedTick:
        return kb::visual::VisualGraphLifecycleEvent::FixedTick;
    case ScriptLifecycleEvent::Tick:
        return kb::visual::VisualGraphLifecycleEvent::Tick;
    case ScriptLifecycleEvent::LateTick:
        return kb::visual::VisualGraphLifecycleEvent::LateTick;
    case ScriptLifecycleEvent::BeforeRender:
        return kb::visual::VisualGraphLifecycleEvent::BeforeRender;
    case ScriptLifecycleEvent::AfterRender:
        return kb::visual::VisualGraphLifecycleEvent::AfterRender;
    case ScriptLifecycleEvent::Deactivated:
        return kb::visual::VisualGraphLifecycleEvent::Deactivated;
    case ScriptLifecycleEvent::Destroyed:
        return kb::visual::VisualGraphLifecycleEvent::Destroyed;
    }
    return kb::visual::VisualGraphLifecycleEvent::Tick;
}

} // namespace kb::script
