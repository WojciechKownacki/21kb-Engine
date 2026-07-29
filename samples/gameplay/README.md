# Gameplay samples

The four gameplay sample profiles are compiled with the engine in
`engine/gameplay/GameplaySamples.hpp`. `kb_cli init-agent --project <path>`
also writes their runnable Lua behaviours to `Assets/Samples/`; they load in
Play Mode through the normal asset registry and script runtime, rather than a
sample-only runtime system.

- `ThirdPersonController.lua`: character-relative movement and jump; requires CharacterController, Move and Jump input.
- `TopDownController.lua`: screen-plane movement; requires Transform and Move input.
- `PlatformerController.lua`: side-scroll movement and jump; requires CharacterController, Move and Jump input.
- `SimpleShooterController.lua`: character-relative movement and projectile spawning; requires CharacterController, Move, Fire and the shipped Projectile prefab.

Attach a sample to a scene entity, bind the named input actions, save/reopen
the scene and enter Play Mode. Camera policy remains configured by the matching
`GameplaySampleProfile`; the behaviours deliberately own only gameplay input.
