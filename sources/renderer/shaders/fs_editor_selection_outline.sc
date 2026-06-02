$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_selectionMask, 0);
uniform vec4 u_outlineParams;

float mask_at(vec2 uv)
{
    return texture2D(s_selectionMask, uv).a;
}

void main()
{
    vec2 texel = u_outlineParams.xy;
    float center = mask_at(v_texcoord0);
    float edge =
        mask_at(v_texcoord0 + vec2(-texel.x, 0.0)) +
        mask_at(v_texcoord0 + vec2(texel.x, 0.0)) +
        mask_at(v_texcoord0 + vec2(0.0, -texel.y)) +
        mask_at(v_texcoord0 + vec2(0.0, texel.y)) +
        mask_at(v_texcoord0 + vec2(-texel.x, -texel.y)) +
        mask_at(v_texcoord0 + vec2(texel.x, -texel.y)) +
        mask_at(v_texcoord0 + vec2(-texel.x, texel.y)) +
        mask_at(v_texcoord0 + vec2(texel.x, texel.y));
    float outline = step(0.01, edge) * (1.0 - step(0.01, center));
    gl_FragColor = vec4(1.0, 0.72, 0.18, outline * 0.95);
}
