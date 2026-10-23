#include "bgfx_compute.sh"
#include "gbuffer_position.sh"

// GPU-simulated particle emitter with collisions or force fields. Bouncing and the fields make the motion
// depend on its history, so unlike cs_particle_gpu_emit this kernel keeps a state record per ring slot
// (position + last time, velocity + birth stamp) and integrates the elapsed time every frame in substeps,
// with authored CPU motion order, then movement and the plane, followed by a bounce off the
// surface the scene depth buffer shows at the particle's screen
// position. Records of a local-space emitter are in the owner's frame, which u_gpuParticleWorld places.
BUFFER_RO(spawnRecords, vec4, 0);
BUFFER_RW(stateRecords, vec4, 2);
#include "particle_gpu_common.sh"

SAMPLER2D(s_particleDepth, 3);
uniform vec4 u_gpuParticlePlane[2];   // [0] = normal.xyz, distance; [1] = restitution, friction, kill on contact
uniform vec4 u_gpuParticleCollision;  // x = 1 when the plane collides, y = 1 when the depth buffer collides, z = depth thickness (m)
uniform vec4 u_gpuParticleDepthBounce; // x = restitution, y = friction of a depth-buffer bounce
uniform vec4 u_gpuParticleTexel;      // xy = 1 / depth texture size
uniform mat4 u_gpuParticleViewProj;
uniform mat4 u_gpuParticleWorld;
uniform mat4 u_gpuParticleWorldInverse;
uniform vec4 u_gpuParticleLocal;      // x = 1 for a local-space emitter
// Two vec4s per active motion module, in authored execution order (at most 16).
// First: type (1 acceleration, 2 drag, 3 noise, 4 axis rotation, 5 attraction), strength, parameter, seed low16.
// Second: world-axis vector, seed high16. parameter = noise frequency or attraction radius.
uniform vec4 u_gpuParticleForces[32];
uniform vec4 u_gpuParticleField; // xyz = emitter origin in render space, w = motion module count
uniform vec4 u_gpuParticleNoiseOrigin; // xyz = the world origin the noise position is relative to

// Gradient noise with a quintic fade and its derivative, and its curl, as the CPU backend's Noise module.
uint HashCorner(int x, int y, int z, uint seed)
{
    uint h = seed * 2654435769u;
    h = h ^ (uint(x) * 2246822507u);
    h = h ^ (uint(y) * 3266489909u);
    h = h ^ (uint(z) * 668265263u);
    h = h ^ (h >> 15u); h = h * 739982445u;
    h = h ^ (h >> 12u); h = h * 695872825u;
    h = h ^ (h >> 15u);
    return h;
}

vec3 CornerEdge(uint index)
{
    float a = (index & 1u) != 0u ? -1.0 : 1.0;
    float b = (index & 2u) != 0u ? -1.0 : 1.0;
    uint group = index / 4u;
    return group == 0u ? vec3(a, b, 0.0) : (group == 1u ? vec3(a, 0.0, b) : vec3(0.0, a, b));
}

vec3 NoiseGradient(vec3 p, uint seed)
{
    vec3 fl = floor(p);
    int ix = int(fl.x);
    int iy = int(fl.y);
    int iz = int(fl.z);
    vec3 f = p - fl;
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec3 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    vec3 g0 = CornerEdge(HashCorner(ix, iy, iz, seed) % 12u);
    vec3 g1 = CornerEdge(HashCorner(ix + 1, iy, iz, seed) % 12u);
    vec3 g2 = CornerEdge(HashCorner(ix, iy + 1, iz, seed) % 12u);
    vec3 g3 = CornerEdge(HashCorner(ix + 1, iy + 1, iz, seed) % 12u);
    vec3 g4 = CornerEdge(HashCorner(ix, iy, iz + 1, seed) % 12u);
    vec3 g5 = CornerEdge(HashCorner(ix + 1, iy, iz + 1, seed) % 12u);
    vec3 g6 = CornerEdge(HashCorner(ix, iy + 1, iz + 1, seed) % 12u);
    vec3 g7 = CornerEdge(HashCorner(ix + 1, iy + 1, iz + 1, seed) % 12u);
    float v0 = dot(g0, f);
    float v1 = dot(g1, f - vec3(1.0, 0.0, 0.0));
    float v2 = dot(g2, f - vec3(0.0, 1.0, 0.0));
    float v3 = dot(g3, f - vec3(1.0, 1.0, 0.0));
    float v4 = dot(g4, f - vec3(0.0, 0.0, 1.0));
    float v5 = dot(g5, f - vec3(1.0, 0.0, 1.0));
    float v6 = dot(g6, f - vec3(0.0, 1.0, 1.0));
    float v7 = dot(g7, f - vec3(1.0, 1.0, 1.0));
    float k1 = v1 - v0;
    float k2 = v2 - v0;
    float k3 = v4 - v0;
    float k4 = v0 - v1 - v2 + v3;
    float k5 = v0 - v2 - v4 + v6;
    float k6 = v0 - v1 - v4 + v5;
    float k7 = -v0 + v1 + v2 - v3 + v4 - v5 - v6 + v7;
    vec3 d = g0 + (g1 - g0) * u.x + (g2 - g0) * u.y + (g4 - g0) * u.z +
        (g0 - g1 - g2 + g3) * (u.x * u.y) + (g0 - g2 - g4 + g6) * (u.y * u.z) +
        (g0 - g1 - g4 + g5) * (u.z * u.x) + (g1 + g2 - g3 + g4 - g5 - g6 + g7 - g0) * (u.x * u.y * u.z);
    d.x += du.x * (k1 + u.y * k4 + u.z * k6 + u.y * u.z * k7);
    d.y += du.y * (k2 + u.z * k5 + u.x * k4 + u.z * u.x * k7);
    d.z += du.z * (k3 + u.x * k6 + u.y * k5 + u.x * u.y * k7);
    return d;
}

vec3 CurlNoise(vec3 p, uint seed)
{
    vec3 p1 = NoiseGradient(p, seed);
    vec3 p2 = NoiseGradient(p + vec3(31.416, -47.853, 12.793), seed + 1u);
    vec3 p3 = NoiseGradient(p + vec3(-19.123, 33.301, -57.456), seed + 2u);
    return vec3(p3.y - p2.z, p1.z - p3.x, p2.x - p1.y);
}

// Turns v about the unit axis by angle radians (Rodrigues).
vec3 RotateAboutAxis(vec3 v, vec3 axis, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0 - c));
}

vec3 DepthWorldPosition(vec2 uv)
{
    vec2 clamped = clamp(uv, vec2(0.0, 0.0), vec2(1.0, 1.0));
    return ReconstructWorldPosition(clamped, texture2DLod(s_particleDepth, clamped, 0.0).x);
}

// Bounces a particle that moved behind the visible surface; returns true when it collided.
bool CollideWithDepth(inout vec3 position, inout vec3 velocity)
{
    vec4 clip = mul(u_gpuParticleViewProj, vec4(position, 1.0));
    if (clip.w <= 0.0001)
    {
        return false;
    }
    vec2 uv = KbNdcToScreenUv(clip.xy / clip.w);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        return false;
    }
    float depth = texture2DLod(s_particleDepth, uv, 0.0).x;
    if (depth <= 0.00001)
    {
        return false; // nothing was drawn here
    }
    vec3 surface = ReconstructWorldPosition(uv, depth);
    vec3 camera = u_deferredCameraPosition.xyz;
    float behind = distance(camera, position) - distance(camera, surface);
    if (behind <= 0.0 || behind > u_gpuParticleCollision.z)
    {
        return false;
    }
    // Surface normal from the depth neighbours, taking the smaller step on each axis to stay off edges.
    vec2 stepX = vec2(u_gpuParticleTexel.x, 0.0);
    vec2 stepY = vec2(0.0, u_gpuParticleTexel.y);
    vec3 right = DepthWorldPosition(uv + stepX) - surface;
    vec3 left = surface - DepthWorldPosition(uv - stepX);
    vec3 down = DepthWorldPosition(uv + stepY) - surface;
    vec3 up = surface - DepthWorldPosition(uv - stepY);
    vec3 tangentX = dot(right, right) < dot(left, left) ? right : left;
    vec3 tangentY = dot(down, down) < dot(up, up) ? down : up;
    vec3 normal = cross(tangentX, tangentY);
    if (dot(normal, normal) < 0.0000001)
    {
        return false;
    }
    normal = normalize(normal);
    if (dot(normal, camera - surface) < 0.0)
    {
        normal = -normal;
    }
    float normalVelocity = dot(velocity, normal);
    if (normalVelocity >= 0.0)
    {
        return false; // already leaving the surface
    }
    vec3 tangent = velocity - normal * normalVelocity;
    velocity = tangent * (1.0 - u_gpuParticleDepthBounce.y) - normal * (normalVelocity * u_gpuParticleDepthBounce.x);
    position = surface + normal * 0.01;
    return true;
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
    float now = u_gpuParticleTime.x;
    float age = now - start.w;
    float lifetime = motion.w;
    uint base = slot * 5u;
    if (lifetime <= 0.0 || age < 0.0 || age >= lifetime)
    {
        WriteDeadInstance(base);
        return;
    }

    vec4 positionState = stateRecords[slot * 2u];
    vec4 velocityState = stateRecords[slot * 2u + 1u];
    vec3 position = positionState.xyz;
    vec3 velocity = velocityState.xyz;
    float lastTime = positionState.w;
    // The stamp is offset so that a zeroed state record never matches a birth time of 0.
    float stamp = start.w + 1.0;
    if (velocityState.w == -stamp)
    {
        // Death survives later dispatches until Upload invalidates this slot for a new birth.
        WriteDeadInstance(base);
        return;
    }
    if (velocityState.w != stamp)
    {
        // First visit of a particle born into this slot.
        position = start.xyz;
        velocity = motion.xyz;
        lastTime = start.w;
    }

    float elapsed = clamp(now - lastTime, 0.0, 0.25);
    int steps = int(clamp(ceil(elapsed * 60.0 - 0.0001), 1.0, 8.0));
    float h = elapsed / float(steps);
    bool local = u_gpuParticleLocal.x > 0.5;
    if (local)
    {
        position = mul(u_gpuParticleWorld, vec4(position, 1.0)).xyz;
        velocity = mul(u_gpuParticleWorld, vec4(velocity, 0.0)).xyz;
    }
    for (int i = 0; i < 8; ++i)
    {
        if (i >= steps || elapsed <= 0.0)
        {
            break;
        }
        if (u_gpuParticleField.w < 0.5)
        {
            // Direct commands without an authored stream retain constant acceleration/drag.
            velocity = (velocity + u_gpuParticleMotion.xyz * h) * exp(-u_gpuParticleMotion.w * h);
        }
        for (int moduleIndex = 0; moduleIndex < 16; ++moduleIndex)
        {
            if (moduleIndex >= int(u_gpuParticleField.w)) break;
            vec4 op = u_gpuParticleForces[moduleIndex * 2];
            vec4 vectorSeed = u_gpuParticleForces[moduleIndex * 2 + 1];
            if (op.x >= 1.5 && op.y == 0.0) continue;
            if (op.x < 1.5)
            {
                velocity += vectorSeed.xyz * h;
            }
            else if (op.x < 2.5)
            {
                velocity *= exp(-op.y * h);
            }
            else if (op.x < 3.5)
            {
                uint seed = uint(op.w) | (uint(vectorSeed.w) << 16u);
                velocity += CurlNoise((position + u_gpuParticleNoiseOrigin.xyz) * op.z, seed) * (op.y * h);
            }
            else if (op.x < 4.5)
            {
                float angle = op.y * h;
                position = u_gpuParticleField.xyz + RotateAboutAxis(position - u_gpuParticleField.xyz, vectorSeed.xyz, angle);
                velocity = RotateAboutAxis(velocity, vectorSeed.xyz, angle);
            }
            else
            {
                vec3 to = u_gpuParticleField.xyz + vectorSeed.xyz - position;
                float away = length(to);
                bool everywhere = op.z <= 0.0;
                if (away > 0.0001 && (everywhere || away < op.z))
                {
                    velocity += to * (op.y * (everywhere ? 1.0 : 1.0 - away / op.z) / away * h);
                }
            }
        }
        position += velocity * h;
        if (u_gpuParticleCollision.x > 0.5)
        {
            vec4 plane = u_gpuParticlePlane[0];
            vec4 response = u_gpuParticlePlane[1];
            float signedDistance = dot(plane.xyz, position) - plane.w;
            if (signedDistance < 0.0)
            {
                position -= plane.xyz * signedDistance;
                if (response.z > 0.5)
                {
                    stateRecords[slot * 2u] = vec4(position, now);
                    stateRecords[slot * 2u + 1u] = vec4(velocity, -stamp);
                    WriteDeadInstance(base);
                    return;
                }
                float normalVelocity = dot(velocity, plane.xyz);
                if (normalVelocity < 0.0)
                {
                    vec3 tangent = velocity - plane.xyz * normalVelocity;
                    velocity = tangent * (1.0 - response.y) - plane.xyz * (normalVelocity * response.x);
                }
            }
        }
        if (u_gpuParticleCollision.y > 0.5)
        {
            CollideWithDepth(position, velocity);
        }
    }

    vec3 recordPosition = local ? mul(u_gpuParticleWorldInverse, vec4(position, 1.0)).xyz : position;
    vec3 recordVelocity = local ? mul(u_gpuParticleWorldInverse, vec4(velocity, 0.0)).xyz : velocity;
    stateRecords[slot * 2u] = vec4(recordPosition, now);
    stateRecords[slot * 2u + 1u] = vec4(recordVelocity, stamp);
    WriteLiveInstance(base, position, velocity, age / lifetime, ParticleSpin(slot, start.w, age));
}
