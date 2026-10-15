$input a_position, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_color0, v_texcoord0, v_worldPos, v_shadowFlags

// Particle decals: every particle is a box (its size wide and deep, the emitter's decal depth tall,
// turned about the vertical by the particle's rotation) whose fragments project the texture onto the
// scene inside it. The instance is the particle's: i_data0 centre and size, i_data1.w rotation,
// i_data3 colour.

#include <bgfx_shader.sh>

uniform vec4 u_particleDecalParams;

void main()
{
    float size = i_data0.w;
    float depth = u_particleDecalParams.y;
    float c = cos(i_data1.w);
    float s = sin(i_data1.w);
    vec3 local = vec3(a_position.x * size, a_position.y * depth, a_position.z * size);
    vec3 world = i_data0.xyz + vec3(local.x * c - local.z * s, local.y, local.x * s + local.z * c);
    gl_Position = mul(u_viewProj, vec4(world, 1.0));
    v_color0 = i_data3;
    v_texcoord0 = vec2(size, depth);
    v_worldPos = world;
    v_shadowFlags = vec4(i_data0.xyz, i_data1.w);
}
