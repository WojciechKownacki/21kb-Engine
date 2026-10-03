// Screen-space diffuse global illumination and ambient occlusion: the accumulated results shared by the
// resolve pass (which writes them) and the deferred lighting shader (which reads them).
// s_giAccum holds rgb = incoming diffuse radiance (before the surface albedo) and a = the view depth
// of the surface it was resolved for, in the frame described by u_giPrevViewProj; s_aoAccum holds the
// ambient visibility of the same frame in r.
SAMPLER2D(s_giAccum, 8);
SAMPLER2D(s_aoAccum, 10);
uniform mat4 u_giViewProj;
uniform mat4 u_giPrevViewProj;
uniform vec4 u_giParams; // x = intensity (0 = off), y = ray length (m), z = frame index, w = hit thickness factor
uniform vec4 u_giTemporal; // x = history weight, y = relative depth tolerance, z = 1 when s_giAccum holds a frame
uniform vec4 u_aoParams;   // x = intensity (0 = off), y = radius (m)

float KbGiHash(vec2 p, float seed)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233)) + seed * 37.719) * 43758.5453);
}

vec2 KbGiUv(vec4 clip)
{
    vec2 ndc = clip.xy / clip.w;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

// Accumulated bounce light and ambient visibility at a world position. Returns 1 when the stored sample
// belongs to the same surface; otherwise gi is zero and ao is 1 (unoccluded).
float KbScreenSpaceAccumulated(vec3 worldPos, out vec3 gi, out float ao)
{
    gi = vec3(0.0, 0.0, 0.0);
    ao = 1.0;
    vec4 clip = mul(u_giPrevViewProj, vec4(worldPos, 1.0));
    if (u_giTemporal.z < 0.5 || clip.w <= 0.0001) {
        return 0.0;
    }
    vec2 uv = KbGiUv(clip);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 0.0;
    }
    vec4 stored = texture2DLod(s_giAccum, uv, 0.0);
    float same = abs(stored.a - clip.w) < u_giTemporal.y * clip.w ? 1.0 : 0.0;
    gi = stored.rgb * same;
    ao = mix(1.0, texture2DLod(s_aoAccum, uv, 0.0).x, same);
    return same;
}
