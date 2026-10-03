// Shared by the GPU particle emitter kernels (closed form and colliding): the appearance curves and the
// instance record consumed by vs_particle_instanced. Include after bgfx_compute.sh.
BUFFER_WO(instanceOut, vec4, 1);

uniform vec4 u_gpuParticleMotion; // xyz = acceleration, w = linear drag
uniform vec4 u_gpuParticleTime;   // x = now, y = slot count, z = stretch velocity scale, w = minimum stretch
uniform vec4 u_gpuParticleColor[8];
uniform vec4 u_gpuParticleSize[2]; // eight sizes over normalized age
// x = output mode (0 billboard, 1 mesh instance, 2 trail segments), y = instances written per slot,
// z = trail segment seconds, w = trail width.
uniform vec4 u_gpuParticleOutput;
// Spin ranges: x, y = initial angle min, max; z, w = angular velocity min, max (radians, radians per second).
uniform vec4 u_gpuParticleSpin;
// Orientation of mesh particles: the columns of a rotation matrix.
uniform vec4 u_gpuParticleBasis[3];

uint PcgHash(uint value)
{
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Hash01(uint seed)
{
    return float(PcgHash(seed) & 16777215u) / 16777216.0;
}

// The angle of a particle: a random start and a random angular velocity (both drawn once from the slot and the
// birth time, so they never change over the particle's life), turned by the age.
float ParticleSpin(uint slot, float birth, float age)
{
    uint seed = slot * 9781u + uint(max(birth, 0.0) * 1000.0) * 6271u;
    float angle = mix(u_gpuParticleSpin.x, u_gpuParticleSpin.y, Hash01(seed));
    float rate = mix(u_gpuParticleSpin.z, u_gpuParticleSpin.w, Hash01(seed ^ 2654435769u));
    return angle + rate * age;
}

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

// A mesh particle: a uniformly scaled, unrotated instance in the layout of the mesh pipeline's instance
// buffer (model columns with the material data lanes in w, then the colour).
void WriteMeshInstance(uint base, uint slot, vec3 position, float u, float angle)
{
    float size = SampleSize(u);
    float random = frac(sin(float(slot) * 12.9898) * 43758.5453);
    float cosine = cos(angle);
    float sine = sin(angle);
    // orientation = basis x spin about the local Z axis; the columns of the model are the turned axes
    vec3 axisX = (u_gpuParticleBasis[0].xyz * cosine + u_gpuParticleBasis[1].xyz * sine) * size;
    vec3 axisY = (u_gpuParticleBasis[1].xyz * cosine - u_gpuParticleBasis[0].xyz * sine) * size;
    vec3 axisZ = u_gpuParticleBasis[2].xyz * size;
    instanceOut[base] = vec4(axisX, random);
    instanceOut[base + 1u] = vec4(axisY, 0.0);
    instanceOut[base + 2u] = vec4(axisZ, 1.0);
    instanceOut[base + 3u] = vec4(position, 0.0);
    instanceOut[base + 4u] = SampleColor(u);
}

void WriteLiveInstance(uint base, vec3 position, vec3 velocity, float u, float angle)
{
    if (u_gpuParticleOutput.x > 0.5)
    {
        WriteMeshInstance(base, base / 5u, position, u, angle);
        return;
    }
    float speed = length(velocity);
    instanceOut[base] = vec4(position, SampleSize(u));
    instanceOut[base + 1u] = vec4(position - velocity * 0.016666668, angle);
    instanceOut[base + 2u] = vec4(velocity, max(u_gpuParticleTime.w, speed * u_gpuParticleTime.z));
    instanceOut[base + 3u] = SampleColor(u);
    instanceOut[base + 4u] = vec4(0.0, u, 0.0, 0.0);
}
