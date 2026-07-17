#pragma once

#include "engine/library/EngineLibraryEntityHandle.hpp"
#include "engine/library/EngineLibraryExecutionOrder.hpp"
#include "engine/library/EngineLibraryLifecycle.hpp"
#include "engine/script/ScriptExecutionContext.hpp"

namespace kb::library {

// Base wrapper every kb::library context shares: a non-owning view onto the
// ScriptRuntimeHost's ScriptExecutionContext for exactly the duration of one
// lifecycle callback. The ScriptExecutionContext it wraps is a call-scoped,
// stack-allocated object the runtime destroys the moment the callback
// returns (ScriptRuntime::DispatchBehaviour constructs one, invokes the
// callback, then lets it go out of scope) — so a Context must never outlive
// that callback either. This is enforced by the type itself, not just
// documented: copy and move are deleted (LIB-007), so a Context cannot be
// stored in a member, captured by value into a longer-lived closure, or
// otherwise smuggled out of the call that constructed it. Every accessor
// below returns by value (EntityId, EntityHandle, LifecycleEvent, float) —
// there is no method that hands back a reference of uncertain lifetime, and
// no escape hatch to the wrapped ScriptExecutionContext&: a callback that
// needs the full surface already holds that reference directly as its own
// parameter, so re-exposing it through the wrapper would only add a second,
// copy-and-retain-able path to the same danger.
class LibraryContextBase {
public:
    explicit LibraryContextBase(kb::script::ScriptExecutionContext& context) noexcept
        : context_(&context) {}

    LibraryContextBase(const LibraryContextBase&) = delete;
    LibraryContextBase& operator=(const LibraryContextBase&) = delete;
    LibraryContextBase(LibraryContextBase&&) = delete;
    LibraryContextBase& operator=(LibraryContextBase&&) = delete;

    [[nodiscard]] EntityId Self() const noexcept { return context_->Self().Id(); }
    // The full world-checked handle for the entity this callback runs on.
    // Safe to keep past the callback: it carries only the entity id and
    // originating Scene::Id(), and re-validates against whatever Scene is
    // passed to IsAlive()/Validate() later.
    [[nodiscard]] EntityHandle SelfHandle() const noexcept { return EntityHandle{ context_->Self(), context_->GetScene().Id() }; }
    [[nodiscard]] LifecycleEvent Phase() const noexcept { return context_->Lifecycle(); }

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
