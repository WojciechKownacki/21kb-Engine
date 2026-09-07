$input v_texcoord0, v_widgetLocal

#include <bgfx_shader.sh>

SAMPLER2D(s_widgetTexture, 0);
uniform vec4 u_widgetFillColor;
uniform vec4 u_widgetBorderColor;
uniform vec4 u_widgetBorderWidths;
uniform vec4 u_widgetCornerRadii;
uniform vec4 u_widgetStyleParams;
uniform vec4 u_widgetEffectParams;
uniform vec4 u_widgetTonemapParams;

float rounded_distance(vec2 localPosition, vec2 size, vec4 radii)
{
    bool right = localPosition.x >= size.x * 0.5;
    bool bottom = localPosition.y >= size.y * 0.5;
    float radius = bottom
        ? (right ? radii.z : radii.w)
        : (right ? radii.y : radii.x);
    radius = clamp(radius, 0.0, min(size.x, size.y) * 0.5);
    vec2 q = abs(localPosition - size * 0.5) - (size * 0.5 - vec2_splat(radius));
    return length(max(q, vec2_splat(0.0))) + min(max(q.x, q.y), 0.0) - radius;
}

float rounded_coverage(vec2 localPosition, vec2 size, vec4 radii, float feather)
{
    float distance = rounded_distance(localPosition, size, radii);
    float aa = max(fwidth(distance), 0.75);
    return 1.0 - smoothstep(-aa, max(aa, feather), distance);
}

vec3 aces_fitted(vec3 color)
{
    color = max(color, vec3_splat(0.0));
    return saturate((color * (2.51 * color + vec3_splat(0.03))) /
        (color * (2.43 * color + vec3_splat(0.59)) + vec3_splat(0.14)));
}

vec3 agx_approx(vec3 color)
{
    color = max(color, vec3_splat(0.0));
    vec3 compressed = log2(color + vec3_splat(1.0));
    compressed = compressed / (compressed + vec3_splat(1.65));
    return saturate(compressed * vec3_splat(1.18));
}

vec3 display_color(vec3 hdr)
{
    vec3 exposed = hdr * exp2(u_widgetTonemapParams.x);
    vec3 mapped = u_widgetTonemapParams.z < 0.0
        ? saturate(exposed)
        : (u_widgetTonemapParams.z < 0.5 ? aces_fitted(exposed) : agx_approx(exposed));
    return pow(max(mapped, vec3_splat(0.0)), vec3_splat(u_widgetTonemapParams.y));
}

void main()
{
    float kind = u_widgetEffectParams.x;
    vec2 size = max(u_widgetStyleParams.xy, vec2_splat(1.0));
    vec2 localPosition = v_widgetLocal * size;
    float coverage = rounded_coverage(localPosition, size, u_widgetCornerRadii, u_widgetStyleParams.w);
    vec4 color = u_widgetFillColor;

    if (kind > 2.5) {
        color = vec4(display_color(texture2D(s_widgetTexture, v_texcoord0).rgb), 1.0);
    } else if (kind > 1.5) {
        float alpha = texture2D(s_widgetTexture, v_texcoord0).a;
        float outline = 0.0;
        if (u_widgetEffectParams.y > 0.0) {
            vec2 texel = vec2(1.0 / max(u_widgetEffectParams.z, 1.0), 1.0 / max(u_widgetEffectParams.w, 1.0)) * u_widgetEffectParams.y;
            outline = max(outline, texture2D(s_widgetTexture, v_texcoord0 + vec2(texel.x, 0.0)).a);
            outline = max(outline, texture2D(s_widgetTexture, v_texcoord0 - vec2(texel.x, 0.0)).a);
            outline = max(outline, texture2D(s_widgetTexture, v_texcoord0 + vec2(0.0, texel.y)).a);
            outline = max(outline, texture2D(s_widgetTexture, v_texcoord0 - vec2(0.0, texel.y)).a);
            outline = max(outline, texture2D(s_widgetTexture, v_texcoord0 + texel).a);
            outline = max(outline, texture2D(s_widgetTexture, v_texcoord0 - texel).a);
            outline = max(outline, texture2D(s_widgetTexture, v_texcoord0 + vec2(texel.x, -texel.y)).a);
            outline = max(outline, texture2D(s_widgetTexture, v_texcoord0 + vec2(-texel.x, texel.y)).a);
        }
        color = mix(u_widgetBorderColor, u_widgetFillColor, alpha);
        color.a *= max(alpha, outline);
        coverage = 1.0;
    } else if (kind > 0.5) {
        color = texture2D(s_widgetTexture, v_texcoord0) * u_widgetFillColor;
    } else {
        vec2 innerMin = vec2(u_widgetBorderWidths.x, u_widgetBorderWidths.y);
        vec2 innerMax = size - vec2(u_widgetBorderWidths.z, u_widgetBorderWidths.w);
        vec2 innerSize = max(innerMax - innerMin, vec2_splat(0.0));
        vec4 innerRadii = max(u_widgetCornerRadii - vec4(
            max(u_widgetBorderWidths.x, u_widgetBorderWidths.y),
            max(u_widgetBorderWidths.z, u_widgetBorderWidths.y),
            max(u_widgetBorderWidths.z, u_widgetBorderWidths.w),
            max(u_widgetBorderWidths.x, u_widgetBorderWidths.w)), vec4_splat(0.0));
        float insideInner = rounded_coverage(localPosition - innerMin, innerSize, innerRadii, 0.0);
        color = mix(u_widgetBorderColor, u_widgetFillColor, insideInner);
    }
    color.a *= coverage * u_widgetStyleParams.z;
    gl_FragColor = color;
}
