#pragma once

#include "engine/library/EngineLibraryEntityHandle.hpp"
#include "engine/library/EngineLibraryExecutionOrder.hpp"
#include "engine/library/EngineLibraryLifecycle.hpp"
#include "engine/script/ScriptExecutionContext.hpp"

namespace kb::library {

// Base wrapper every kb::library context shares: a non-owning view onto the
// ScriptRuntimeHost's ScriptExecutionContext for exactly the duration of one
// lifecycle callback. Never store a Context — or anything obtained through
// Raw() — past the callback that received it: the ScriptExecutionContext it
// wraps is a call-scoped, stack-allocated object the runtime destroys the
// moment the callback returns (ScriptRuntime::DispatchBehaviour constructs
// one, invokes the callback, then lets it go out of scope). The only value
// a context yields that is safe to keep is Self(), because EntityId (LIB-005)
// is a plain, generation-protected 64-bit value, not a reference.
class LibraryContextBase {
public:
    explicit LibraryContextBase(kb::script::ScriptExecutionContext& context) noexcept
        : context_(&context) {}

    [[nodiscard]] EntityId Self() const noexcept { return context_->Self().Id(); }
    // The full world-checked handle for the entity this callback runs on.
    // Safe to keep past the callback (unlike Raw()): it carries only the
    // entity id and originating Scene::Id(), and re-validates against
    // whatever Scene is passed to IsAlive()/Validate() later.
    [[nodiscard]] EntityHandle SelfHandle() const noexcept { return EntityHandle{ context_->Self(), context_->GetScene().Id() }; }
    [[nodiscard]] LifecycleEvent Phase() const noexcept { return context_->Lifecycle(); }

    // Escape hatch for kb::library modules (World, Transform, Time, ...)
    // that need the full ScriptExecutionContext surface (shared state,
    // function calls, events) this task does not re-expose on its own. Not
    // meant for game code, and not safe to retain past the current call for
    // the same reason the context itself is not.
    [[nodiscard]] kb::script::ScriptExecutionContext& Raw() noexcept { return *context_; }
    [[nodiscard]] const kb::script::ScriptExecutionContext& Raw() const noexcept { return *context_; }

protected:
    kb::script::ScriptExecutionContext* context_;
};

// Created, Activated, Ready, Deactivated, Destroyed (ClassifyLifecycleContext
// == LibraryLifecycleContextKind::Behaviour): no per-frame timing data is
// meaningful during these phases, so BehaviourContext adds nothing beyond
// Self()/Phase().
class BehaviourContext final : public LibraryContextBase {
public:
    using LibraryContextBase::LibraryContextBase;
};

// FixedTick (LibraryLifecycleContextKind::Fixed): the deterministic
// simulation step. FixedDeltaSeconds() is the fixed step configured on
// ScriptRuntimeFrameSettings, never the variable frame delta.
class FixedContext final : public LibraryContextBase {
public:
    using LibraryContextBase::LibraryContextBase;

    [[nodiscard]] float FixedDeltaSeconds() const noexcept { return context_->DeltaSeconds(); }
};

// Tick, LateTick (LibraryLifecycleContextKind::Frame): the variable
// per-frame delta, clamped to non-negative by ScriptRuntimeSceneSystem.
class FrameContext final : public LibraryContextBase {
public:
    using LibraryContextBase::LibraryContextBase;

    [[nodiscard]] float DeltaSeconds() const noexcept { return context_->DeltaSeconds(); }
};

// BeforeRender, AfterRender (LibraryLifecycleContextKind::Render): run
// around render submission. kb::library exposes no render-list mutator
// here, so there is nothing on this type that could violate the "do not
// mutate the current frame's render list" rule from the lifecycle contract
// (others/Engine21kbLibrary.md section 3) — a future render module can only
// add read-only or next-frame-scoped operations without breaking that rule.
class RenderContext final : public LibraryContextBase {
public:
    using LibraryContextBase::LibraryContextBase;
};

} // namespace kb::library
