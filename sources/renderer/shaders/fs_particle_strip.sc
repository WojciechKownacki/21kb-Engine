$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_particleAtlas, 0);
uniform vec4 u_particleFeatureParams;

// The strip texture modulated by the point colour (HDR, alpha): additive output is premultiplied,
// alpha output straight, as the sprites of the same emitter.
void main()
{
    vec4 tex = texture2D(s_particleAtlas, v_texcoord0);
    float alpha = tex.a * clamp(v_color0.a, 0.0, 1.0);
    if (alpha < 0.002) {
        discard;
    }
    vec3 rgb = tex.rgb * v_color0.rgb;
    bool additive = u_particleFeatureParams.z > 0.5;
    gl_FragColor = additive ? vec4(rgb * alpha, alpha) : vec4(rgb, alpha);
}
