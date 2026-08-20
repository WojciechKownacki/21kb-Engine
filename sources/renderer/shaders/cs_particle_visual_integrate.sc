#include "bgfx_compute.sh"

BUFFER_RO(particleVisualSource, vec4, 0);
BUFFER_RO(particleVisualPositionCurrent, vec4, 1);
BUFFER_RO(particleVisualVelocityCurrent, vec4, 2);
BUFFER_WO(particleVisualPositionNext, vec4, 3);
BUFFER_WO(particleVisualVelocityNext, vec4, 4);
BUFFER_WO(particleVisualOutput, vec4, 5);
BUFFER_RW(particleVisualCounters, uint, 6);
BUFFER_RW(particleVisualIndirect, uint, 7);
BUFFER_RO(particleVisualMask, uint, 8);

uniform vec4 u_particleVisualParams;

NUM_THREADS(64, 1, 1)
void main()
{
    uint particleIndex = gl_GlobalInvocationID.x;
    uint particleCount = uint(u_particleVisualParams.x);
    if (particleIndex >= particleCount)
    {
        return;
    }

    uint base = particleIndex * 5u;
    vec4 currentPositionSize = particleVisualSource[base];
    vec4 previousPositionRotation = particleVisualSource[base + 1u];
    vec4 velocityStretch = particleVisualSource[base + 2u];
    vec4 priorVisualPositionSize = particleVisualPositionCurrent[particleIndex];
    vec4 priorVisualVelocityStretch = particleVisualVelocityCurrent[particleIndex];
    if (particleVisualMask[particleIndex] == 0u)
    {
        particleVisualOutput[base] = currentPositionSize;
        particleVisualOutput[base + 1u] = previousPositionRotation;
        particleVisualOutput[base + 2u] = velocityStretch;
        particleVisualOutput[base + 3u] = particleVisualSource[base + 3u];
        particleVisualOutput[base + 4u] = particleVisualSource[base + 4u];
        return;
    }
    vec3 integratedPosition = u_particleVisualParams.z > 0.5
        ? priorVisualPositionSize.xyz + priorVisualVelocityStretch.xyz * u_particleVisualParams.y
        : previousPositionRotation.xyz + velocityStretch.xyz * u_particleVisualParams.y;
    float correction = clamp(length(currentPositionSize.xyz - integratedPosition) * 16.0, 0.0, 1.0);
    vec4 integratedPositionSize = vec4(mix(integratedPosition, currentPositionSize.xyz, correction), currentPositionSize.w);
    particleVisualPositionNext[particleIndex] = integratedPositionSize;
    particleVisualVelocityNext[particleIndex] = velocityStretch;
    particleVisualOutput[base] = integratedPositionSize;
    particleVisualOutput[base + 1u] = previousPositionRotation;
    particleVisualOutput[base + 2u] = velocityStretch;
    particleVisualOutput[base + 3u] = particleVisualSource[base + 3u];
    particleVisualOutput[base + 4u] = particleVisualSource[base + 4u];
    atomicAdd(particleVisualCounters[0], 1u);
    particleVisualIndirect[0] = particleCount;
}
