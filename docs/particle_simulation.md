# Particle simulation

The particle provider selects simulation per emitter. `GpuVisualPreferred`
allows CPU simulation when an emitter needs behavior the GPU kernel cannot
preserve. CPU particles still use the renderer's particle draw path.

Collision planes with `killOnContact` use CPU simulation: particles end their
lifetime at contact and disappear from subsequent render snapshots. Bouncing
planes can use GPU simulation. An authored plane never implicitly enables
screen-depth collision; the renderer's explicit depth-collision parameter is
independent of the plane.

The GPU parameter block represents one Noise, AxisRotation and Attraction
module each. Emitters with multiple enabled modules of any of these types use
CPU simulation, preserving every module and its authored order. Disabled
modules do not affect eligibility.

Run `kb_21kb_particle_cpu_backend_tests` for collision, field and routing
regressions. On Windows, `kb_renderer_tests particle-collision-fallback`
loads an authored effect through the particle provider and compares hidden
D3D11 readback against CPU simulation and a free-flight control. This check
also runs in the renderer's particle mesh suite.
