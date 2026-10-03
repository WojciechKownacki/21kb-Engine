#include "bgfx_compute.sh"

// GPU-simulated particle emitter. Every ring slot holds the birth record of one particle
// (world position + birth time, velocity + lifetime). Motion has a closed form (constant
// acceleration with linear drag), so each frame the kernel only re-evaluates the slot at the
// current time and writes the instance consumed by vs_particle_instanced.
BUFFER_RO(spawnRecords, vec4, 0);
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

NUM_THREADS(64, 1, 1)
void main()
{
    uint slot = gl_GlobalInvocationID.x;
    if (slot >= uint(u_gpuParticleTime.y))
    {
        return;
    }

    vec4 start = spawnRecords[slot * 2u];
    vec4 motion = spawnRecords[slot * 2u + 1u];
    float age = u_gpuParticleTime.x - start.w;
    float lifetime = motion.w;
    uint base = slot * 5u;
    if (lifetime <= 0.0 || age < 0.0 || age >= lifetime)
    {
        // Dead or unborn slot: a zero-sized instance collapses to a point and is clipped.
        instanceOut[base] = vec4(0.0, 0.0, 0.0, 0.0);
        instanceOut[base + 1u] = vec4(0.0, 0.0, 0.0, 0.0);
        instanceOut[base + 2u] = vec4(0.0, 0.0, 0.0, 0.0);
        instanceOut[base + 3u] = vec4(0.0, 0.0, 0.0, 0.0);
        instanceOut[base + 4u] = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    vec3 accel = u_gpuParticleMotion.xyz;
    float drag = u_gpuParticleMotion.w;
    vec3 position;
    vec3 velocity;
    if (drag > 0.0001)
    {
        vec3 terminal = accel / drag;
        float decay = exp(-drag * age);
        velocity = (motion.xyz - terminal) * decay + terminal;
        position = start.xyz + terminal * age + (motion.xyz - terminal) * ((1.0 - decay) / drag);
    }
    else
    {
        velocity = motion.xyz + accel * age;
        position = start.xyz + motion.xyz * age + accel * (0.5 * age * age);
    }

    float u = age / lifetime;
    float speed = length(velocity);
    instanceOut[base] = vec4(position, SampleSize(u));
    instanceOut[base + 1u] = vec4(position - velocity * 0.016666668, 0.0);
    instanceOut[base + 2u] = vec4(velocity, max(u_gpuParticleTime.w, speed * u_gpuParticleTime.z));
    instanceOut[base + 3u] = SampleColor(u);
    instanceOut[base + 4u] = vec4(0.0, u, 0.0, 0.0);
}
