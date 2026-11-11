$input v_texcoord0

#include <bgfx_shader.sh>

#include "present_display.sh"

void main()
{
    vec4 scene = texture2D(s_kbFullscreenColor, v_texcoord0);
    vec3 display = display_transform(scene.rgb, v_texcoord0);
    if (u_displayAntiAliasing.x > 0.5) {
        display = fxaa_display(v_texcoord0, display);
    }
    // Quantisation dither: half an 8-bit step of
    // interleaved gradient noise hides the banding of smooth light falloff.
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    display += vec3_splat((noise - 0.5) / 255.0);
    gl_FragColor = vec4(display, mix(scene.a, 1.0, u_tonemapParams.w));
}
