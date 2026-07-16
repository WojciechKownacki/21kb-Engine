#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Registers Particles.Create/Release/Exists/Play/Stop/IsPlaying/SetSeed/
// SetParameterScalar/ClearParameter/Emit/LiveCount as script functions - LIB-143's
// "particles/VFX jako assetowa instancja: play, stop, seed, parameter, event". Create
// resolves its "effect" argument (numeric asset id or virtual path, exactly like
// MaterialInstance.Create's "material" argument) against the scene's AssetRegistry and
// validates its type before creating the instance (kb::scene::SceneParticleSystems); the
// effect asset's own authored material reference is resolved at the same time, so Create
// fails honestly for either a bad effect or a bad material. `entity` (owner) is optional,
// defaulting to the calling entity (context.caller) - mirrors MeshRenderer.SetMesh's own
// "entity?" convention - particles spawn at the owner's live world position/rotation every
// emission, and the instance is auto-released the moment its owner dies or deactivates.
// Emit is LIB-143's "event" verb: an immediate, on-demand burst independent of Play/Stop
// state (Unity's ParticleSystem.Emit precedent), not an outbound completion notification -
// all five named verbs (play/stop/seed/parameter/event) are inbound instance control,
// documented in others/_temp.md's LIB-143 scope notes.
struct ScriptParticleSystemApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
