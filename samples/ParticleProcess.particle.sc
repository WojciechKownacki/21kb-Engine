// Compile this file with particle-process-cook; no engine fork or runtime native module is needed.
void particle_process(inout ParticleProcess p, float dt, float timeSeconds) {
    p.custom.x += dt;
    p.position.x += 2.0 * dt;
    p.velocity.y = 3.0;
    p.color = vec4(0.25, 0.5, 0.75, 1.0);
    p.size = 0.5 + p.custom.x;
}
