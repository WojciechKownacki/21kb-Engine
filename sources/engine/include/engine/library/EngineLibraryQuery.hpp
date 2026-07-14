#pragma once

#include "engine/ecs/StructuralChangeValidator.hpp"
#include "engine/ecs/World.hpp"
#include "engine/library/EngineLibraryEntityHandle.hpp"
#include "engine/library/EngineLibraryLifecycle.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <exception>
#include <type_traits>
#include <utility>

namespace kb::library {

namespace detail {
// LIB-078: same "dependent false" trick LIB-075's ScriptComponentAccess<T>
// uses — fires only when the primary QueryProvider template is actually
// instantiated for an unregistered Component, not at header-parse time.
template <typename Component>
struct LibraryQueryProviderUnregistered : std::false_type {};

// LIB-078: dispatches Query<Component>::ForEach to the real kb::scene
// iteration primitive for each of the registered component types that
// actually HAS one. Deliberately does NOT cover VisibilityComponent — no
// SceneVisibilityComponents::ForEach / SceneComponentVisitors entry point
// exists anywhere in kb::scene today (checked: no bulk-iteration
// mechanism for Visibility at all, unlike the other five) — instantiating
// Query<VisibilityComponent> fails to compile with a clear message
// instead of silently returning zero results or crashing. Adding that
// primitive to kb::scene is a bigger, separate change, not attempted
// here.
template <typename Component>
struct LibraryQueryProvider {
    static_assert(detail::LibraryQueryProviderUnregistered<Component>::value,
        "kb::library::Query<T>: this component type has no registered iteration primitive (LIB-078) — "
        "Transform/Behaviour/Camera/Light/MeshRenderer only; VisibilityComponent has no bulk ForEach "
        "anywhere in kb::scene today");
};

// LIB-078: Camera/Light/MeshRenderer iteration (SceneComponentVisitors::
// ForEachCamera/ForEachLight/ForEachMeshRenderer) runs through a cached
// flecs ecs_query_t* under the hood (SceneComponentIteration.hpp) — a C
// library's own iteration loop, which is NOT guaranteed to unwind a C++
// exception thrown from a callback it invokes cleanly (confirmed by
// testing: an early version of this file that let the visitor's
// exception propagate directly out of the trampoline crashed the whole
// process, exit code 3, instead of the exception reaching the caller's
// try/catch). The fix: the trampoline NEVER lets an exception cross back
// into the C iteration frame — it catches everything, stores it, and lets
// the C loop finish/return normally; ForEach then rethrows the stored
// exception once back on pure C++ ground. Transform/Behaviour iteration
// (no cached ecs_query_t*, a native-storage-only loop) is not confirmed
// to have this problem, but every provider uses the same pattern anyway —
// one exception-safety contract for all five, not two different ones
// depending on which internal iteration mechanism a given component
// happens to use today.
template <typename Visitor>
struct VisitorTrampolineContext {
    Visitor* visitor = nullptr;
    std::uint64_t sceneId = 0U;
    std::exception_ptr capturedException;
};

template <typename Visitor>
void RethrowIfCaptured(const VisitorTrampolineContext<Visitor>& context) {
    if (context.capturedException) {
        std::rethrow_exception(context.capturedException);
    }
}

template <>
struct LibraryQueryProvider<kb::scene::TransformComponent> {
    template <typename Visitor>
    static void ForEach(kb::scene::Scene& scene, Visitor& visitor) {
        VisitorTrampolineContext<Visitor> context{ &visitor, scene.Id(), {} };
        scene.Transforms().ForEach(
            [](kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* rawContext) {
                auto* typedContext = static_cast<VisitorTrampolineContext<Visitor>*>(rawContext);
                if (typedContext->capturedException) {
                    return;
                }
                try {
                    (*typedContext->visitor)(EntityHandle{ entity, typedContext->sceneId }, transform);
                } catch (...) {
                    typedContext->capturedException = std::current_exception();
                }
            },
            &context);
        RethrowIfCaptured(context);
    }
};

template <>
struct LibraryQueryProvider<kb::scene::BehaviourComponent> {
    template <typename Visitor>
    static void ForEach(kb::scene::Scene& scene, Visitor& visitor) {
        VisitorTrampolineContext<Visitor> context{ &visitor, scene.Id(), {} };
        scene.Components().Behaviours().ForEach(
            [](kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour, void* rawContext) {
                auto* typedContext = static_cast<VisitorTrampolineContext<Visitor>*>(rawContext);
                if (typedContext->capturedException) {
                    return;
                }
                try {
                    (*typedContext->visitor)(EntityHandle{ entity, typedContext->sceneId }, behaviour);
                } catch (...) {
                    typedContext->capturedException = std::current_exception();
                }
            },
            &context);
        RethrowIfCaptured(context);
    }
};

// Camera/Light/MeshRenderer visitors carry the entity's TransformComponent
// alongside the specific component (kb::scene::SceneVisitorTypes.hpp) —
// existing infrastructure built for rendering, which always needs both.
// Query<CameraComponent>/<LightComponent>/<MeshRendererComponent> only
// asked for one component, so the transform argument is real data that
// exists but is simply not forwarded to the caller's visitor here.
template <>
struct LibraryQueryProvider<kb::scene::CameraComponent> {
    template <typename Visitor>
    static void ForEach(kb::scene::Scene& scene, Visitor& visitor) {
        VisitorTrampolineContext<Visitor> context{ &visitor, scene.Id(), {} };
        scene.Components().Visitors().ForEachCamera(
            [](kb::scene::SceneEntity entity, const kb::scene::TransformComponent&, const kb::scene::CameraComponent& camera, void* rawContext) {
                auto* typedContext = static_cast<VisitorTrampolineContext<Visitor>*>(rawContext);
                if (typedContext->capturedException) {
                    return;
                }
                try {
                    (*typedContext->visitor)(EntityHandle{ entity, typedContext->sceneId }, camera);
                } catch (...) {
                    typedContext->capturedException = std::current_exception();
                }
            },
            &context);
        RethrowIfCaptured(context);
    }
};

template <>
struct LibraryQueryProvider<kb::scene::LightComponent> {
    template <typename Visitor>
    static void ForEach(kb::scene::Scene& scene, Visitor& visitor) {
        VisitorTrampolineContext<Visitor> context{ &visitor, scene.Id(), {} };
        scene.Components().Visitors().ForEachLight(
            [](kb::scene::SceneEntity entity, const kb::scene::TransformComponent&, const kb::scene::LightComponent& light, void* rawContext) {
                auto* typedContext = static_cast<VisitorTrampolineContext<Visitor>*>(rawContext);
                if (typedContext->capturedException) {
                    return;
                }
                try {
                    (*typedContext->visitor)(EntityHandle{ entity, typedContext->sceneId }, light);
                } catch (...) {
                    typedContext->capturedException = std::current_exception();
                }
            },
            &context);
        RethrowIfCaptured(context);
    }
};

template <>
struct LibraryQueryProvider<kb::scene::MeshRendererComponent> {
    template <typename Visitor>
    static void ForEach(kb::scene::Scene& scene, Visitor& visitor) {
        VisitorTrampolineContext<Visitor> context{ &visitor, scene.Id(), {} };
        scene.Components().Visitors().ForEachMeshRenderer(
            [](kb::scene::SceneEntity entity, const kb::scene::TransformComponent&, const kb::scene::MeshRendererComponent& meshRenderer, void* rawContext) {
                auto* typedContext = static_cast<VisitorTrampolineContext<Visitor>*>(rawContext);
                if (typedContext->capturedException) {
                    return;
                }
                try {
                    (*typedContext->visitor)(EntityHandle{ entity, typedContext->sceneId }, meshRenderer);
                } catch (...) {
                    typedContext->capturedException = std::current_exception();
                }
            },
            &context);
        RethrowIfCaptured(context);
    }
};

} // namespace detail

// LIB-078: a script-facing, phase-gated read-only query over one of the
// component types registered for scripts (LIB-075/076/077's closed set —
// Transform/Behaviour/Camera/Light/MeshRenderer; Visibility has no
// registered iteration primitive, see LibraryQueryProvider's comment).
// Native C++ script code only — Lua/Visual Graph cannot express a C++
// template, so they never reach this type; ScriptFunctionRegistry-backed
// World.* functions remain the cross-frontend surface (LIB-065..077).
//
// "Only for phases that allow iteration" (this task's own name) reuses
// LIB-007's EXISTING phase classification instead of inventing a second
// one: ForEach() proceeds only when ClassifyLifecycleContext(event) is
// Fixed, Frame, or Render — never Behaviour (Created/Activated/Ready/
// Deactivated/Destroyed), the same phases where CreateEntity/Destroy/
// component Add/Remove are routine and structural, and where scene
// dispatch itself is mid-snapshot-collection (see
// EngineLibraryCommandApplication.hpp's own note on this). Returns false
// without iterating for a Behaviour phase — an honest rejection, not a
// crash or empty-but-silent success.
//
// "Ban on structural change in the loop" is NOT a new mechanism: ForEach()
// enters kb::ecs::World::EnterIteration() (the SAME
// kb::ecs::StructuralChangeValidator instance World::CreateEntity/
// DestroyEntity/SetComponent/RemoveComponent already check) for the
// duration of the call, via the public kb::scene::SceneRuntime::EcsWorld()
// accessor — so a script calling World.Spawn/World.Destroy/
// EntityHandle::Add<T>/Remove<T> from inside a visitor throws
// std::logic_error, caught and re-thrown by the exception-safe trampoline
// above so it reaches the CALLER (not the C iteration frame) exactly as
// it already would inside a kb::ecs::Query<T...> loop. Recording changes
// to apply after the loop instead is kb::ecs::CommandBuffer's job
// (LIB-080, not wrapped here yet).
template <typename Component>
class Query final {
public:
    Query() = delete;

    template <typename Visitor>
    [[nodiscard]] static bool ForEach(kb::scene::Scene& scene, LifecycleEvent event, Visitor&& visitor) {
        if (ClassifyLifecycleContext(event) == LibraryLifecycleContextKind::Behaviour) {
            return false;
        }
        const kb::ecs::StructuralChangeValidator::Guard iterationGuard = scene.Runtime().EcsWorld().EnterIteration();
        detail::LibraryQueryProvider<Component>::ForEach(scene, visitor);
        return true;
    }
};

} // namespace kb::library
