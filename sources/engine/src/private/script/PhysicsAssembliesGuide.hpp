#pragma once
#include <string_view>
namespace kb::script {
inline constexpr std::string_view kPhysicsAssembliesGuide = R"guide(# Authored physics assemblies

Physics assemblies use the active Jolt world and the existing scene body,
animation, mesh and asset systems. Include `engine/scene/PhysicsAssembly.hpp`.
Create directly from `PhysicsAssemblyAsset`, or load a saved `.kbphysics` asset:

```cpp
auto result = kb::scene::PhysicsAssemblies::Load(scene, owner, "/Game/Physics/Car.kbphysics");
if (!result.Succeeded()) throw std::runtime_error(result.error);
std::string error;
if (!kb::scene::PhysicsAssemblies::Drive(scene, result.handle, 0.6F, 0.2F, 0, error))
    throw std::runtime_error(error);
kb::scene::PhysicsAssemblyState state;
if (!kb::scene::PhysicsAssemblies::State(scene, result.handle, state, error))
    throw std::runtime_error(error);
// state.wheelContacts, speed, entities and cloth vertices are observations of the solver.
```

Handles are scene-local and expire on Destroy or owner destruction. Owners need
unit world scale; author physical dimensions in the asset. Instantiate a ragdoll
after its skeletal Animator has initialized. Other kinds can be created in Ready.
Rebuild native modules against the current SDK (native registration API version 4);
the registration guard rejects older compiled modules.

Assets are strict JSON: unknown/inapplicable fields, invalid dimensions, parent
order, grid size and non-finite values fail without creating an assembly.
`EncodePhysicsAssembly`, `DecodePhysicsAssembly` and `SavePhysicsAssembly` share
validation with runtime Create. A failed validation does not overwrite a file.

```json
{"schema":"21kb.physics-assembly/v1","kind":"Vehicle","size":[1.6,0.5,3],"mass":700,"wheelRadius":0.35,"suspensionLength":0.4,"engineTorque":300}
```

A Vehicle creates a chassis and four rendered wheels. Jolt's wheeled controller,
engine, differential, tire contacts and suspension solve motion; Drive takes
throttle/steering in [-1,1] and brake in [0,1]. The owner must not already have
Rigidbody/Collider. Physics can roll the vehicle; no artificial upright assist is
applied. State.entities lists the chassis owner followed by four wheels.

```json
{"schema":"21kb.physics-assembly/v1","kind":"Cloth","columns":8,"rows":8,"spacing":0.25,"mass":2,"compliance":0.00001,"bendingCompliance":0.001,"pinTopEdge":true}
```

Cloth is a Jolt soft-body triangle grid, up to 32x32 vertices, with stretch,
shear/bend constraints and optional pinned first row. It starts in local XY,
extending down -Y; rotate the owner to hang or drape it. Position/rotation of this
anchor remain fixed while active. State.vertices are owner-local positions.
The renderer updates a generated mesh and normals from solver vertices. It is
a deforming surface, not separate rigid particles. Volumetric soft bodies,
arbitrary cloth topology and skinned cloth attachments are not provided.

```json
{"schema":"21kb.physics-assembly/v1","kind":"Ragdoll","bones":[{"name":"Root","parent":-1,"center":[0,0.4,0],"radius":0.14,"height":0.8,"mass":5},{"name":"Tip","parent":0,"center":[0,0.4,0],"radius":0.14,"height":0.8,"mass":5}]}
```

Ragdoll maps exact skeleton bone names to authored capsules and point joints,
disables collision between adjacent bodies, and sends simulated bone poses into
the existing skeletal skinning solve. The actor follows the root bone. Call
SimulateRagdoll(false) to restore animation and make bodies follow evaluated
bones kinematically; true returns them to dynamics. The owner must be a hierarchy
root with an initialized skeletal Animator and no root-motion owner. Capsule
axes follow bone-local Y. Anatomical angular limits, automatic body fitting and
blended get-up transitions are outside this profile. State.entities follows the
asset's bone order. Existing Physics impulse/velocity APIs work on those bodies.

```json
{"schema":"21kb.physics-assembly/v1","kind":"Fracture","size":[1.5,1.5,1.5],"mass":10,"cellsX":2,"cellsY":2,"cellsZ":2}
```

Fracture authors a static solid box and planar box-cell cuts (2..64 pieces).
Fracture(handle, impulse) removes the whole collider and creates corresponding
rendered cell geometry with independent dynamic colliders, distributed mass and
initial momentum. It is not arbitrary imported-mesh cutting or a damage model.
The owner must not already own Rigidbody/Collider. State.entities contains the
fragments after the trigger; repeat triggers fail explicitly.

```json
{"schema":"21kb.physics-assembly/v1","kind":"Buoyancy","waterLevel":1,"buoyancy":1.5,"linearDrag":2,"angularDrag":0.5,"fluidVelocity":[0,0,0]}
```

Buoyancy attaches to an existing dynamic rigid body. Jolt integrates submerged
shape volume against a horizontal water plane, buoyancy, linear/angular drag and
fluid flow. Buoyancy is a relative-density factor, not a force per frame.
SetWaterLevel changes immersion at runtime. This is flat-water physics; it does
not sample rendered waves or model fluid particles. State.submerged reports a
nonzero displaced volume.

Lua calls the same backend:

```lua
local handle, diagnostic = Physics.AssemblyLoad(self.entity, "/Game/Physics/Car.kbphysics")
assert(handle, diagnostic)
assert(Physics.AssemblyDrive(handle, 0.6, 0.2, 0))
local contacts = Physics.AssemblyMetric(handle, "contacts")
local wheel = Physics.AssemblyEntity(handle, 1)
```

Additional functions: AssemblyDestroy, AssemblySimulateRagdoll,
AssemblyFracture(handle,x,y,z), AssemblyWaterLevel(handle,level), and
AssemblyVertex(handle,index), which returns an x/y/z table. Metric accepts ready,
simulated, fractured, submerged, speed, contacts, entityCount and vertexCount.
Indices start at zero. Invalid handles and operations on another kind return a
diagnostic; they do not report a successful ignored write.

For native dynamic asset loads, put leading `# @asset /Game/Physics/Car.kbphysics`
comments in the `.native` descriptor. Lua uses leading `-- @asset ...` comments.
Both share dependency discovery/validation, including source-free cooked packs.
`kb_cli skeleton show --project . --skeleton /Game/Skeletal/Rig.kbskeleton`
lists the bone tree, asset id and compatibility signature for authoring bindings.

Destroy cleans up generated objects/constraints and restores a replaced cloth
mesh. Runtime snapshots/rollback of active assemblies are explicitly refused;
persist the authored asset and instantiation script. Advanced bodies use the
default collision layer; soft-body contact events are not mapped to rigid-body
gameplay callbacks. Windows editor/package acceptance is not a qualification for
other providers or platforms. `kb_editor_physics_assembly_package` observes all
five kinds, real cloth GPU deformation, native/Lua calls, scene reload and cook.
)guide";
}
