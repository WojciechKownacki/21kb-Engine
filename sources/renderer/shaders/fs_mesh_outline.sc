$input v_normal, v_worldPos

#include <bgfx_shader.sh>

uniform vec4 u_outlineColor;   // rgb = colour, a = how strongly the silhouette behind other surfaces glows
uniform vec4 u_outlineParams;  // y = 1 for that silhouette, 0 for the outline hull

void main()
{
    if (u_outlineParams.y < 0.5) {
        gl_FragColor = vec4(u_outlineColor.rgb, 1.0);
        return;
    }
    // Behind a wall the object shows as a glow that is strongest at its edges (a lit rim); its inside, which shows the
    // object's own surfaces behind one another, stays faint.
    vec3 camera = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    float facing = clamp(dot(normalize(v_normal), normalize(camera - v_worldPos)), 0.0, 1.0);
    float rim = pow(1.0 - facing, 2.0);
    gl_FragColor = vec4(u_outlineColor.rgb, u_outlineColor.a * (0.15 + 0.85 * rim));
}
