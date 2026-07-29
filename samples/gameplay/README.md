# Gameplay samples

The four gameplay sample profiles are compiled with the engine in
`engine/gameplay/GameplaySamples.hpp`. They deliberately reuse the public
gameplay contracts rather than introducing sample-only runtime systems.

- `third-person-controller`: character-relative movement, follow camera, jump.
- `top-down-controller`: screen-plane movement and follow camera.
- `platformer`: side-scroll movement, follow camera, jump.
- `simple-shooter`: character-relative movement, possess camera, combat.

Each profile is a starting configuration: attach it to the project's input
actions and scene entities, then drive movement through the existing character
controller and abilities through `GameplayAbilities`.
