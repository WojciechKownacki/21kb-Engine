$input v_texcoord0

#include <bgfx_shader.sh>

#include "present_display.sh"

// The scene composite of an image rendered below the output resolution. The four source texels around
// the output pixel are weighted bilinearly, but a texel whose brightness differs from the nearest one
// loses weight, so edges stay crisp instead of spreading over the larger output pixels. A light
// sharpening against the four direct neighbours follows, clamped to the texels' own range so it never rings.

float upscale_luma(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 upscale_edge_aware(vec2 uv, out vec3 low, out vec3 high)
{
    vec2 texel = u_displayAntiAliasing.yz;
    vec2 position = uv / texel - vec2_splat(0.5);
    vec2 base = floor(position);
    vec2 f = position - base;
    vec2 uv00 = (base + vec2_splat(0.5)) * texel;
    vec2 uv10 = uv00 + vec2(texel.x, 0.0);
    vec2 uv01 = uv00 + vec2(0.0, texel.y);
    vec2 uv11 = uv00 + texel;
    vec3 c00 = display_transform(texture2D(s_kbFullscreenColor, uv00).rgb, uv00);
    vec3 c10 = display_transform(texture2D(s_kbFullscreenColor, uv10).rgb, uv10);
    vec3 c01 = display_transform(texture2D(s_kbFullscreenColor, uv01).rgb, uv01);
    vec3 c11 = display_transform(texture2D(s_kbFullscreenColor, uv11).rgb, uv11);
    vec4 weights = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    vec4 lumas = vec4(upscale_luma(c00), upscale_luma(c10), upscale_luma(c01), upscale_luma(c11));
    float nearest = f.x < 0.5 ? (f.y < 0.5 ? lumas.x : lumas.z) : (f.y < 0.5 ? lumas.y : lumas.w);
    weights *= vec4_splat(1.0) / (vec4_splat(1.0) + 8.0 * abs(lumas - vec4_splat(nearest)));
    low = min(min(c00, c10), min(c01, c11));
    high = max(max(c00, c10), max(c01, c11));
    return (c00 * weights.x + c10 * weights.y + c01 * weights.z + c11 * weights.w) /
        max(weights.x + weights.y + weights.z + weights.w, 0.0001);
}

void main()
{
    vec4 scene = texture2D(s_kbFullscreenColor, v_texcoord0);
    vec3 low;
    vec3 high;
    vec3 display = upscale_edge_aware(v_texcoord0, low, high);
    if (u_displayAntiAliasing.x > 0.5) {
        display = fxaa_display(v_texcoord0);
    }
    vec2 texel = u_displayAntiAliasing.yz;
    vec3 neighbours = 0.25 * (
        display_sample(v_texcoord0 + vec2(texel.x, 0.0)) + display_sample(v_texcoord0 - vec2(texel.x, 0.0)) +
        display_sample(v_texcoord0 + vec2(0.0, texel.y)) + display_sample(v_texcoord0 - vec2(0.0, texel.y)));
    display = clamp(display + u_displayAntiAliasing.w * (display - neighbours), low, high);
    // Quantisation dither, as the plain present.
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    display += vec3_splat((noise - 0.5) / 255.0);
    gl_FragColor = vec4(display, mix(scene.a, 1.0, u_tonemapParams.w));
}
