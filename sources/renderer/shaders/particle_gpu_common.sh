// Shared by the GPU particle emitter kernels (closed form and colliding): the appearance curves and the
// instance record consumed by vs_particle_instanced. Include after bgfx_compute.sh.
BUFFER_WO(instanceOut, vec4, 1);

uniform vec4 u_gpuParticleMotion; // xyz = acceleration, w = linear drag
uniform vec4 u_gpuParticleTime;   // x = now, y = slot count, z = stretch velocity scale, w = minimum stretch
uniform vec4 u_gpuParticleColor[8];
uniform vec4 u_gpuParticleSize[2]; // eight sizes over normalized age

float SampleSize(float u)
{
    float sizes[8];
    sizes[0] = u_gpuParticleSize[0].x; sizes[1] = u_gpuParticleSize[0].y;
    sizes[2] = u_gpuParticleSize[0].z; sizes[3] = u_gpuParticleSize[0].w;
    sizes[4] = u_gpuParticleSize[1].x; sizes[5] = u_gpuParticleSize[1].y;
    sizes[6] = u_gpuParticleSize[1].z; sizes[7] = u_gpuParticleSize[1].w;
    float scaled = clamp(u, 0.0, 1.0) * 7.0;
    int index = int(min(floor(scaled), 6.0));
    return mix(sizes[index], sizes[index + 1], scaled - float(index));
}

vec4 SampleColor(float u)
{
    float scaled = clamp(u, 0.0, 1.0) * 7.0;
    int index = int(min(floor(scaled), 6.0));
    return mix(u_gpuParticleColor[index], u_gpuParticleColor[index + 1], scaled - float(index));
}

// A dead or unborn slot: a zero-sized instance collapses to a point and is clipped.
void WriteDeadInstance(uint base)
{
    instanceOut[base] = vec4(0.0, 0.0, 0.0, 0.0);
    instanceOut[base + 1u] = vec4(0.0, 0.0, 0.0, 0.0);
    instanceOut[base + 2u] = vec4(0.0, 0.0, 0.0, 0.0);
    instanceOut[base + 3u] = vec4(0.0, 0.0, 0.0, 0.0);
    instanceOut[base + 4u] = vec4(0.0, 0.0, 0.0, 0.0);
}

void WriteLiveInstance(uint base, vec3 position, vec3 velocity, float u)
{
    float speed = length(velocity);
    instanceOut[base] = vec4(position, SampleSize(u));
    instanceOut[base + 1u] = vec4(position - velocity * 0.016666668, 0.0);
    instanceOut[base + 2u] = vec4(velocity, max(u_gpuParticleTime.w, speed * u_gpuParticleTime.z));
    instanceOut[base + 3u] = SampleColor(u);
    instanceOut[base + 4u] = vec4(0.0, u, 0.0, 0.0);
}
