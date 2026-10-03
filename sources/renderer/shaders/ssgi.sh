// Screen-space diffuse global illumination: the accumulated bounce light shared by the resolve pass
// (which writes it) and the deferred lighting shader (which reads it).
// s_giAccum holds rgb = incoming diffuse radiance (before the surface albedo) and a = the view depth
// of the surface it was resolved for, in the frame described by u_giPrevViewProj.
SAMPLER2D(s_giAccum, 8);
uniform mat4 u_giPrevViewProj;
uniform vec4 u_giParams; // x = intensity (0 = off), y = ray length (m), z = frame index, w = hit thickness factor
uniform vec4 u_giTemporal; // x = history weight, y = relative depth tolerance, z = 1 when s_giAccum holds a frame

vec2 KbGiUv(vec4 clip)
{
    vec2 ndc = clip.xy / clip.w;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

// Accumulated bounce light at a world position; w is 1 when the stored sample belongs to the same surface.
vec4 KbGiAccumulated(vec3 worldPos)
{
    vec4 clip = mul(u_giPrevViewProj, vec4(worldPos, 1.0));
    if (u_giTemporal.z < 0.5 || clip.w <= 0.0001) {
        return vec4(0.0, 0.0, 0.0, 0.0);
    }
    vec2 uv = KbGiUv(clip);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4(0.0, 0.0, 0.0, 0.0);
    }
    vec4 stored = texture2DLod(s_giAccum, uv, 0.0);
    float same = abs(stored.a - clip.w) < u_giTemporal.y * clip.w ? 1.0 : 0.0;
    return vec4(stored.rgb * same, same);
}
