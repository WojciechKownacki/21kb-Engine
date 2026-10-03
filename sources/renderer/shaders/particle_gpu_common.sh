// Shared by the GPU particle emitter kernels (closed form and colliding): the appearance curves and the
// instance record consumed by vs_particle_instanced. Include after bgfx_compute.sh.
BUFFER_WO(instanceOut, vec4, 1);

uniform vec4 u_gpuParticleMotion; // xyz = acceleration, w = linear drag
uniform vec4 u_gpuParticleTime;   // x = now, y = slot count, z = stretch velocity scale, w = minimum stretch
uniform vec4 u_gpuParticleColor[8];
uniform vec4 u_gpuParticleSize[2]; // eight sizes over normalized age
// x = output mode (0 billboard, 1 mesh instance, 2 trail segments), y = instances written per slot,
// z = trail segment seconds (trail) or 1 when a mesh particle's Y axis follows its velocity, w = trail width.
uniform vec4 u_gpuParticleOutput;
// Spin ranges per axis (xyz): [0] initial angle min, [1] max, [2] angular velocity min, [3] max
// (radians, radians per second).
uniform vec4 u_gpuParticleSpin[4];
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

// The angles of a particle about X, Y and Z: a random start and a random angular velocity per axis (all drawn once
// from the slot and the birth time, so they never change over the particle's life), turned by the age.
vec3 ParticleSpin(uint slot, float birth, float age)
{
    uint seed = slot * 9781u + uint(max(birth, 0.0) * 1000.0) * 6271u;
    vec3 randomAngle = vec3(Hash01(seed ^ 2654435769u), Hash01(seed ^ 1640531527u), Hash01(seed ^ 3294967296u + 7u));
    vec3 randomRate = vec3(Hash01(seed ^ 40503u), Hash01(seed ^ 69069u), Hash01(seed ^ 1812433253u));
    vec3 angle = mix(u_gpuParticleSpin[0].xyz, u_gpuParticleSpin[1].xyz, randomAngle);
    vec3 rate = mix(u_gpuParticleSpin[2].xyz, u_gpuParticleSpin[3].xyz, randomRate);
    return angle + rate * age;
}

vec3 RotateAboutX(vec3 v, float a)
{
    float c = cos(a);
    float s = sin(a);
    return vec3(v.x, c * v.y - s * v.z, s * v.y + c * v.z);
}

vec3 RotateAboutY(vec3 v, float a)
{
    float c = cos(a);
    float s = sin(a);
    return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

vec3 RotateAboutZ(vec3 v, float a)
{
    float c = cos(a);
    float s = sin(a);
    return vec3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}

// Euler turn: about X first, then Y, then Z.
vec3 EulerTurn(vec3 v, vec3 angles)
{
    return RotateAboutZ(RotateAboutY(RotateAboutX(v, angles.x), angles.y), angles.z);
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
void WriteMeshInstance(uint base, uint slot, vec3 position, vec3 velocity, float u, vec3 angles)
{
    float size = SampleSize(u);
    float random = frac(sin(float(slot) * 12.9898) * 43758.5453);
    // The frame the Euler turn is expressed in: the emitter's basis, or - when the mesh follows its velocity and
    // there is one - a frame whose Y axis is the velocity (X horizontal-ish, from the world up).
    vec3 frame0 = u_gpuParticleBasis[0].xyz;
    vec3 frame1 = u_gpuParticleBasis[1].xyz;
    vec3 frame2 = u_gpuParticleBasis[2].xyz;
    float speed = length(velocity);
    if (u_gpuParticleOutput.z > 0.5 && speed > 0.00001)
    {
        vec3 forward = velocity / speed;
        vec3 helper = abs(forward.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
        frame0 = normalize(cross(helper, forward));
        frame1 = forward;
        frame2 = cross(frame0, frame1);
    }
    // orientation = basis x Euler turn; the columns of the model are the turned axes, expressed in the world by the basis
    vec3 localX = EulerTurn(vec3(1.0, 0.0, 0.0), angles);
    vec3 localY = EulerTurn(vec3(0.0, 1.0, 0.0), angles);
    vec3 localZ = EulerTurn(vec3(0.0, 0.0, 1.0), angles);
    vec3 axisX = (frame0 * localX.x + frame1 * localX.y + frame2 * localX.z) * size;
    vec3 axisY = (frame0 * localY.x + frame1 * localY.y + frame2 * localY.z) * size;
    vec3 axisZ = (frame0 * localZ.x + frame1 * localZ.y + frame2 * localZ.z) * size;
    instanceOut[base] = vec4(axisX, random);
    instanceOut[base + 1u] = vec4(axisY, 0.0);
    instanceOut[base + 2u] = vec4(axisZ, 1.0);
    instanceOut[base + 3u] = vec4(position, 0.0);
    instanceOut[base + 4u] = SampleColor(u);
}

void WriteLiveInstance(uint base, vec3 position, vec3 velocity, float u, vec3 angles)
{
    if (u_gpuParticleOutput.x > 0.5)
    {
        WriteMeshInstance(base, base / 5u, position, velocity, u, angles);
        return;
    }
    float speed = length(velocity);
    instanceOut[base] = vec4(position, SampleSize(u));
    instanceOut[base + 1u] = vec4(position - velocity * 0.016666668, angles.z);
    instanceOut[base + 2u] = vec4(velocity, max(u_gpuParticleTime.w, speed * u_gpuParticleTime.z));
    instanceOut[base + 3u] = SampleColor(u);
    instanceOut[base + 4u] = vec4(0.0, u, 0.0, 0.0);
}
