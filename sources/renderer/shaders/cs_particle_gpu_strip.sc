#include "bgfx_compute.sh"

// Builds the strip of a GPU ribbon or beam emitter: one thread per segment, four vertices per segment
// (the two ends, each pushed to both sides across the camera), in the vertex layout of vs_particle_strip
// (position, colour, texture coordinate; one vec4 each). The indices are the fixed pattern of a quad per segment.
//   ribbon: the segment joins ring slot s to the next slot; the slot records are those of cs_particle_gpu_emit
//           (position and live flag in the first vec4, colour in the fourth, normalized age in the fifth).
//   beam:   the segment joins two points of a straight line pushed across itself by a sine of the clock.
BUFFER_RO(slotRecords, vec4, 0);
BUFFER_WO(stripOut, vec4, 1);

// x = 0 ribbon / 1 beam, y = segment count (slots of a ribbon), z = width, w = the ring slot the next birth goes to.
uniform vec4 u_gpuStripMode;
// [0] = beam start (xyz), intensity (w); [1] = beam end (xyz), noise amplitude (w); [2].x = noise frequency,
// [2].y = scene clock in fixed steps.
uniform vec4 u_gpuStripBeam[3];
uniform vec4 u_gpuStripCamera[2]; // [0] = forward, [1] = right

vec3 NormalizeOr(vec3 value, vec3 fallback)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 0.0000001 ? value * (1.0 / sqrt(lengthSquared)) : fallback;
}

uint RingNext(uint slot, uint count)
{
    return slot + 1u == count ? 0u : slot + 1u;
}

uint RingPrevious(uint slot, uint count)
{
    return slot == 0u ? count - 1u : slot - 1u;
}

bool SlotAlive(uint slot)
{
    return slotRecords[slot * 5u].w > 0.0;
}

// The segment from `slot` to the next one exists: both are alive and the next is not the oldest of the ring.
bool SegmentLinked(uint slot, uint count, uint head)
{
    uint next = RingNext(slot, count);
    return next != head && SlotAlive(slot) && SlotAlive(next);
}

vec3 BeamPoint(uint pointIndex)
{
    float segments = u_gpuStripMode.y;
    float t = float(pointIndex) / segments;
    vec3 start = u_gpuStripBeam[0].xyz;
    vec3 end = u_gpuStripBeam[1].xyz;
    vec3 axis = NormalizeOr(end - start, vec3(0.0, 1.0, 0.0));
    vec3 worldUp = vec3(0.0, 1.0, 0.0);
    vec3 across = NormalizeOr(worldUp - axis * dot(worldUp, axis),
        NormalizeOr(vec3(1.0, 0.0, 0.0) - axis * axis.x, vec3(0.0, 0.0, 1.0)));
    float phase = u_gpuStripBeam[2].y * u_gpuStripBeam[2].x + t * 31.0;
    float envelope = sin(t * 3.14159265);
    return start + (end - start) * t + across * (sin(phase) * u_gpuStripBeam[1].w * envelope);
}

void WriteVertex(uint index, vec3 position, vec4 color, float u, float v)
{
    stripOut[index * 3u] = vec4(position, 1.0);
    stripOut[index * 3u + 1u] = color;
    stripOut[index * 3u + 2u] = vec4(u, v, 0.0, 0.0);
}

NUM_THREADS(64, 1, 1)
void main()
{
    uint segment = gl_GlobalInvocationID.x;
    uint count = uint(u_gpuStripMode.y);
    if (segment >= count)
    {
        return;
    }
    uint base = segment * 4u;
    vec3 head = vec3(0.0, 0.0, 0.0);
    vec3 tail = vec3(0.0, 0.0, 0.0);
    vec3 before = vec3(0.0, 0.0, 0.0);
    vec3 after = vec3(0.0, 0.0, 0.0);
    vec4 headColor = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 tailColor = vec4(0.0, 0.0, 0.0, 0.0);
    float headU = 0.0;
    float tailU = 0.0;
    if (u_gpuStripMode.x > 0.5)
    {
        // Beam: points `segment` and `segment + 1`, their neighbours (an end uses itself).
        head = BeamPoint(segment);
        tail = BeamPoint(segment + 1u);
        before = BeamPoint(segment == 0u ? segment : segment - 1u);
        after = BeamPoint(segment + 2u <= count ? segment + 2u : segment + 1u);
        headColor = vec4(u_gpuStripBeam[0].www, 1.0);
        tailColor = headColor;
        headU = float(segment) / float(count);
        tailU = float(segment + 1u) / float(count);
    }
    else
    {
        uint ringHead = uint(u_gpuStripMode.w);
        if (!SegmentLinked(segment, count, ringHead))
        {
            // A dead or unlinked slot collapses its quad to a point, which is not drawn.
            for (uint corner = 0u; corner < 4u; ++corner)
            {
                WriteVertex(base + corner, vec3(0.0, 0.0, 0.0), vec4(0.0, 0.0, 0.0, 0.0), 0.0, 0.0);
            }
            return;
        }
        uint next = RingNext(segment, count);
        uint previous = RingPrevious(segment, count);
        head = slotRecords[segment * 5u].xyz;
        tail = slotRecords[next * 5u].xyz;
        before = SegmentLinked(previous, count, ringHead) ? slotRecords[previous * 5u].xyz : head;
        after = SegmentLinked(next, count, ringHead) ? slotRecords[RingNext(next, count) * 5u].xyz : tail;
        headColor = slotRecords[segment * 5u + 3u];
        tailColor = slotRecords[next * 5u + 3u];
        // Along the strip the texture runs from the oldest particle (0) to the newest (1).
        headU = 1.0 - slotRecords[segment * 5u + 4u].y;
        tailU = 1.0 - slotRecords[next * 5u + 4u].y;
    }
    vec3 forward = NormalizeOr(u_gpuStripCamera[0].xyz, vec3(0.0, 0.0, 1.0));
    vec3 right = NormalizeOr(u_gpuStripCamera[1].xyz, vec3(1.0, 0.0, 0.0));
    float halfWidth = u_gpuStripMode.z * 0.5;
    vec3 headSide = NormalizeOr(cross(forward, NormalizeOr(tail - before, vec3(0.0, 1.0, 0.0))), right) * halfWidth;
    vec3 tailSide = NormalizeOr(cross(forward, NormalizeOr(after - head, vec3(0.0, 1.0, 0.0))), right) * halfWidth;
    WriteVertex(base, head - headSide, headColor, headU, 0.0);
    WriteVertex(base + 1u, head + headSide, headColor, headU, 1.0);
    WriteVertex(base + 2u, tail - tailSide, tailColor, tailU, 0.0);
    WriteVertex(base + 3u, tail + tailSide, tailColor, tailU, 1.0);
}
