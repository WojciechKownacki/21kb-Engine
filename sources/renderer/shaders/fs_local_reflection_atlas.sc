$input v_texcoord0

// Copies one roughness level of a probe's cube map into its tile of the local reflection atlas
// (SceneLocalReflectionAtlas) as an octahedral map. The one texel border around the tile is filled with the
// texels the map continues with across its edge, so the lighting's bilinear reads need no special case there.

#include <bgfx_shader.sh>

SAMPLERCUBE(s_localReflectionSource, 0);
// x source cube mip, y tile size in texels without its border, z border in texels.
uniform vec4 u_localReflectionBuild;

// The direction of an octahedral map position (-1..1 across the map; the border lies just outside).
vec3 KbOctahedralDecode(vec2 e)
{
    if (abs(e.x) > 1.0) {
        e.x = (e.x > 0.0 ? 1.0 : -1.0) * (2.0 - abs(e.x));
        e.y = -e.y;
    }
    if (abs(e.y) > 1.0) {
        e.y = (e.y > 0.0 ? 1.0 : -1.0) * (2.0 - abs(e.y));
        e.x = -e.x;
    }
    vec3 d = vec3(e.x,e.y,1.0 - abs(e.x) - abs(e.y));
    if (d.z < 0.0) {
        vec2 side = vec2(d.x >= 0.0 ? 1.0 : -1.0,d.y >= 0.0 ? 1.0 : -1.0);
        d.xy = (vec2(1.0,1.0) - abs(d.yx)) * side;
    }
    return normalize(d);
}

void main()
{
    float size = u_localReflectionBuild.y;
    float border = u_localReflectionBuild.z;
    vec2 inside = v_texcoord0 * (size + 2.0 * border) - vec2_splat(border);
    vec3 direction = KbOctahedralDecode(inside / size * 2.0 - vec2(1.0,1.0));
    gl_FragColor = vec4(textureCubeLod(s_localReflectionSource,direction,u_localReflectionBuild.x).rgb,1.0);
}
