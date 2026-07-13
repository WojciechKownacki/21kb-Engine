#pragma once

#include "engine/script/ScriptLifecycle.hpp"

#include <cstdint>

namespace kb::library {

// Engine21kbLibrary reuses kb::script::ScriptLifecycleEvent as-is: it is
// already the single, ordered list of phases the scheduler drives
// (Created -> Activated -> Ready -> (FixedTick*) -> Tick -> LateTick ->
// BeforeRender -> AfterRender -> Deactivated -> Destroyed; see
// ScriptLifecycle.hpp and the lifecycle table in
// others/Engine21kbLibrary.md section 3). kb::library never redeclares this
// enum or runs a second scheduler over it; it only classifies which public
// context shape (LIB-007: BehaviourContext, FixedContext, FrameContext,
// RenderContext) a script receives during each phase.
using LifecycleEvent = kb::script::ScriptLifecycleEvent;

// Which public context shape (LIB-007) a script receives for a given
// LifecycleEvent. Behaviour phases carry no per-frame timing data; Fixed
// carries the fixed step; Frame carries the variable frame delta; Render
// phases run around render submission and forbid mutating the current
// frame's render list (see Engine21kbLibrary.md section 3, "Dozwolone
// działanie" column).
enum class LibraryLifecycleContextKind : std::uint8_t {
    Behaviour,
    Fixed,
    Frame,
    Render,
};

[[nodiscard]] constexpr LibraryLifecycleContextKind ClassifyLifecycleContext(LifecycleEvent event) noexcept {
    switch (event) {
        case LifecycleEvent::Created:
        case LifecycleEvent::Activated:
        case LifecycleEvent::Ready:
        case LifecycleEvent::Deactivated:
        case LifecycleEvent::Destroyed:
            return LibraryLifecycleContextKind::Behaviour;
        case LifecycleEvent::FixedTick:
            return LibraryLifecycleContextKind::Fixed;
        case LifecycleEvent::Tick:
        case LifecycleEvent::LateTick:
            return LibraryLifecycleContextKind::Frame;
        case LifecycleEvent::BeforeRender:
        case LifecycleEvent::AfterRender:
            return LibraryLifecycleContextKind::Render;
    }
    return LibraryLifecycleContextKind::Behaviour;
}

[[nodiscard]] const char* ToString(LibraryLifecycleContextKind kind) noexcept;

} // namespace kb::library
