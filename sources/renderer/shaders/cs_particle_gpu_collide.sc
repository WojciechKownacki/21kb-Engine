#include "bgfx_compute.sh"
#include "gbuffer_position.sh"

// GPU-simulated particle emitter with collisions. Bouncing makes the motion depend on its history, so
// unlike cs_particle_gpu_emit this kernel keeps a state record per ring slot (position + last time,
// velocity + birth stamp) and integrates the elapsed time every frame in substeps, with the same
// order of operations as the CPU backend (acceleration, drag, move, then the planes), followed by a
// bounce off the surface the scene depth buffer shows at the particle's screen position.
BUFFER_RO(spawnRecords, vec4, 0);
BUFFER_RW(stateRecords, vec4, 2);
#include "particle_gpu_common.sh"

SAMPLER2D(s_particleDepth, 3);
uniform vec4 u_gpuParticlePlane[2];   // [0] = normal.xyz, distance; [1] = restitution, friction
uniform vec4 u_gpuParticleCollision;  // x = 1 when the plane collides, y = 1 when the depth buffer collides, z = depth thickness (m)
uniform vec4 u_gpuParticleDepthBounce; // x = restitution, y = friction of a depth-buffer bounce
uniform vec4 u_gpuParticleTexel;      // xy = 1 / depth texture size
uniform mat4 u_gpuParticleViewProj;

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
    vec2 uv = vec2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
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
    if (velocityState.w != stamp)
    {
        // First visit of a particle born into this slot.
        position = start.xyz;
        velocity = motion.xyz;
        lastTime = start.w;
    }

    float elapsed = clamp(now - lastTime, 0.0, 0.25);
    int steps = int(clamp(ceil(elapsed * 60.0), 1.0, 8.0));
    float h = elapsed / float(steps);
    vec3 accel = u_gpuParticleMotion.xyz;
    float drag = exp(-u_gpuParticleMotion.w * h);
    for (int i = 0; i < 8; ++i)
    {
        if (i >= steps || elapsed <= 0.0)
        {
            break;
        }
        velocity += accel * h;
        velocity *= drag;
        position += velocity * h;
        if (u_gpuParticleCollision.x > 0.5)
        {
            vec4 plane = u_gpuParticlePlane[0];
            vec4 response = u_gpuParticlePlane[1];
            float signedDistance = dot(plane.xyz, position) - plane.w;
            if (signedDistance < 0.0)
            {
                position -= plane.xyz * signedDistance;
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

    stateRecords[slot * 2u] = vec4(position, now);
    stateRecords[slot * 2u + 1u] = vec4(velocity, stamp);
    WriteLiveInstance(base, position, velocity, age / lifetime);
}
