#include "bgfx_compute.sh"

// GPU-simulated particle emitter. Every ring slot holds the birth record of one particle
// (world position + birth time, velocity + lifetime). Motion has a closed form (constant
// acceleration with linear drag), so each frame the kernel only re-evaluates the slot at the
// current time and writes the instance consumed by vs_particle_instanced.
BUFFER_RO(spawnRecords, vec4, 0);
#include "particle_gpu_common.sh"

// Local-space emitters: the birth records are in the owner's frame; u_gpuParticleWorld carries them to
// world space every frame. The (world-space) acceleration is expressed in that frame for the closed form.
uniform mat4 u_gpuParticleWorld;
uniform mat4 u_gpuParticleWorldInverse;
uniform vec4 u_gpuParticleLocal; // x = 1 for a local-space emitter

// Where a particle born at `start` with velocity `launch` is after `t` seconds (constant acceleration, linear drag).
vec3 ClosedFormPosition(vec3 start, vec3 launch, vec3 accel, float drag, float t)
{
    if (drag > 0.0001)
    {
        vec3 terminal = accel / drag;
        return start + terminal * t + (launch - terminal) * ((1.0 - exp(-drag * t)) / drag);
    }
    return start + launch * t + accel * (0.5 * t * t);
}

NUM_THREADS(64, 1, 1)
void main()
{
    // A trail emitter writes one instance per trail segment of every slot; the other outputs one per slot.
    uint perSlot = max(uint(u_gpuParticleOutput.y), 1u);
    uint index = gl_GlobalInvocationID.x;
    uint slot = index / perSlot;
    uint segment = index - slot * perSlot;
    if (slot >= uint(u_gpuParticleTime.y))
    {
        return;
    }

    vec4 start = spawnRecords[slot * 2u];
    vec4 motion = spawnRecords[slot * 2u + 1u];
    float age = u_gpuParticleTime.x - start.w;
    float lifetime = motion.w;
    uint base = index * 5u;
    if (lifetime <= 0.0 || age < 0.0 || age >= lifetime)
    {
        WriteDeadInstance(base);
        return;
    }

    if (u_gpuParticleOutput.x > 1.5)
    {
        // Trail segment `segment` (newest first) runs between the positions one segment-time apart.
        float newer = age - float(segment) * u_gpuParticleOutput.z;
        float older = max(newer - u_gpuParticleOutput.z, 0.0);
        vec3 trailAccel = u_gpuParticleMotion.xyz;
        vec3 head = ClosedFormPosition(start.xyz, motion.xyz, trailAccel, u_gpuParticleMotion.w, newer);
        vec3 tail = ClosedFormPosition(start.xyz, motion.xyz, trailAccel, u_gpuParticleMotion.w, older);
        vec3 along = head - tail;
        float length_ = length(along);
        if (newer <= 0.0 || length_ < 0.0001)
        {
            WriteDeadInstance(base);
            return;
        }
        float width = max(u_gpuParticleOutput.w, 0.0001);
        instanceOut[base] = vec4((head + tail) * 0.5, width);
        instanceOut[base + 1u] = vec4(0.0, 0.0, 0.0, 0.0);
        instanceOut[base + 2u] = vec4(along, length_ / width);
        instanceOut[base + 3u] = vec4(1.0, 1.0, 1.0, 1.0);
        instanceOut[base + 4u] = vec4(0.0, age / lifetime, 0.0, 0.0);
        return;
    }

    bool local = u_gpuParticleLocal.x > 0.5;
    vec3 accel = u_gpuParticleMotion.xyz;
    if (local)
    {
        accel = mul(u_gpuParticleWorldInverse, vec4(accel, 0.0)).xyz;
    }
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

    if (local)
    {
        position = mul(u_gpuParticleWorld, vec4(position, 1.0)).xyz;
        velocity = mul(u_gpuParticleWorld, vec4(velocity, 0.0)).xyz;
    }
    WriteLiveInstance(base, position, velocity, age / lifetime);
}
