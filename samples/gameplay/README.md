# Gameplay samples

The five gameplay sample profiles are compiled with the engine in
`engine/gameplay/GameplaySamples.hpp`. `kb_cli init-agent --project <path>`
also writes their runnable Lua behaviours to `Assets/Samples/`; they load in
Play Mode through the normal asset registry and script runtime, rather than a
sample-only runtime system.

- `ThirdPersonController.lua`: character-relative movement and jump; requires CharacterController, Move and Jump input.
- `TopDownController.lua`: screen-plane movement; requires Transform and Move input.
- `PlatformerController.lua`: side-scroll movement and jump; requires CharacterController, Move and Jump input.
- `SimpleShooterController.lua`: character-relative movement and projectile spawning; requires CharacterController, Move, Fire and the shipped Projectile prefab.
- `AudioShooterDemo.21kbscene`: a ready-to-run forward-flight scene. Space fires visible cube projectiles and a spatial one-shot; the ship demonstrates a non-spatial looping source, while the beacon demonstrates spatial attenuation. Its scripts, input assets, prefab, mesh and generated WAV clips live in `Assets/Samples/AudioShooter/`.

Attach a sample to a scene entity, bind the named input actions, save/reopen
the scene and enter Play Mode. `AudioShooterDemo.21kbscene` is already wired and
can be opened directly after running `kb_cli init-agent`. Camera policy remains
configured by the matching `GameplaySampleProfile`; the behaviours deliberately
own only gameplay input.
